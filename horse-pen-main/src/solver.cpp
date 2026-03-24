#include "solver.hpp"
#include <algorithm>
#include <iostream>
#include <queue>
#include <cmath>

static const int DR[] = {-1, 1, 0, 0};
static const int DC[] = {0, 0, -1, 1};

Solver::Solver(const Grid& grid, int budget)
    : grid_(grid), budget_(budget), bestScore_(-1),
      n_(grid.rows() * grid.cols()),
      rows_(grid.rows()), cols_(grid.cols()),
      horseIdx_(grid.horse().r * grid.cols() + grid.horse().c),
      cur_gen_(0) {

    passable_.resize(n_);
    border_.resize(n_);
    wallable_.resize(n_);
    tileScore_.resize(n_);
    portalPair_.resize(n_, -1);
    visited_gen_.resize(n_, 0);
    adj_.resize(n_);

    for (int r = 0; r < rows_; r++) {
        for (int c = 0; c < cols_; c++) {
            int i = r * cols_ + c;
            passable_[i] = grid.isPassable(r, c);
            border_[i] = grid.isBorder(r, c);
            tileScore_[i] = grid.tileScore(r, c);
            Tile t = grid.tile(r, c);
            wallable_[i] = (t == Tile::GRASS);
            if (grid.isPortal(r, c)) {
                Pos pair = grid.portalPair(r, c);
                if (pair.r >= 0) portalPair_[i] = pair.r * cols_ + pair.c;
            }
        }
    }

    for (int i = 0; i < n_; i++) {
        if (!passable_[i]) continue;
        int r = i / cols_, c = i % cols_;
        for (int d = 0; d < 4; d++) {
            int nr = r + DR[d], nc = c + DC[d];
            if (nr < 0 || nr >= rows_ || nc < 0 || nc >= cols_) continue;
            int ni = nr * cols_ + nc;
            if (passable_[ni]) adj_[i].push_back(ni);
        }
        if (portalPair_[i] >= 0 && passable_[portalPair_[i]])
            adj_[i].push_back(portalPair_[i]);
    }
}

bool Solver::timeUp() const {
    return std::chrono::steady_clock::now() >= deadline_;
}

void Solver::updateBest(const std::vector<char>& walls, int score) {
    if (score > bestScore_) {
        bestScore_ = score;
        bestWalls_ = walls;
    }
}

// bfs from horse pos, return if horse can escape and total score
// with a config
std::pair<bool, int> Solver::evaluate(const std::vector<char>& isWall) const {
    ++cur_gen_;
    static thread_local std::vector<int> q_buf;
    q_buf.clear();

    visited_gen_[horseIdx_] = cur_gen_;
    q_buf.push_back(horseIdx_);
    bool escaped = false;
    int score = 0;

    for (size_t head = 0; head < q_buf.size(); head++) {
        int cur = q_buf[head];
        score += tileScore_[cur];
        if (border_[cur]) escaped = true;

        for (int nb : adj_[cur]) {
            if (visited_gen_[nb] != cur_gen_ && !isWall[nb]) {
                visited_gen_[nb] = cur_gen_;
                q_buf.push_back(nb);
            }
        }
    }
    return {escaped, score};
}

std::pair<int, int> Solver::evaluateFull(const std::vector<char>& isWall) const {
    ++cur_gen_;
    static thread_local std::vector<int> q_buf;
    q_buf.clear();

    visited_gen_[horseIdx_] = cur_gen_;
    q_buf.push_back(horseIdx_);
    int borderCount = 0;
    int score = 0;

    for (size_t head = 0; head < q_buf.size(); head++) {
        int cur = q_buf[head];
        score += tileScore_[cur];
        if (border_[cur]) borderCount++;

        for (int nb : adj_[cur]) {
            if (visited_gen_[nb] != cur_gen_ && !isWall[nb]) {
                visited_gen_[nb] = cur_gen_;
                q_buf.push_back(nb);
            }
        }
    }
    return {borderCount, score};
}

std::pair<std::vector<char>, int> Solver::greedyExpand(std::mt19937& rng, double randomness) {
    std::vector<bool> inS(n_, false);
    std::vector<bool> inBoundary(n_, false);
    std::vector<int> boundaryList;

    inS[horseIdx_] = true;
    int score = tileScore_[horseIdx_];

    for (int nb : adj_[horseIdx_]) {
        if (!inBoundary[nb]) {
            inBoundary[nb] = true;
            boundaryList.push_back(nb);
        }
    }

    int boundarySize = 0;
    for (int nb : boundaryList) {
        if (wallable_[nb]) boundarySize++;
    }

    int bestLocalScore = -1;
    std::vector<char> bestLocalWalls;

    auto absorbNonWallable = [&]() {
        bool changed = true;
        while (changed) {
            changed = false;
            for (int i = 0; i < (int)boundaryList.size(); i++) {
                int tile = boundaryList[i];
                if (!inBoundary[tile]) continue;
                if (wallable_[tile]) continue;
                if (border_[tile]) continue;
                inS[tile] = true;
                score += tileScore_[tile];
                inBoundary[tile] = false;
                for (int nb : adj_[tile]) {
                    if (!inS[nb] && !inBoundary[nb]) {
                        inBoundary[nb] = true;
                        boundaryList.push_back(nb);
                        if (wallable_[nb]) boundarySize++;
                    }
                }
                changed = true;
            }
        }
    };
    absorbNonWallable();

    auto recordIfValid = [&]() {
        bool valid = (boundarySize <= budget_);
        if (valid) {
            for (int i = 0; i < (int)boundaryList.size(); i++) {
                int tile = boundaryList[i];
                if (!inBoundary[tile]) continue;
                if (!wallable_[tile]) { valid = false; break; }
            }
        }
        if (valid && score > bestLocalScore) {
            bestLocalScore = score;
            bestLocalWalls.assign(n_, 0);
            for (int i = 0; i < (int)boundaryList.size(); i++) {
                if (inBoundary[boundaryList[i]])
                    bestLocalWalls[boundaryList[i]] = 1;
            }
        }
    };
    recordIfValid();

    struct Candidate { int tile; double value; };
    std::vector<Candidate> candidates;

    // Helper to estimate reachable score through a portal
    auto estimatePortalValue = [&](int portalIdx) -> int {
        if (portalPair_[portalIdx] < 0) return 0;
        int pairIdx = portalPair_[portalIdx];
        if (inS[pairIdx] || inBoundary[pairIdx]) return 0; // already included
        
        // BFS from portal pair to estimate reachable high-value region
        ++cur_gen_;
        static thread_local std::vector<int> portal_q;
        portal_q.clear();
        portal_q.push_back(pairIdx);
        visited_gen_[pairIdx] = cur_gen_;
        
        int reachableScore = 0;
        int reachableTiles = 0;
        for (size_t head = 0; head < portal_q.size() && reachableTiles < 50; head++) {
            int cur = portal_q[head];
            reachableScore += tileScore_[cur];
            reachableTiles++;
            
            for (int nb : adj_[cur]) {
                if (visited_gen_[nb] != cur_gen_ && !inS[nb] && !inBoundary[nb]) {
                    visited_gen_[nb] = cur_gen_;
                    portal_q.push_back(nb);
                }
            }
        }
        return reachableScore;
    };

    while (true) {
        candidates.clear();

        for (int i = 0; i < (int)boundaryList.size(); i++) {
            int tile = boundaryList[i];
            if (!inBoundary[tile]) continue;
            if (border_[tile]) continue;

            int newWallable = 0;
            for (int nb : adj_[tile]) {
                if (!inS[nb] && !inBoundary[nb] && wallable_[nb]) newWallable++;
            }
            int costChange = (wallable_[tile] ? -1 : 0) + newWallable;
            int newBoundarySize = boundarySize + costChange;
            if (newBoundarySize > budget_) continue;

            double value;
            int effectiveScore = tileScore_[tile];
            
            // Boost value if this is a portal with high-value accessible region
            if (portalPair_[tile] >= 0) {
                int portalBonus = estimatePortalValue(tile);
                effectiveScore += portalBonus / 5; // discount future value
            }
            
            if (costChange <= 0) value = 1e6 + effectiveScore;
            else value = (double)effectiveScore / (1.0 + costChange);
            candidates.push_back({tile, value});
        }

        if (candidates.empty()) break;

        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) { return a.value > b.value; });

        int pick = 0;
        if (randomness > 0 && candidates.size() > 1) {
            int topK = std::max(2, (int)(candidates.size() * randomness));
            topK = std::min(topK, (int)candidates.size());
            std::uniform_int_distribution<int> dist(0, topK - 1);
            pick = dist(rng);
        }

        int chosen = candidates[pick].tile;
        inS[chosen] = true;
        score += tileScore_[chosen];
        inBoundary[chosen] = false;
        if (wallable_[chosen]) boundarySize--;

        for (int nb : adj_[chosen]) {
            if (!inS[nb] && !inBoundary[nb]) {
                inBoundary[nb] = true;
                boundaryList.push_back(nb);
                if (wallable_[nb]) boundarySize++;
            }
        }
        absorbNonWallable();
        recordIfValid();
    }

    return {bestLocalWalls, bestLocalScore};
}

std::pair<std::vector<char>, int> Solver::reverseGreedy(std::mt19937& rng, double randomness) {
    std::vector<char> isWall(n_, 0);
    int wallCount = 0;

    while (wallCount < budget_) {
        auto [baseBorder, baseScore] = evaluateFull(isWall);
        if (baseBorder == 0) {
            return {isWall, baseScore};
        }

        // Find all reachable wallable tiles
        ++cur_gen_;
        visited_gen_[horseIdx_] = cur_gen_;
        std::vector<int> reachable = {horseIdx_};
        for (size_t head = 0; head < reachable.size(); head++) {
            int cur = reachable[head];
            for (int nb : adj_[cur]) {
                if (visited_gen_[nb] != cur_gen_ && !isWall[nb]) {
                    visited_gen_[nb] = cur_gen_;
                    reachable.push_back(nb);
                }
            }
        }

        // Try each reachable wallable tile: pick the one that reduces border
        // count the most while preserving the most score
        struct Candidate { int tile; int borderAfter; int scoreAfter; };
        std::vector<Candidate> candidates;

        for (int t : reachable) {
            if (!wallable_[t] || t == horseIdx_) continue;
            isWall[t] = 1;
            auto [borderAfter, scoreAfter] = evaluateFull(isWall);
            isWall[t] = 0;

            if (borderAfter < baseBorder) {
                candidates.push_back({t, borderAfter, scoreAfter});
            }
        }

        if (candidates.empty()) {
            return {{}, -1};
        }

        // Sort: highest score first (preserve largest area), break ties by
        // fewer remaining borders (most progress toward enclosure)
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) {
                      if (a.scoreAfter != b.scoreAfter) return a.scoreAfter > b.scoreAfter;
                      return a.borderAfter < b.borderAfter;
                  });

        int pick = 0;
        if (randomness > 0 && candidates.size() > 1) {
            int topK = std::max(2, (int)(candidates.size() * randomness));
            topK = std::min(topK, (int)candidates.size());
            std::uniform_int_distribution<int> dist(0, topK - 1);
            pick = dist(rng);
        }

        isWall[candidates[pick].tile] = 1;
        wallCount++;
    }

    auto [borderCount, score] = evaluateFull(isWall);
    if (borderCount == 0) return {isWall, score};
    return {{}, -1};
}

bool Solver::repairWalls(std::vector<char>& isWall, std::mt19937& rng) {
    int wallCount = 0;
    for (int i = 0; i < n_; i++) wallCount += isWall[i];

    for (int i = 0; i < n_; i++) {
        if (isWall[i] && !wallable_[i]) { isWall[i] = 0; wallCount--; }
    }

    for (int iter = 0; iter < budget_ * 3 && wallCount <= budget_; iter++) {
        ++cur_gen_;
        std::vector<int> parent(n_, -1);
        std::vector<int> q;
        visited_gen_[horseIdx_] = cur_gen_;
        q.push_back(horseIdx_);
        int escapeTile = -1;

        for (size_t head = 0; head < q.size(); head++) {
            int cur = q[head];
            if (border_[cur] && cur != horseIdx_) {
                escapeTile = cur;
                break;
            }
            for (int nb : adj_[cur]) {
                if (visited_gen_[nb] != cur_gen_ && !isWall[nb]) {
                    visited_gen_[nb] = cur_gen_;
                    parent[nb] = cur;
                    q.push_back(nb);
                }
            }
        }

        if (escapeTile < 0) return true;
        if (wallCount >= budget_) return false;

        // trace the escape path
        std::vector<int> path;
        for (int t = escapeTile; t != horseIdx_ && t >= 0; t = parent[t]) {
            path.push_back(t);
        }

        // collect all wallable tiles on the escape path
        std::vector<int> pathCandidates;
        for (int t : path) {
            if (wallable_[t] && !isWall[t]) pathCandidates.push_back(t);
        }
        if (pathCandidates.empty()) {
            for (int t : path) {
                for (int nb : adj_[t]) {
                    if (wallable_[nb] && !isWall[nb] && nb != horseIdx_) {
                        pathCandidates.push_back(nb);
                    }
                }
                if (!pathCandidates.empty()) break;
            }
        }
        // pick randomly from candidates for diverse enclosures
        int bestBlock = pathCandidates.empty() ? -1 :
            pathCandidates[rng() % pathCandidates.size()];
        if (bestBlock < 0) return false;

        isWall[bestBlock] = 1;
        wallCount++;
    }
    return false;
}

// steepest-descent with 1-swap and 2-swap
void Solver::localSearch(std::vector<char>& isWall, int& currentScore) {
    std::vector<int> allWallable;
    for (int i = 0; i < n_; i++) {
        if (wallable_[i] && i != horseIdx_) allWallable.push_back(i);
    }

    bool improved = true;
    while (improved && !timeUp()) {
        improved = false;

        std::vector<int> walls;
        for (int i = 0; i < n_; i++) {
            if (isWall[i]) walls.push_back(i);
        }

        // 1-swap: best improvement
        int bestSc = currentScore;
        int bestRemove = -1, bestAdd = -1;

        for (int wi = 0; wi < (int)walls.size() && !timeUp(); wi++) {
            int w = walls[wi];
            isWall[w] = 0;

            for (int t : allWallable) {
                if (isWall[t] || t == w) continue;
                isWall[t] = 1;
                auto [esc, sc] = evaluate(isWall);
                isWall[t] = 0;
                if (!esc && sc > bestSc) {
                    bestSc = sc;
                    bestRemove = w;
                    bestAdd = t;
                }
            }
            isWall[w] = 1;
        }

        if (bestRemove >= 0) {
            isWall[bestRemove] = 0;
            isWall[bestAdd] = 1;
            currentScore = bestSc;
            improved = true;
            continue;
        }

        // 2-swap: remove two walls, try placing two elsewhere (we take the first improvement, since
        // this takes literally forever)
        if (walls.size() >= 2 && !timeUp()) {
            for (int wi = 0; wi < (int)walls.size() && !improved && !timeUp(); wi++) {
                for (int wj = wi + 1; wj < (int)walls.size() && !improved && !timeUp(); wj++) {
                    int w1 = walls[wi], w2 = walls[wj];
                    isWall[w1] = 0;
                    isWall[w2] = 0;

                    for (int ti = 0; ti < (int)allWallable.size() && !improved && !timeUp(); ti++) {
                        int t1 = allWallable[ti];
                        if (isWall[t1]) continue;
                        isWall[t1] = 1;

                        for (int tj = ti + 1; tj < (int)allWallable.size(); tj++) {
                            int t2 = allWallable[tj];
                            if (isWall[t2]) continue;
                            isWall[t2] = 1;
                            auto [esc, sc] = evaluate(isWall);
                            isWall[t2] = 0;
                            if (!esc && sc > currentScore) {
                                isWall[t2] = 1;
                                currentScore = sc;
                                improved = true;
                                break;
                            }
                        }
                        if (!improved) isWall[t1] = 0;
                    }

                    if (!improved) {
                        isWall[w1] = 1;
                        isWall[w2] = 1;
                    }
                }
            }
        }
    }

    updateBest(isWall, currentScore);
}

// fast 1-swap for simulated annealing, only does 1-swap
void Solver::fastLocalSearch(std::vector<char>& isWall, int& currentScore) {
    std::vector<int> allWallable;
    for (int i = 0; i < n_; i++) {
        if (wallable_[i] && i != horseIdx_) allWallable.push_back(i);
    }

    bool improved = true;
    while (improved && !timeUp()) {
        improved = false;

        std::vector<int> walls;
        for (int i = 0; i < n_; i++) {
            if (isWall[i]) walls.push_back(i);
        }

        int bestSc = currentScore;
        int bestRemove = -1, bestAdd = -1;

        for (int wi = 0; wi < (int)walls.size() && !timeUp(); wi++) {
            int w = walls[wi];
            isWall[w] = 0;

            for (int t : allWallable) {
                if (isWall[t] || t == w) continue;
                isWall[t] = 1;
                auto [esc, sc] = evaluate(isWall);
                isWall[t] = 0;
                if (!esc && sc > bestSc) {
                    bestSc = sc;
                    bestRemove = w;
                    bestAdd = t;
                }
            }
            isWall[w] = 1;
        }

        if (bestRemove >= 0) {
            isWall[bestRemove] = 0;
            isWall[bestAdd] = 1;
            currentScore = bestSc;
            improved = true;
        }
    }

    updateBest(isWall, currentScore);
}

// simulated annealing from a starting wall config
void Solver::anneal(std::vector<char>& isWall, int& currentScore, std::mt19937& rng,
                    double startTemp, double endTemp, int maxIter) {
    std::vector<int> allWallable;
    for (int i = 0; i < n_; i++) {
        if (wallable_[i] && i != horseIdx_) allWallable.push_back(i);
    }
    if (allWallable.empty()) return;

    std::uniform_real_distribution<double> unif(0.0, 1.0);
    int nw = (int)allWallable.size();

    for (int iter = 0; iter < maxIter && !timeUp(); iter++) {
        double t = startTemp * std::pow(endTemp / startTemp, (double)iter / maxIter);

        // Pick a random wall and a random non-wall wallable tile, swap them
        // First collect current walls
        std::vector<int> walls;
        for (int i = 0; i < n_; i++) {
            if (isWall[i]) walls.push_back(i);
        }
        if (walls.empty()) break;

        int wi = rng() % walls.size();
        int ti = rng() % nw;
        int w = walls[wi];
        int newt = allWallable[ti];
        if (isWall[newt]) continue; // already a wall

        isWall[w] = 0;
        isWall[newt] = 1;

        auto [esc, sc] = evaluate(isWall);

        if (!esc && sc >= currentScore) {
            // Accept improvement
            currentScore = sc;
            updateBest(isWall, sc);
        } else if (!esc && t > 0 && unif(rng) < std::exp((sc - currentScore) / t)) {
            // Accept worse solution with SA probability
            currentScore = sc;
        } else {
            // Reject
            isWall[w] = 1;
            isWall[newt] = 0;
        }
    }
}

SolveResult Solver::solve() {
    deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(25);
    bestScore_ = -1;
    bestWalls_.assign(n_, 0);

    std::mt19937 rng(42);

    std::vector<int> allWallable;
    for (int i = 0; i < n_; i++) {
        if (wallable_[i] && i != horseIdx_) allWallable.push_back(i);
    }

    // it's very important to me that we get the solution quickly,
    // so we use the timeUp function and a deadline so that
    // the greedy approach and simulated annealing doesn't go on for too long
    std::vector<std::pair<std::vector<char>, int>> startingPoints;

    // we let phase 1 have 2 seconds
    auto phase1_end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    int trial = 0;
    while (std::chrono::steady_clock::now() < phase1_end) {
        double randomness;
        if (trial == 0) randomness = 0.0;
        else randomness = 0.1 + 0.9 * (trial % 20) / 19.0;

        auto [walls, score] = greedyExpand(rng, randomness);
        if (score > 0) {
            updateBest(walls, score);
            if (trial % 100 == 0 || score >= bestScore_ - 3) {
                startingPoints.push_back({walls, score});
            }
        }
        trial++;
    }
    std::cout << "Best greedy: " << bestScore_
              << " (" << startingPoints.size() << " starting points, "
              << trial << " trials)" << std::endl;

    // Phase 1b: random repair from scratch (diverse enclosures)
    auto phase1b_end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    int repairTrial = 0;
    while (std::chrono::steady_clock::now() < phase1b_end) {
        std::vector<char> isWall(n_, 0);
        if (repairWalls(isWall, rng)) {
            auto [esc, score] = evaluate(isWall);
            if (!esc && score > 0) {
                updateBest(isWall, score);
                startingPoints.push_back({isWall, score});
            }
        }
        repairTrial++;
    }
    std::cout << "Best after random repair: " << bestScore_
              << " (" << repairTrial << " trials)" << std::endl;

    // local search and simulated annealing from the best 30 start
    std::sort(startingPoints.begin(), startingPoints.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    std::vector<std::pair<std::vector<char>, int>> unique_starts;
    for (auto& [walls, score] : startingPoints) {
        if (score < bestScore_ - 10) continue;
        bool dup = false;
        for (auto& [uw, us] : unique_starts) {
            if (walls == uw) { dup = true; break; }
        }
        if (!dup) unique_starts.push_back({walls, score});
        if (unique_starts.size() >= 30) break;
    }

    auto phase2_end = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    int searched = 0;
    for (auto& [walls, score] : unique_starts) {
        if (timeUp() || std::chrono::steady_clock::now() >= phase2_end) break;
        localSearch(walls, score);
        searched++;
    }
    std::cout << "After local search (" << searched << " starts): " << bestScore_ << std::endl;

    // while we still have time left, we do continuous simulated annealing
    // restarting from the best and adding perturbation, anywhere from 1 tile
    // up to budget / 2
    int restarts = 0;
    while (!timeUp()) {
        auto walls = bestWalls_;
        int sc = bestScore_;

        // Perturb: randomly swap 1-budget/2 walls
        std::vector<int> wallPos;
        for (int i = 0; i < n_; i++) {
            if (walls[i]) wallPos.push_back(i);
        }
        if (wallPos.empty()) break;

        int k = 1 + rng() % std::max(1, (int)wallPos.size());
        std::shuffle(wallPos.begin(), wallPos.end(), rng);

        for (int i = 0; i < k; i++) walls[wallPos[i]] = 0;

        // Try repair
        if (repairWalls(walls, rng)) {
            auto [esc, score] = evaluate(walls);
            if (!esc) {
                sc = score;
                anneal(walls, sc, rng, 8.0, 0.05, 30000);
                if (!timeUp()) fastLocalSearch(walls, sc);
            }
        } else {
            // Just do SA from a random greedy
            double randomness = 0.3 + 0.7 * (rng() % 1000) / 999.0;
            auto [gw, gs] = greedyExpand(rng, randomness);
            if (gs > 0) {
                walls = gw;
                sc = gs;
                anneal(walls, sc, rng, 8.0, 0.05, 30000);
                if (!timeUp()) fastLocalSearch(walls, sc);
            }
        }
        restarts++;
    }
    if (restarts > 0)
        std::cout << "After SA restarts (" << restarts << "): " << bestScore_ << std::endl;

    SolveResult result;
    result.score = bestScore_;
    for (int i = 0; i < n_; i++) {
        if (bestWalls_[i]) result.walls.push_back(fromIdx(i));
    }
    return result;
}
