# horse-pen

ILP solver for the [Enclose Horse](https://enclose.horse) daily puzzle.

Solves to **proven optimality** using Gurobi's MIP solver via its C++ API.

## Requirements

- CMake ≥ 3.20
- C++17 compiler (GCC / Clang / MSVC)
- [Gurobi](https://www.gurobi.com) ≥ 10 with a valid licence
- libcurl

## Build

```bash
mkdir build && cd build

# If $GUROBI_HOME is already set in your environment:
cmake -DCMAKE_BUILD_TYPE=Release ..

# Or point CMake at Gurobi explicitly:
cmake -DCMAKE_BUILD_TYPE=Release \
      -DGUROBI_HOME=/opt/gurobi1100/linux64 ..

cmake --build . -j
```

The binary lands at `build/enclosed`.

### macOS (Homebrew curl)

```bash
cmake -DCMAKE_BUILD_TYPE=Release \
      -DGUROBI_HOME=/Library/gurobi1100/macos_universal2 \
      -DCURL_ROOT=$(brew --prefix curl) ..
```

### Windows (MSVC)

```powershell
cmake -DCMAKE_BUILD_TYPE=Release `
      -DGUROBI_HOME="C:\gurobi1100\win64" ..
cmake --build . --config Release
```

## Usage

```bash
# Today's puzzle
./build/enclosed

# A specific date
./build/enclosed 2026-02-08
```

## ILP Formulation

Binary variables per grid tile `i`:
- `x[i]` — tile `i` is enclosed (inside the horse's region)
- `w[i]` — tile `i` has a wall placed on it

**Objective:** minimise `∑ -score[i] · x[i]`  (i.e. maximise enclosed score)

**Constraints:**

| # | Constraint | Purpose |
|---|-----------|---------|
| 1 | `x[horse] = 1` | Horse must be enclosed |
| 2 | `x[i] = 0` for border tiles | Horse can't reach the edge |
| 3 | `w[i] = 0` for non-grass | Walls only on grass |
| 4 | `x[i] + w[i] ≤ 1` | Tile can't be enclosed *and* walled |
| 5 | `x[i] ≤ x[j] + w[j]` for each edge `(i,j)` | **Connectivity** — every neighbour of an enclosed tile must be enclosed or walled off |
| 6 | `∑ w[i] ≤ budget` | Wall budget |

Constraint 5 is the key insight: it propagates enclosure across the entire
reachable graph and guarantees no path from the horse to the border exists
through non-wall tiles. Portals are handled by adding the portal pair as an
extra adjacency edge, so constraint 5 handles them automatically.

## Tile scores

| Tile | Symbol | Score |
|------|--------|-------|
| Grass | `.` | +1 |
| Horse | `H` | +1 |
| Cherry | `C` | +4 |
| Golden apple | `G` | +11 |
| Bee | `S` | −4 |
| Portal | `0–9` | +1 |
| Water | `~` | impassable |
