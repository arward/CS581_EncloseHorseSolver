#include "grid.hpp"
#include <sstream>
#include <iostream>
#include <algorithm>

Grid::Grid(const std::string& mapStr) {
    std::istringstream iss(mapStr);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty()) {
            grid_.push_back(line);
        }
    }
    rows_ = grid_.size();
    cols_ = 0;
    for (auto& row : grid_) {
        cols_ = std::max(cols_, (int)row.size());
    }
    // pad shorter rows with water
    for (auto& row : grid_) {
        while ((int)row.size() < cols_) row += '~';
    }

    horse_ = {-1, -1};
    for (int r = 0; r < rows_; r++) {
        for (int c = 0; c < cols_; c++) {
            char ch = grid_[r][c];
            if (ch == 'H') {
                horse_ = {r, c};
            } else if (ch >= '0' && ch <= '9') {
                portals_[ch].push_back({r, c});
            }
        }
    }
}

// create tiles from ASCII
Tile Grid::tile(int r, int c) const {
    char ch = grid_[r][c];
    switch (ch) {
        case '~': return Tile::WATER;
        case '.': return Tile::GRASS;
        case 'H': return Tile::HORSE;
        case 'C': return Tile::CHERRY;
        case 'G': return Tile::GOLDEN;
        case 'S': return Tile::BEE;
        default:
            if (ch >= '0' && ch <= '9') return Tile::PORTAL;
            return Tile::GRASS;
    }
}

// just a few utility functions
bool Grid::inBounds(int r, int c) const {
    return r >= 0 && r < rows_ && c >= 0 && c < cols_;
}

bool Grid::isBorder(int r, int c) const {
    return r == 0 || r == rows_ - 1 || c == 0 || c == cols_ - 1;
}

bool Grid::isPassable(int r, int c) const {
    if (!inBounds(r, c)) return false;
    return grid_[r][c] != '~';
}

int Grid::tileScore(int r, int c) const {
    Tile t = tile(r, c);
    switch (t) {
        case Tile::GRASS:  return 1;
        case Tile::HORSE:  return 1;
        case Tile::CHERRY: return 4;
        case Tile::GOLDEN: return 11;
        case Tile::BEE:    return -4;
        case Tile::PORTAL: return 1;
        case Tile::WATER:  return 0;
    }
    return 0;
}

Pos Grid::portalPair(int r, int c) const {
    char ch = grid_[r][c];
    auto it = portals_.find(ch);
    if (it == portals_.end()) return {-1, -1};
    for (auto& p : it->second) {
        if (!(p.r == r && p.c == c)) return p;
    }
    return {-1, -1};
}

bool Grid::isPortal(int r, int c) const {
    char ch = grid_[r][c];
    return ch >= '0' && ch <= '9';
}

void Grid::print(const std::vector<Pos>& walls) const {
    // copy grid, mark all walls with hashtag
    auto display = grid_;
    for (auto& w : walls) {
        display[w.r][w.c] = '#';
    }
    for (auto& row : display) {
        std::cout << row << "\n";
    }
}

void Grid::printColored(const std::vector<Pos>& walls, const std::vector<Pos>& enclosed) const {
    // build the lookup sets
    std::vector<bool> isWall(rows_ * cols_, false);
    std::vector<bool> isEnclosed(rows_ * cols_, false);
    for (auto& w : walls)    isWall[w.r * cols_ + w.c] = true;
    for (auto& e : enclosed) isEnclosed[e.r * cols_ + e.c] = true;

    for (int r = 0; r < rows_; r++) {
        for (int c = 0; c < cols_; c++) {
            char ch = grid_[r][c];
            int idx = r * cols_ + c;
            bool enc = isEnclosed[idx];
            bool wall = isWall[idx];

            // bg: bright-green for enclosed, default otherwise
            const char* bg = enc ? "\033[102m" : "";
            
            // HIGHKEY did not know how to do ANY of this
            if (wall) {
                // Bold white on red background
                std::cout << "\033[1;37;41m#\033[0m";
            } else if (ch == '~') {
                // Water: bright blue
                std::cout << "\033[94m~\033[0m";
            } else if (ch == 'H') {
                // Horse: bold yellow
                std::cout << bg << "\033[1;33mH\033[0m";
            } else if (ch == 'C') {
                // Cherry: bold magenta
                std::cout << bg << "\033[1;35mC\033[0m";
            } else if (ch == 'G') {
                // Golden: bold bright yellow
                std::cout << bg << "\033[1;93mG\033[0m";
            } else if (ch == 'S') {
                // Bee (bad): bold red
                std::cout << bg << "\033[1;31mS\033[0m";
            } else if (ch >= '0' && ch <= '9') {
                // Portal: bold cyan
                std::cout << bg << "\033[1;36m" << ch << "\033[0m";
            } else if (ch == '.') {
                if (enc) {
                    // Enclosed grass: green fg on green bg
                    std::cout << "\033[32;102m.\033[0m";
                } else {
                    // Outside grass: dim
                    std::cout << "\033[2m.\033[0m";
                }
            } else {
                std::cout << ch;
            }
        }
        std::cout << "\n";
    }
}
