#include "solver.hpp"
#include "gurobi_c++.h"

#include <iostream>
#include <stdexcept>
#include <cmath>

static const int DR[] = {-1, 1, 0, 0};
static const int DC[] = {0, 0, -1, 1};

// ── Constructor ───────────────────────────────────────────────────────────────

Solver::Solver(const Grid& grid, int budget)
    : grid_(grid), budget_(budget),
      rows_(grid.rows()), cols_(grid.cols()),
      n_(grid.rows() * grid.cols()),
      horseIdx_(grid.horse().r * grid.cols() + grid.horse().c)
{
    passable_.resize(n_);
    wallable_.resize(n_);
    border_.resize(n_);
    tileScore_.resize(n_);
    adj_.resize(n_);

    for (int r = 0; r < rows_; r++) {
        for (int c = 0; c < cols_; c++) {
            int i = idx(r, c);
            passable_[i]  = grid.isPassable(r, c);
            wallable_[i]  = (grid.tile(r, c) == Tile::GRASS);
            border_[i]    = grid.isBorder(r, c);
            tileScore_[i] = grid.tileScore(r, c);
        }
    }

    // Adjacency: cardinal neighbours + portal pairs (undirected)
    for (int r = 0; r < rows_; r++) {
        for (int c = 0; c < cols_; c++) {
            int i = idx(r, c);
            if (!passable_[i]) continue;
            for (int d = 0; d < 4; d++) {
                int nr = r + DR[d], nc = c + DC[d];
                if (nr < 0 || nr >= rows_ || nc < 0 || nc >= cols_) continue;
                int j = idx(nr, nc);
                if (passable_[j]) adj_[i].push_back(j);
            }
            if (grid.isPortal(r, c)) {
                Pos pair = grid.portalPair(r, c);
                if (pair.r >= 0 && passable_[idx(pair.r, pair.c)]) {
                    int j = idx(pair.r, pair.c);
                    bool found = false;
                    for (int nb : adj_[i]) if (nb == j) { found = true; break; }
                    if (!found) adj_[i].push_back(j);
                }
            }
        }
    }
}

// ── Solve ─────────────────────────────────────────────────────────────────────

SolveResult Solver::solve() {
    GRBEnv   env;
    env.set(GRB_IntParam_OutputFlag, 0);
    GRBModel model(env);

    // ── Variables ─────────────────────────────────────────────────────────────
    // x[i]    binary  tile i is enclosed (in the horse's connected region)
    // w[i]    binary  a wall is placed on tile i
    // f[i][j] continuous in [0, n]  flow from i to j along edge (i,j)
    //         Used to enforce that every x[i]=1 tile is reachable from horse.

    std::vector<GRBVar> x(n_), w(n_);
    // f is indexed by edge: for each i, for each neighbour j in adj_[i]
    // We store as f[i][k] where k is the index into adj_[i]
    std::vector<std::vector<GRBVar>> f(n_);

    double bigM = (double)n_;  // upper bound on flow

    for (int i = 0; i < n_; i++) {
        x[i] = model.addVar(0.0, 1.0, -tileScore_[i], GRB_BINARY);
        w[i] = model.addVar(0.0, 1.0,  0.0,           GRB_BINARY);
        for (int k = 0; k < (int)adj_[i].size(); k++) {
            // Flow on edge i->j, bounded by n (can't exceed total enclosed tiles)
            f[i].push_back(model.addVar(0.0, bigM, 0.0, GRB_CONTINUOUS));
        }
    }
    model.update();

    // ── Constraints ───────────────────────────────────────────────────────────

    // 1. Horse must be enclosed.
    model.addConstr(x[horseIdx_] == 1);

    for (int i = 0; i < n_; i++) {
        // 2a. Border passable tiles cannot be enclosed.
        if (border_[i] && passable_[i])
            model.addConstr(x[i] == 0);

        // 2b. Impassable tiles: no enclosure, no wall, no flow.
        if (!passable_[i]) {
            model.addConstr(x[i] == 0);
            model.addConstr(w[i] == 0);
        }

        // 3. Walls only on grass.
        if (passable_[i] && !wallable_[i])
            model.addConstr(w[i] == 0);

        // 4. Mutual exclusion: can't be enclosed and walled.
        if (passable_[i])
            model.addConstr(x[i] + w[i] <= 1);
    }

    // 5. Boundary closure: for every edge (i,j), if i is enclosed then j
    //    must be enclosed or walled. Prevents leaks to the outside.
    //    x[i] - x[j] - w[j] <= 0
    for (int i = 0; i < n_; i++) {
        if (!passable_[i]) continue;
        for (int j : adj_[i]) {
            model.addConstr(x[i] - x[j] - w[j] <= 0);
        }
    }

    // 6. Wall budget.
    GRBLinExpr wallSum;
    for (int i = 0; i < n_; i++)
        if (wallable_[i]) wallSum += w[i];
    model.addConstr(wallSum <= budget_);

    // 7. Flow-based connectivity: every enclosed tile must be reachable from
    //    the horse via a path through enclosed tiles.
    //
    //    The horse acts as a source. Each enclosed tile i consumes 1 unit of
    //    flow (it "absorbs" the flow that proves its reachability).
    //
    //    Flow conservation at each non-horse tile i:
    //      (flow in) - (flow out) = x[i]
    //    i.e. each enclosed tile absorbs exactly 1 unit; non-enclosed tiles 0.
    //
    //    At the horse (source):
    //      (flow out) - (flow in) = sum_j x[j]  -- total flow emitted = tiles enclosed
    //    But that's complex; instead we use the equivalent formulation:
    //      (flow in) - (flow out) = x[i] - (flow_emitted_if_horse)
    //    Simplest: just use net-flow = -x[i] for horse (it emits), +x[i] for others.
    //    But since horse is always enclosed (x[horse]=1), let's write it as:
    //
    //    For non-horse enclosed tiles i:
    //      sum_{j: j->i} f[j][i] - sum_{j: i->j} f[i][j] >= x[i]
    //      (net inflow >= 1 if enclosed, >= 0 if not)
    //
    //    Flow only travels along open (non-wall) edges between enclosed tiles:
    //      f[i][j] <= n * x[i]   (can only send flow from enclosed tile)
    //      f[i][j] <= n * x[j]   (can only send flow to enclosed tile)
    //      f[i][j] <= n * (1 - w[j])  (can't flow through a wall) -- redundant with x[j]+w[j]<=1

    // Build reverse adjacency index for flow conservation
    // For each tile i and each adj entry j, we need f[i][k] where adj_[i][k]=j
    // Also need to find f[j][k'] where adj_[j][k']=i for inflow at i
    // Pre-build: for each (i,j) edge store the forward flow var
    // adj_[i][k] = j means f[i][k] is flow i->j

    for (int i = 0; i < n_; i++) {
        if (!passable_[i]) continue;
        if (i == horseIdx_) continue;  // horse is the source, skip conservation

        GRBLinExpr inflow, outflow;

        // Outflow from i
        for (int k = 0; k < (int)adj_[i].size(); k++)
            outflow += f[i][k];

        // Inflow to i: find all j where i is in adj_[j]
        for (int j : adj_[i]) {  // adj is undirected so adj_[j] contains i
            for (int k = 0; k < (int)adj_[j].size(); k++) {
                if (adj_[j][k] == i) {
                    inflow += f[j][k];
                    break;
                }
            }
        }

        // Net inflow >= x[i]: if tile is enclosed it must receive at least 1 unit
        model.addConstr(inflow - outflow >= x[i]);
    }

    // Flow only along edges between enclosed non-wall tiles
    for (int i = 0; i < n_; i++) {
        if (!passable_[i]) continue;
        for (int k = 0; k < (int)adj_[i].size(); k++) {
            int j = adj_[i][k];
            // f[i->j] <= n * x[i]: flow only from enclosed tiles
            model.addConstr(f[i][k] <= bigM * x[i]);
            // f[i->j] <= n * x[j]: flow only to enclosed tiles
            model.addConstr(f[i][k] <= bigM * x[j]);
        }
    }

    // ── Solver settings ───────────────────────────────────────────────────────
    model.set(GRB_IntAttr_ModelSense, GRB_MINIMIZE);
    model.set(GRB_DoubleParam_TimeLimit, 60.0);
    model.set(GRB_IntParam_MIPFocus, 1);

    // ── Optimise ──────────────────────────────────────────────────────────────
    model.optimize();

    int status = model.get(GRB_IntAttr_Status);

    SolveResult result;

    if (status == GRB_INFEASIBLE) {
        std::cout << "  ILP: INFEASIBLE - budget too small to enclose the horse\n";
        return result;
    }
    if (status != GRB_OPTIMAL && status != GRB_TIME_LIMIT) {
        std::cout << "  ILP: unexpected status " << status << "\n";
        return result;
    }
    if (model.get(GRB_IntAttr_SolCount) == 0) {
        std::cout << "  ILP: no feasible solution found within time limit\n";
        return result;
    }

    result.optimal = (status == GRB_OPTIMAL);
    result.score   = static_cast<int>(std::round(-model.get(GRB_DoubleAttr_ObjVal)));

    for (int i = 0; i < n_; i++)
        if (w[i].get(GRB_DoubleAttr_X) > 0.5)
            result.walls.push_back(fromIdx(i));

    std::string tag = result.optimal ? "OPTIMAL" : "TIME LIMIT (best bound)";
    std::cout << "  ILP: " << tag
              << ", score=" << result.score
              << ", walls=" << result.walls.size() << "\n";

    return result;
}
