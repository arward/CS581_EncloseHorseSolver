#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>
#include <queue>
#include <vector>
#include <map>
#include <iomanip>
#include <curl/curl.h>
#include "grid.hpp"
#include "solver.hpp"

using namespace std;

// ── HTTP fetch ────────────────────────────────────────────────────────────────

static size_t writeCallback(void* contents, size_t size, size_t nmemb, string* output) {
    output->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

string fetchURL(const string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) throw runtime_error("Failed to initialize curl");

    string response;
    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,      "horse-pen-solver/3.0");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        throw runtime_error(string("curl: ") + curl_easy_strerror(res));

    return response;
}

// ── Puzzle parsing ────────────────────────────────────────────────────────────

string getTodayDate() {
    time_t now = time(nullptr);
    tm* t = localtime(&now);
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", t);
    return buf;
}

string unescapeNewlines(const string& raw) {
    string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); i++) {
        if (i + 1 < raw.size() && raw[i] == '\\' && raw[i+1] == 'n') {
            out += '\n'; i++;
        } else {
            out += raw[i];
        }
    }
    return out;
}

string extractField(const string& html, const string& field) {
    string key = "\"" + field + "\"";
    size_t pos = html.find(key);
    if (pos == string::npos) return "";
    pos = html.find(':', pos + key.size());
    if (pos == string::npos) return "";
    pos = html.find('"', pos + 1);
    if (pos == string::npos) return "";
    size_t end = html.find('"', pos + 1);
    if (end == string::npos) return "";
    return html.substr(pos + 1, end - pos - 1);
}

int extractIntField(const string& html, const string& field) {
    string key = "\"" + field + "\"";
    size_t pos = html.find(key);
    if (pos == string::npos) return -1;
    pos = html.find(':', pos + key.size());
    if (pos == string::npos) return -1;
    pos++;
    while (pos < html.size() && html[pos] == ' ') pos++;
    string num;
    while (pos < html.size() && html[pos] >= '0' && html[pos] <= '9') num += html[pos++];
    return num.empty() ? -1 : stoi(num);
}

// ── BFS to find enclosed region ───────────────────────────────────────────────

vector<Pos> findEnclosed(const Grid& grid, const vector<Pos>& walls) {
    int rows = grid.rows(), cols = grid.cols();
    static const int DR[] = {-1, 1, 0, 0};
    static const int DC[] = {0, 0, -1, 1};

    vector<bool> isWall(rows * cols, false);
    for (auto& w : walls) isWall[w.r * cols + w.c] = true;

    vector<bool> visited(rows * cols, false);
    vector<Pos>  enclosed;
    queue<Pos>   q;
    Pos horse = grid.horse();
    if (horse.r < 0) return enclosed;

    visited[horse.r * cols + horse.c] = true;
    q.push(horse);

    while (!q.empty()) {
        Pos cur = q.front(); q.pop();
        enclosed.push_back(cur);
        for (int d = 0; d < 4; d++) {
            int nr = cur.r + DR[d], nc = cur.c + DC[d];
            if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
            int ni = nr * cols + nc;
            if (!grid.isPassable(nr, nc) || isWall[ni] || visited[ni]) continue;
            visited[ni] = true;
            q.push({nr, nc});
        }
        if (grid.isPortal(cur.r, cur.c)) {
            Pos pair = grid.portalPair(cur.r, cur.c);
            if (pair.r >= 0 && grid.isPassable(pair.r, pair.c)) {
                int pi = pair.r * cols + pair.c;
                if (!isWall[pi] && !visited[pi]) {
                    visited[pi] = true;
                    q.push(pair);
                }
            }
        }
    }
    return enclosed;
}

// ── Solve one date, return score ──────────────────────────────────────────────

struct PuzzleResult {
    string date;
    string name;
    int    score   = 0;
    int    optimal = -1;
    int    oldScore = -1;  // from CSV, -1 if unknown
    bool   proved  = false;
    bool   ok      = false; // score == optimal
};

PuzzleResult solveDate(const string& date, bool quiet) {
    PuzzleResult pr;
    pr.date = date;

    string url  = "https://enclose.horse/play/" + date;
    string html = fetchURL(url);

    string mapRaw = extractField(html, "map");
    if (mapRaw.empty()) throw runtime_error("could not find map data");

    string mapStr   = unescapeNewlines(mapRaw);
    int    budget   = extractIntField(html, "budget");
    pr.optimal      = extractIntField(html, "optimalScore");
    pr.name         = extractField(html, "name");

    Grid grid(mapStr);

    if (!quiet) {
        cout << "Puzzle:       " << pr.name    << "\n";
        cout << "Budget:       " << budget     << " walls\n";
        cout << "Known optimal:" << pr.optimal << "\n\n";
        cout << "Map (" << grid.rows() << "x" << grid.cols() << "):\n";
        grid.printColored({}, {});
        cout << "\n";
        cout << "Solving with ILP (Gurobi)...\n";
    }

    Solver     solver(grid, budget);
    SolveResult result = solver.solve();

    pr.score  = result.score;
    pr.proved = result.optimal;
    pr.ok     = (pr.optimal > 0 && pr.score == pr.optimal);

    if (!quiet) {
        cout << "\n=== Solution ===\n";

        if (result.score == 0 && result.walls.empty() && !result.optimal) {
            cout << "No solution found.\n";
            return pr;
        }

        cout << "Score: " << result.score;
        if (pr.optimal > 0) {
            cout << " / " << pr.optimal << " optimal";
            if (pr.ok)                    cout << "  [MATCHES KNOWN OPTIMAL]";
            else if (result.score > pr.optimal) cout << "  [!!! EXCEEDS]";
            else                          cout << "  [gap: " << (pr.optimal - result.score) << "]";
        }
        cout << "\n";
        cout << "Walls placed: " << result.walls.size() << " / " << budget << "\n";
        cout << "Proved optimal: " << (result.optimal ? "yes" : "no (time limit)") << "\n";

        cout << "\nWall positions:\n";
        for (auto& w : result.walls)
            cout << "  (" << w.r << ", " << w.c << ")\n";

        auto enclosed = findEnclosed(grid, result.walls);
        cout << "\nSolution map:\n";
        grid.printColored(result.walls, enclosed);

        cout << "\nLegend: "
             << "\033[94m~\033[0m"     << " water  "
             << "\033[32;102m.\033[0m" << " enclosed  "
             << "\033[2m.\033[0m"      << " outside  "
             << "\033[1;37;41m#\033[0m"<< " wall  "
             << "\033[1;33mH\033[0m"   << " horse  "
             << "\033[1;35mC\033[0m"   << " cherry(+3)  "
             << "\033[1;93mG\033[0m"   << " golden(+10)  "
             << "\033[1;31mS\033[0m"   << " bee(-5)  "
             << "\033[1;36m0\033[0m"   << " portal\n";
    }

    return pr;
}

// ── CSV loading ───────────────────────────────────────────────────────────────

// Returns map of date -> old score from results.csv
map<string, int> loadCSV(const string& path) {
    map<string, int> out;
    ifstream f(path);
    if (!f) return out;
    string line;
    getline(f, line); // header
    while (getline(f, line)) {
        istringstream ss(line);
        string date, score;
        getline(ss, date,  ',');
        getline(ss, score, ',');
        if (!date.empty() && !score.empty())
            out[date] = stoi(score);
    }
    return out;
}

// ── Main ──────────────────────────────────────────────────────────────────────

void printUsage() {
    cerr << "Usage:\n"
         << "  enclosed [date]              solve one date (default: today)\n"
         << "  enclosed -q [date]           quiet one-liner output\n"
         << "  enclosed --batch <csv>       solve all dates in CSV, summary table\n"
         << "  enclosed --batch <csv> -q    same but suppress Gurobi output too\n";
}

int main(int argc, char* argv[]) {
    // Parse args
    bool        quiet     = false;
    bool        batch     = false;
    string      csvPath;
    string      date;

    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "-q")       quiet = true;
        else if (a == "--batch") {
            batch = true;
            if (i + 1 < argc) csvPath = argv[++i];
        } else {
            date = a;
        }
    }

    if (batch) {
        // --batch mode: read CSV, solve each date, print summary table
        if (csvPath.empty()) { printUsage(); return 1; }

        auto oldScores = loadCSV(csvPath);
        if (oldScores.empty()) {
            cerr << "ERROR: could not read " << csvPath << "\n";
            return 1;
        }

        // Header
        cout << left
             << setw(12) << "Date"
             << setw(20) << "Puzzle"
             << right
             << setw(6)  << "Score"
             << setw(8)  << "Opt"
             << setw(7)  << "Old"
             << setw(5)  << "Diff"
             << "  Status\n";
        cout << string(65, '-') << "\n";

        int total = 0, perfect = 0, improved = 0;

        for (auto& [d, oldScore] : oldScores) {
            cout << flush;
            try {
                PuzzleResult pr = solveDate(d, true);
                pr.oldScore = oldScore;

                int diff = pr.score - oldScore;
                string status;
                if (pr.ok)          status = "OPTIMAL";
                else if (pr.proved) status = "proved-suboptimal";
                else                status = "time-limit";

                cout << left
                     << setw(12) << d
                     << setw(20) << pr.name.substr(0, 19)
                     << right
                     << setw(6)  << pr.score
                     << setw(8)  << pr.optimal
                     << setw(7)  << oldScore
                     << setw(5)  << (diff > 0 ? "+" + to_string(diff) : (diff < 0 ? to_string(diff) : "="))
                     << "  " << status << "\n";

                total++;
                if (pr.ok)   perfect++;
                if (diff > 0) improved++;

            } catch (exception& e) {
                cout << left << setw(12) << d << "  ERROR: " << e.what() << "\n";
            }
        }

        cout << string(65, '-') << "\n";
        cout << "Optimal: " << perfect << "/" << total
             << "  |  Improved vs old: " << improved << "\n";

        return 0;
    }

    // Single date mode
    if (date.empty()) date = getTodayDate();

    if (quiet) {
        try {
            PuzzleResult pr = solveDate(date, true);
            // one-liner
            cout << date << "  " << pr.score;
            if (pr.optimal > 0) {
                cout << " / " << pr.optimal;
                if (pr.ok) cout << "  OPTIMAL";
                else        cout << "  gap=" << (pr.optimal - pr.score);
            }
            cout << (pr.proved ? "  proved" : "  time-limit") << "\n";
        } catch (exception& e) {
            cerr << "ERROR: " << e.what() << "\n";
            return 1;
        }
    } else {
        cout << "Fetching puzzle for " << date << "...\n";
        try {
            solveDate(date, false);
        } catch (exception& e) {
            cerr << "ERROR: " << e.what() << "\n";
            return 1;
        }
    }

    return 0;
}
