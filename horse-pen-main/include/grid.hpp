#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

enum class Tile { WATER, GRASS, HORSE, CHERRY, GOLDEN, BEE, PORTAL };

struct Pos {
    int r, c;
    bool operator==(const Pos& o) const { return r == o.r && c == o.c; }
};

struct PosHash {
    size_t operator()(const Pos& p) const {
        return std::hash<int>()(p.r) ^ (std::hash<int>()(p.c) << 16);
    }
};

class Grid {
public:
    Grid(const std::string& mapStr);

    int rows() const { return rows_; }
    int cols() const { return cols_; }
    char at(int r, int c) const { return grid_[r][c]; }
    Tile tile(int r, int c) const;

    bool inBounds(int r, int c) const;
    bool isBorder(int r, int c) const;
    // not water
    bool isPassable(int r, int c) const;

    // how much a single tile adds to the score
    int tileScore(int r, int c) const;

    Pos horse() const { return horse_; }

    // return portal pair pos or {-1,-1} if none xists
    Pos portalPair(int r, int c) const;
    bool isPortal(int r, int c) const;

    void print(const std::vector<Pos>& walls = {}) const;
    void printColored(const std::vector<Pos>& walls, const std::vector<Pos>& enclosed) const;

private:
    int rows_, cols_;
    std::vector<std::string> grid_;
    Pos horse_;
    // portal char -> list of positions (each char has exactly 2)
    std::unordered_map<char, std::vector<Pos>> portals_;
};
