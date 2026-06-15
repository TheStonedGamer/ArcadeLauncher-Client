// SortSelfCheckMain.cpp — headless KAT for the portable library sort ordering
// (Linux port L3d-c). Locks gamesort::less against the Windows App.cpp sort
// semantics: Title asc; Platform by name then title; Rating/Playtime/Recent
// desc then title; every mode tie-breaks title then id. Exit 0 on success.

#include "GameSort.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int g_fail = 0;

// Sort a copy under `mode` and return the concatenated ids, e.g. "abch".
std::string order(std::vector<gamesort::Key> v, gamesort::Mode mode) {
    std::sort(v.begin(), v.end(), [&](const gamesort::Key& a, const gamesort::Key& b) {
        return gamesort::less(a, b, mode);
    });
    std::string s;
    for (const auto& k : v) s += k.id;
    return s;
}

void eq(const char* what, const std::string& got, const std::string& want) {
    if (got != want) {
        std::printf("  FAIL %s: got \"%s\" want \"%s\"\n", what, got.c_str(),
                    want.c_str());
        ++g_fail;
    }
}
} // namespace

int main() {
    std::vector<gamesort::Key> v;
    // Key order is {title, id, platform, rating, playtime, lastPlayed}.
    v.push_back({"Celeste", "c", "PC",   90, 100, 5});
    v.push_back({"Hades",   "h", "PC",   95, 300, 10});
    v.push_back({"Apex",    "a", "Xbox", 80, 300, 2});   // same title as 'b'
    v.push_back({"Apex",    "b", "Xbox", 80, 50,  1});   // id tie-breaks after 'a'

    eq("title",    order(v, gamesort::Mode::Title),    "abch");  // Apex(a),Apex(b),Celeste,Hades
    eq("platform", order(v, gamesort::Mode::Platform), "chab");  // PC:Celeste,Hades; Xbox:Apex a,b
    eq("rating",   order(v, gamesort::Mode::Rating),   "hcab");  // 95,90,80(a),80(b)
    eq("playtime", order(v, gamesort::Mode::Playtime), "ahcb");  // 300:Apex,Hades;100;50
    eq("recent",   order(v, gamesort::Mode::Recent),   "hcab");  // 10,5,2,1

    // modeFromIndex maps the Windows 0..4 cycle.
    if (gamesort::modeFromIndex(0) != gamesort::Mode::Title ||
        gamesort::modeFromIndex(3) != gamesort::Mode::Playtime ||
        gamesort::modeFromIndex(99) != gamesort::Mode::Title) {
        std::printf("  FAIL modeFromIndex mapping\n");
        ++g_fail;
    }

    if (g_fail == 0)
        std::printf("sort self-check: OK — all sort KATs passed\n");
    else
        std::printf("sort self-check: FAILED (%d)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
