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
#include <CL/sycl.hpp>
#include <StencilStream/GenericID.hpp>
#include <StencilStream/Index.hpp>
#include <StencilStream/Stencil.hpp>

enum class CellStatus
{
    Normal,
    Invalid,
    Halo,
};

struct Cell
{
    stencil::index_t c;
    stencil::index_t r;
    stencil::index_t i_generation;
    CellStatus status;
};

template <stencil::uindex_t radius>
class FPGATransFunc
{
public:
    using Cell = Cell;

    static Cell halo() { return Cell{0, 0, 0, CellStatus::Halo}; }

    Cell operator()(stencil::Stencil<Cell, radius> const &stencil) const
    {
        Cell new_cell = stencil[stencil::ID(0, 0)];

        stencil::index_t center_column = stencil.id.c;
        stencil::index_t center_row = stencil.id.r;

        bool is_valid = true;
#pragma unroll
        for (stencil::index_t c = -stencil::index_t(radius); c <= stencil::index_t(radius); c++)
        {
#pragma unroll
            for (stencil::index_t r = -stencil::index_t(radius); r <= stencil::index_t(radius); r++)
            {
                Cell old_cell = stencil[stencil::ID(c, r)];
                is_valid &= (old_cell.c == c + center_column && old_cell.r == r + center_row && old_cell.i_generation == stencil.generation) || (old_cell.status == CellStatus::Halo);
            }
        }

        if (new_cell.status == CellStatus::Normal)
        {
            if (!is_valid)
            {
                new_cell.status = CellStatus::Invalid;
            }
            new_cell.i_generation += 1;
        }

        return new_cell;
    }
};