/*
 * Copyright © 2020-2021 Jan-Oliver Opdenhövel, Paderborn Center for Parallel Computing, Paderborn
 * University
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 * associated documentation files (the “Software”), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "simulation.hpp"
#include <StencilStream/MonotileExecutor.hpp>
#include <StencilStream/StencilExecutor.hpp>
#include <deque>

#ifdef MONOTILE
using Executor = MonotileExecutor<FDTDCell, stencil_radius, FDTDKernel, pipeline_length, tile_width,
                                  tile_height>;
#else
using Executor = StencilExecutor<FDTDCell, stencil_radius, FDTDKernel, pipeline_length, tile_width,
                                 tile_height, FDTD_BURST_SIZE>;
#endif

#ifdef HARDWARE
using selector = INTEL::fpga_selector;
#else
using selector = INTEL::fpga_emulator_selector;
#endif

auto exception_handler = [](cl::sycl::exception_list exceptions) {
    for (std::exception_ptr const &e : exceptions) {
        try {
            std::rethrow_exception(e);
        } catch (cl::sycl::exception const &e) {
            std::cout << "Caught asynchronous SYCL exception:\n" << e.what() << "\n";
            std::terminate();
        }
    }
};

enum class CellField {
    EX,
    EY,
    HZ,
    HZ_SUM,
    DISTANCE,
};

void save_frame(cl::sycl::buffer<FDTDCell, 2> frame_buffer, uindex_t generation_index,
                CellField field, Parameters const &parameters) {
    auto frame = frame_buffer.get_access<access::mode::read>();

    ostringstream frame_path;
    frame_path << parameters.out_dir << "/";
    switch (field) {
    case CellField::EX:
        frame_path << "ex";
        break;
    case CellField::EY:
        frame_path << "ey";
        break;
    case CellField::HZ:
        frame_path << "hz";
        break;
    case CellField::HZ_SUM:
        frame_path << "hz_sum";
        break;
    case CellField::DISTANCE:
        frame_path << "distance";
        break;
    default:
        break;
    }
    frame_path << "." << generation_index << ".csv";
    std::ofstream out(frame_path.str());

    for (uindex_t r = 0; r < frame.get_range()[1]; r++) {
        for (uindex_t c = 0; c < frame.get_range()[0]; c++) {
            switch (field) {
            case CellField::EX:
                out << frame[c][r].ex;
                break;
            case CellField::EY:
                out << frame[c][r].ey;
                break;
            case CellField::HZ:
                out << frame[c][r].hz;
                break;
            case CellField::HZ_SUM:
                out << frame[c][r].hz_sum;
                break;
            case CellField::DISTANCE:
                out << frame[c][r].distance;
                break;
            default:
                break;
            }

            if (c != frame.get_range()[0] - 1) {
                out << ",";
            }
        }
        if (r != frame.get_range()[1] - 1) {
            out << std::endl;
        }
    }
}

int main(int argc, char **argv) {
    Parameters parameters(argc, argv);
    parameters.print_configuration();

#ifdef MONOTILE
    if (parameters.grid_range()[0] > tile_width || parameters.grid_range()[1] > tile_height) {
        std::cerr << "Error: The grid may not exceed the size of the tile (" << tile_width << " by "
                  << tile_height << " cells) when using the monotile architecture." << std::endl;
        exit(1);
    }
#endif

    selector device_selector;
    cl::sycl::queue fpga_queue(device_selector, exception_handler,
                               {property::queue::enable_profiling{}});

    cl::sycl::buffer<FDTDCell, 2> grid_buffer(parameters.grid_range());
    {
        auto init_ac = grid_buffer.get_access<cl::sycl::access::mode::discard_write>();
        for (uindex_t c = 0; c < parameters.grid_range()[0]; c++) {
            for (uindex_t r = 0; r < parameters.grid_range()[1]; r++) {
                init_ac[c][r] = FDTDKernel::halo();

                float a = float(c) - float(parameters.grid_range()[0]) / 2.0;
                float b = float(r) - float(parameters.grid_range()[1]) / 2.0;
                init_ac[c][r].distance = parameters.dx * std::sqrt(a * a + b * b);
            }
        }
    }

    Executor executor(FDTDKernel::halo(), FDTDKernel(parameters));
    executor.set_input(grid_buffer);
    executor.set_queue(fpga_queue);

    uindex_t n_timesteps = parameters.n_timesteps();

    std::cout << "Simulating..." << std::endl;
    if (parameters.interval().has_value()) {
        uindex_t interval = 2 * *(parameters.interval());
        double runtime = 0.0;

        while (executor.get_i_generation() + interval < n_timesteps) {
            executor.run(interval);
            executor.copy_output(grid_buffer);

            uindex_t i_generation = executor.get_i_generation();
            save_frame(grid_buffer, i_generation, CellField::EX, parameters);
            save_frame(grid_buffer, i_generation, CellField::EY, parameters);
            save_frame(grid_buffer, i_generation, CellField::HZ, parameters);
            save_frame(grid_buffer, i_generation, CellField::HZ_SUM, parameters);
            save_frame(grid_buffer, i_generation, CellField::DISTANCE, parameters);

            runtime += executor.get_runtime_sample().get_total_runtime();
            double progress = 100.0 * (double(i_generation) / double(n_timesteps));
            double speed = double(i_generation) / runtime;
            double time_remaining = double(n_timesteps - i_generation) / speed;

            std::cout << "Progress: " << i_generation << "/" << n_timesteps;
            std::cout << " timesteps completed (" << progress << "%)";
            std::cout << ", ~" << time_remaining << "s left." << std::endl;
        }

        if (n_timesteps % interval != 0) {
            executor.run(n_timesteps % interval);
        }
    } else {
        executor.run(n_timesteps);
    }
    std::cout << "Simulation complete!" << std::endl;

    executor.copy_output(grid_buffer);
    save_frame(grid_buffer, n_timesteps, CellField::HZ_SUM, parameters);

    return 0;
}
