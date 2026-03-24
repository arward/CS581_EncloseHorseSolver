#pragma once

#include "grid.hpp"
#include <vector>
#include <queue>
#include <random>
#include <chrono>

struct SolveResult {
    std::vector<Pos> walls;
    int score;
};

// always gotta use a class
class Solver {
public:
    Solver(const Grid& grid, int budget);
    SolveResult solve();

private:
    int idx(int r, int c) const { return r * cols_ + c; }
    Pos fromIdx(int i) const { return {i / cols_, i % cols_}; }
    // bfs eval with generation count, return {escaped, score}
    std::pair<bool, int> evaluate(const std::vector<char>& isWall) const;
    // bfs eval returning {borderCount, score}
    std::pair<int, int> evaluateFull(const std::vector<char>& isWall) const;

    // greedy region growing with randonness
    // first run should be deterministic, or nearly so
    std::pair<std::vector<char>, int> greedyExpand(std::mt19937& rng, double randomness);

    // reverse greedy: block escape paths to find large enclosures
    std::pair<std::vector<char>, int> reverseGreedy(std::mt19937& rng, double randomness);

    // 1-swap or 2-swap
    void localSearch(std::vector<char>& isWall, int& currentScore);

    // ONLY 1-swap to save time
    void fastLocalSearch(std::vector<char>& isWall, int& currentScore);

    // fix an invalid config by replacing walls
    bool repairWalls(std::vector<char>& isWall, std::mt19937& rng);

    // simulated annealing my GOAT
    void anneal(std::vector<char>& isWall, int& currentScore, std::mt19937& rng,
                double startTemp, double endTemp, int maxIter);

    void updateBest(const std::vector<char>& walls, int score);

    bool timeUp() const;

    const Grid& grid_;
    int budget_;
    int bestScore_;
    int n_, rows_, cols_;
    int horseIdx_;

    std::vector<bool> passable_;
    std::vector<bool> border_;
    std::vector<bool> wallable_; // can a wall be placed here? (grass only)
    std::vector<int> tileScore_;
    std::vector<int> portalPair_;
    // precomp adjacency lists
    std::vector<std::vector<int>> adj_;

    std::vector<char> bestWalls_;

    mutable std::vector<int> visited_gen_;
    mutable int cur_gen_;

    std::chrono::steady_clock::time_point deadline_;
};
