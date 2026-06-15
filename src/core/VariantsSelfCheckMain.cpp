// VariantsSelfCheckMain.cpp — headless KAT for the portable ROM-dump variant
// logic (Linux port L3d-c). Locks variants::fileStem/key/label/score against
// hand-computed expectations over real ROM filenames, so the Windows Game::
// Variant* wrappers and the Linux grouping share one verified implementation.
// Exit 0 on success.

#include "GameVariants.h"

#include <cstdio>
#include <string>

namespace {
int g_fail = 0;

void eqS(const char* what, const std::string& got, const std::string& want) {
    if (got != want) {
        std::printf("  FAIL %s: got \"%s\" want \"%s\"\n", what, got.c_str(),
                    want.c_str());
        ++g_fail;
    }
}
void eqI(const char* what, int got, int want) {
    if (got != want) {
        std::printf("  FAIL %s: got %d want %d\n", what, got, want);
        ++g_fail;
    }
}

// Platform ids match GameLibrary.h's enum order (NES=9, SNES=10, PS1=11).
constexpr int NES = 9;
} // namespace

int main() {
    // Crystalis alt dump.
    {
        const std::string cp = "games/Nintendo/NES/Crystalis (U) [a1].nes";
        eqS("crystalis.stem", variants::fileStem(cp, "Crystalis"),
            "Crystalis (U) [a1]");
        eqS("crystalis.key", variants::key(NES, "Crystalis"), "9|crystalis");
        eqS("crystalis.label", variants::label(cp, "Crystalis"), "Alt 1");
        eqI("crystalis.score", variants::score(cp, "Crystalis", false), 110);
    }

    // Verified good base dump.
    {
        const std::string cp = "Crystalis (U) [!].nes";
        eqS("verified.label", variants::label(cp, "Crystalis"), "Verified");
        eqI("verified.score", variants::score(cp, "Crystalis", false), 45);
    }

    // PRG-1 alternate.
    {
        const std::string cp = "Super Mario Bros 3 (PRG 1) [a2].nes";
        eqS("smb3.key", variants::key(NES, "Super Mario Bros 3"),
            "9|super mario bros 3");
        eqS("smb3.label", variants::label(cp, "Super Mario Bros 3"),
            "PRG 1, Alt 2");
        eqI("smb3.score", variants::score(cp, "Super Mario Bros 3", false), 115);
    }

    // Prototype.
    {
        const std::string cp = "Some Game (Prototype).nes";
        eqS("proto.label", variants::label(cp, "Some Game"), "Prototype");
        eqI("proto.score", variants::score(cp, "Some Game", false), 140);
    }

    // Bad dump + hack.
    {
        const std::string cp = "Game [b1][h1].nes";
        eqS("badhack.label", variants::label(cp, "Game"), "Bad dump, Hack");
        eqI("badhack.score", variants::score(cp, "Game", false), 190);
    }

    // Installed server copy wins outright (a PC exe, no ROM tags).
    {
        const std::string cp = "games/PC/Cool Game/cool.exe";
        eqS("installed.stem", variants::fileStem(cp, "Cool Game"), "cool");
        eqI("installed.score", variants::score(cp, "Cool Game", true), -900);
    }

    // Empty contentPath falls back to the title; no tags → plain base dump.
    {
        eqS("fallback.stem", variants::fileStem("", "My Game"), "My Game");
        eqS("fallback.label", variants::label("", "My Game"), "");
        eqI("fallback.score", variants::score("", "My Game", false), 100);
    }

    if (g_fail == 0)
        std::printf("variants self-check: OK — all variant KATs passed\n");
    else
        std::printf("variants self-check: FAILED (%d)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
