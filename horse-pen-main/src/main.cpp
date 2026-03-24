#include <iostream>
#include <string>
#include <regex>
#include <ctime>
#include <queue>
#include <vector>
#include <curl/curl.h>
#include "grid.hpp"
#include "solver.hpp"

using namespace std;

static size_t writeCallback(void* contents, size_t size, size_t nmemb, string* output) {
    size_t totalSize = size * nmemb;
    output->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

string fetchURL(const string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw runtime_error("Failed to initialize curl");
    }

    string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw runtime_error(string("curl request failed: ") + curl_easy_strerror(res));
    }

    return response;
}

string getTodayDate() {
    time_t now = time(nullptr);
    tm* t = localtime(&now);
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", t);
    return string(buf);
}

string unescapeNewlines(const string& raw) {
    string result;
    for (size_t i = 0; i < raw.size(); i++) {
        if (i + 1 < raw.size() && raw[i] == '\\' && raw[i + 1] == 'n') {
            result += '\n';
            i++;
        } else {
            result += raw[i];
        }
    }
    return result;
}

string extractField(const string& html, const string& field) {
    // regex wasn't working, use "field":"value" pattern
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
    while (pos < html.size() && html[pos] >= '0' && html[pos] <= '9') {
        num += html[pos++];
    }
    return num.empty() ? -1 : stoi(num);
}

int main(int argc, char* argv[]) {
    string date = argc > 1 ? argv[1] : getTodayDate();
    string url = "https://enclose.horse/play/" + date;

    cout << "Fetching puzzle for " << date << "..." << endl;

    string html = fetchURL(url);

    string mapRaw = extractField(html, "map");
    if (mapRaw.empty()) {
        cerr << "Could not find map data in page" << endl;
        return 1;
    }
    string mapStr = unescapeNewlines(mapRaw);
    int budget = extractIntField(html, "budget");
    int optimal = extractIntField(html, "optimalScore");
    string name = extractField(html, "name");

    cout << "Puzzle: " << name << endl;
    cout << "Budget: " << budget << " walls" << endl;
    cout << "Optimal score: " << optimal << endl;
    cout << endl;

    Grid grid(mapStr);
    cout << "Map (" << grid.rows() << "x" << grid.cols() << "):" << endl;
    grid.printColored({}, {});
    cout << endl;

    Solver solver(grid, budget);
    cout << "Solving..." << endl;
    SolveResult result = solver.solve();

    cout << endl;
    cout << "=== Solution ===" << endl;
    cout << "Score: " << result.score;
    if (optimal > 0) {
        cout << " / " << optimal << " optimal";
        if (result.score == optimal) cout << " [OPTIMAL!]";
    }
    cout << endl;
    cout << "Walls placed: " << result.walls.size() << " / " << budget << endl;

    cout << "\nWall positions:" << endl;
    for (auto& w : result.walls) {
        cout << "  (" << w.r << ", " << w.c << ")" << endl;
    }

    // BFS from horse to find enclosed region (respects walls and portals)
    int rows = grid.rows(), cols = grid.cols();
    static const int DR[] = {-1, 1, 0, 0};
    static const int DC[] = {0, 0, -1, 1};

    vector<bool> isWallVec(rows * cols, false);
    for (auto& w : result.walls) isWallVec[w.r * cols + w.c] = true;

    vector<bool> visited(rows * cols, false);
    vector<Pos> enclosed;
    queue<Pos> q;
    Pos horse = grid.horse();
    if (horse.r >= 0) {
        visited[horse.r * cols + horse.c] = true;
        q.push(horse);
    }
    while (!q.empty()) {
        Pos cur = q.front(); q.pop();
        enclosed.push_back(cur);
        for (int d = 0; d < 4; d++) {
            int nr = cur.r + DR[d], nc = cur.c + DC[d];
            if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
            int ni = nr * cols + nc;
            if (!grid.isPassable(nr, nc) || isWallVec[ni] || visited[ni]) continue;
            visited[ni] = true;
            q.push({nr, nc});
        }
        if (grid.isPortal(cur.r, cur.c)) {
            Pos pair = grid.portalPair(cur.r, cur.c);
            if (pair.r >= 0 && grid.isPassable(pair.r, pair.c)) {
                int pi = pair.r * cols + pair.c;
                if (!isWallVec[pi] && !visited[pi]) {
                    visited[pi] = true;
                    q.push(pair);
                }
            }
        }
    }

    cout << "\nSolution map:" << endl;
    grid.printColored(result.walls, enclosed);

    cout << "\nLegend: "
         << "\033[94m~\033[0m" << " water  "
         << "\033[32;102m.\033[0m" << " enclosed  "
         << "\033[2m.\033[0m" << " outside  "
         << "\033[1;37;41m#\033[0m" << " wall  "
         << "\033[1;33mH\033[0m" << " horse  "
         << "\033[1;35mC\033[0m" << " cherry(+3)  "
         << "\033[1;93mG\033[0m" << " golden(+10)  "
         << "\033[1;31mS\033[0m" << " bee(-5)  "
         << "\033[1;36m0\033[0m" << " portal"
         << endl;

    return 0;
}
