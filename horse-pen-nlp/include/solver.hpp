#pragma once

#include "grid.hpp"
#include <vector>

struct SolveResult {
    std::vector<Pos> walls;
    int score   = 0;
    bool optimal = false; // true if Gurobi proved optimality
};

class Solver {
public:
    Solver(const Grid& grid, int budget);
    SolveResult solve();

private:
    int idx(int r, int c) const { return r * cols_ + c; }
    Pos fromIdx(int i)    const { return {i / cols_, i % cols_}; }

    const Grid& grid_;
    int budget_;
    int rows_, cols_, n_;
    int horseIdx_;

    std::vector<bool> passable_;
    std::vector<bool> wallable_;
    std::vector<bool> border_;
    std::vector<int>  tileScore_;
    std::vector<std::vector<int>> adj_; // passable neighbours + portal pairs
};
