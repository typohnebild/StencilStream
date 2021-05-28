/*
 * Copyright © 2020-2021 Jan-Oliver Opdenhövel, Paderborn Center for Parallel Computing, Paderborn University
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "../res/FPGATransFunc.hpp"
#include "../res/HostPipe.hpp"
#include "../res/catch.hpp"
#include "../res/constants.hpp"
#include <StencilStream/ExecutionKernel.hpp>

using namespace stencil;
using namespace std;
using namespace cl::sycl;

void test_kernel(uindex_t n_generations)
{
    using TransFunc = FPGATransFunc<stencil_radius>;
    using in_pipe = HostPipe<class ExecutionKernelInPipeID, TransFunc::Cell>;
    using out_pipe = HostPipe<class ExecutionKernelOutPipeID, TransFunc::Cell>;
    using TestExecutionKernel = ExecutionKernel<TransFunc, TransFunc::Cell, stencil_radius, pipeline_length, tile_width, tile_height, in_pipe, out_pipe>;

    for (index_t c = -halo_radius; c < index_t(halo_radius + tile_width); c++)
    {
        for (index_t r = -halo_radius; r < index_t(halo_radius + tile_height); r++)
        {
            if (c >= index_t(0) && c < index_t(tile_width) && r >= index_t(0) && r < index_t(tile_height))
            {
                in_pipe::write(TransFunc::Cell(c, r, 0, 0));
            }
            else
            {
                in_pipe::write(TransFunc::halo());
            }
        }
    }

    TestExecutionKernel(TransFunc(), 0, n_generations, 0, 0, tile_width, tile_height, TransFunc::halo())();

    buffer<TransFunc::Cell, 2> output_buffer(range<2>(tile_width, tile_height));

    {
        auto output_buffer_ac = output_buffer.get_access<access::mode::discard_write>();
        for (uindex_t c = 0; c < tile_width; c++)
        {
            for (uindex_t r = 0; r < tile_height; r++)
            {
                output_buffer_ac[c][r] = out_pipe::read();
            }
        }
    }

    REQUIRE(in_pipe::empty());
    REQUIRE(out_pipe::empty());

    auto output_buffer_ac = output_buffer.get_access<access::mode::read>();
    for (uindex_t c = 1; c < tile_width; c++)
    {
        for (uindex_t r = 1; r < tile_height; r++)
        {
            TransFunc::Cell cell = output_buffer_ac[c][r];
            REQUIRE(cell[0] == c);
            REQUIRE(cell[1] == r);
            REQUIRE(cell[2] == n_generations);
            REQUIRE(cell[3] == 0);
        }
    }
}

TEST_CASE("ExecutionKernel", "[ExecutionKernel]")
{
    test_kernel(pipeline_length);
}

TEST_CASE("ExecutionKernel (partial pipeline)", "[ExecutionKernel]")
{
    static_assert(pipeline_length != 1);
    test_kernel(pipeline_length - 1);
}

TEST_CASE("ExecutionKernel (noop)", "[ExecutionKernel]")
{
    test_kernel(0);
}

TEST_CASE("Halo values inside the pipeline are handled correctly", "[ExecutionKernel]")
{
    auto my_kernel = [=](Stencil<bool, stencil_radius> const &stencil, StencilInfo const &info) {
        ID idx = info.center_cell_id;
        bool is_valid = true;
        if (idx.c == 0)
        {
            is_valid &= stencil[ID(-1, -1)] && stencil[ID(-1, 0)] && stencil[ID(-1, 1)];
        }
        else if (idx.c == tile_width - 1)
        {
            is_valid &= stencil[ID(1, -1)] && stencil[ID(1, 0)] && stencil[ID(1, 1)];
        }

        if (idx.r == 0)
        {
            is_valid &= stencil[ID(-1, -1)] && stencil[ID(0, -1)] && stencil[ID(1, -1)];
        }
        else if (idx.r == tile_height - 1)
        {
            is_valid &= stencil[ID(-1, 1)] && stencil[ID(0, 1)] && stencil[ID(1, 1)];
        }

        return is_valid;
    };

    using in_pipe = HostPipe<class HaloValueTestInPipeID, bool>;
    using out_pipe = HostPipe<class HaloValueTestOutPipeID, bool>;
    using TestExecutionKernel = ExecutionKernel<decltype(my_kernel), bool, stencil_radius, pipeline_length, tile_width, tile_height, in_pipe, out_pipe>;

    for (index_t c = -halo_radius; c < index_t(halo_radius + tile_width); c++)
    {
        for (index_t r = -halo_radius; r < index_t(halo_radius + tile_height); r++)
        {
            in_pipe::write(false);
        }
    }

    TestExecutionKernel(my_kernel, 0, pipeline_length, 0, 0, tile_width, tile_height, true)();

    for (uindex_t c = 0; c < tile_width; c++)
    {
        for (uindex_t r = 0; r < tile_height; r++)
        {
            REQUIRE(out_pipe::read());
        }
    }

    REQUIRE(in_pipe::empty());
    REQUIRE(out_pipe::empty());
}