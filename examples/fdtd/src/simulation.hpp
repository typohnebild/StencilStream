/*
 * Copyright © 2020-2021 Jan-Oliver Opdenhövel, Paderborn Center for Parallel Computing, Paderborn University
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#pragma once
#include "collection.hpp"
#include "defines.hpp"
#include <StencilStream/Stencil.hpp>

// The coefficients that describe the properties of a material.
struct Material
{
    float ca;
    float cb;
    float da;
    float db;
};

// The coefficients of vaccuum.
constexpr static Material vacuum{
    (1 - (sigma * dt) / (2 * eps_0 * eps_r)) / (1 + (sigma * dt) / (2 * eps_0 * eps_r)), // ca
    (dt / (eps_0 * eps_r * dx)) / (1 + (sigma * dt) / (2 * eps_0 * eps_r)),              // cb
    (1 - (sigma * dt) / (2 * mu_0)) / (1 + (sigma * dt) / (2 * mu_0)),                   // da
    (dt / (mu_0 * dx)) / (1 + (sigma * dt) / (2 * mu_0)),                                // db
};

class FDTDKernel
{
    float disk_radius;
    double tau;
    float omega;
    float t0;
    float t_cutoff;
    float t_passed;

public:
    FDTDKernel(Parameters const &parameters, float t_passed) : disk_radius(parameters.disk_radius), tau(parameters.tau()), omega(parameters.omega()), t0(parameters.t0()), t_cutoff(parameters.t_cutoff()), t_passed(t_passed) {}

    static FDTDCell halo()
    {
        FDTDCell new_cell;
        new_cell.ex = 0;
        new_cell.ey = 0;
        new_cell.hz = 0;
        new_cell.hz_sum = 0;
        return new_cell;
    }

    FDTDCell operator()(Stencil<FDTDCell, stencil_radius> const &stencil) const
    {
        FDTDCell cell = stencil[ID(0, 0)];

        if (stencil.generation == 0)
        {
            cell.hz_sum = 0;
        }

        float_vec center_cell_row;
#pragma unroll
        for (uindex_t i = 0; i < vector_len; i++)
        {
            center_cell_row[i] = vector_len * stencil.id.r + i; // 1 + 8 FOs
        }
        float_vec center_cell_column = float_vec(stencil.id.c);

        float_vec a = center_cell_row - (disk_radius + 1.0);    // 9 FOs
        float_vec b = center_cell_column - (disk_radius + 1.0); // 9 FOs
        float_vec distance = cl::sycl::sqrt(a * a + b * b);     // 8 * 4 = 32 FOs

        float_vec ca, cb, da, db;
#pragma unroll
        for (uindex_t i = 0; i < vector_len; i++)
        {
            if (distance[i] >= disk_radius)
            {
                ca[i] = da[i] = 1;
                cb[i] = db[i] = 0;
            }
            else
            {
                ca[i] = vacuum.ca;
                cb[i] = vacuum.cb;
                da[i] = vacuum.da;
                db[i] = vacuum.db;
            }
        }

        if ((stencil.stage & 0b1) == 0)
        {
            float_vec left_neighbours, top_neighbours;
            left_neighbours = stencil[ID(-1, 0)].hz;
            top_neighbours = stencil[ID(0, 0)].hz;
#pragma unroll
            for (uindex_t i = 0; i < vector_len - 1; i++)
            {
                top_neighbours[i + 1] = top_neighbours[i];
            }
            top_neighbours[0] = stencil[ID(0, -1)].hz[vector_len - 1];

            cell.ex *= ca;
            cell.ex += cb * (stencil[ID(0, 0)].hz - top_neighbours);

            cell.ey *= ca;
            cell.ey += cb * (left_neighbours - stencil[ID(0, 0)].hz);
        }
        else
        {
            float_vec right_neighbours, bottom_neighbours;
            right_neighbours = stencil[ID(1, 0)].ey;
            bottom_neighbours = stencil[ID(0, 0)].ex;
#pragma unroll
            for (uindex_t i = 0; i < vector_len - 1; i++)
            {
                bottom_neighbours[i] = bottom_neighbours[i + 1];
            }
            bottom_neighbours[vector_len - 1] = stencil[ID(0, 1)].ex[0];

            cell.hz *= da;
            cell.hz += db * (bottom_neighbours - stencil[ID(0, 0)].ex + stencil[ID(0, 0)].ey - right_neighbours);

            float current_time = t_passed + (stencil.generation >> 1) * dt;
            if (current_time < t_cutoff)
            {
                float wave_progress = double(current_time - t0) / tau;
                cell.hz += cl::sycl::cos(omega * current_time) * cl::sycl::exp(-1 * wave_progress * wave_progress);
            }

            cell.hz_sum += cell.hz * cell.hz; // 2*8 = 16 FOs
        }
        return cell;
    }
};