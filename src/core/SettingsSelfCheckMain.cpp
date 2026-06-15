// SettingsSelfCheckMain.cpp — headless KAT for the portable General settings
// model (Linux port L4-g). Locks settings::: parseGeneral reads the behavior
// toggles out of a launcher config JSON; applyGeneral rewrites only those keys
// in-place (preserving every other key byte-for-byte); buildGeneralPanel binds a
// forms::Panel to the model and readGeneralPanel reads it back. Exit 0 on
// success. Pure logic — no file I/O, no display.

#include "Settings.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace {
int g_fail = 0;
void ck(const char* what, bool cond) {
    if (!cond) { std::printf("  FAIL %s\n", what); ++g_fail; }
}

// A representative slice of a real launcher config: the General keys interleaved
// with unrelated keys that applyGeneral must not disturb.
const char* kConfig =
    "{\n"
    "  \"firstLaunchDone\":true,\n"
    "  \"startFullscreen\":false,\n"
    "  \"minimizeOnLaunch\":true,\n"
    "  \"defenderExclusions\":false,\n"
    "  \"windowWidth\":1280,\n"
    "  \"windowHeight\":720,\n"
    "  \"steamGridDbApiKey\":\"abc123\",\n"
    "  \"discordRichPresence\":true,\n"
    "  \"discordClientId\":\"1515119921795960882\",\n"
    "  \"downloadLimitKBps\":0,\n"
    "  \"steamPath\":\"D:\\\\Steam\"\n"
    "}\n";
} // namespace

int main() {
    const std::string cfg = kConfig;

    // 1. parseGeneral reads the toggles as stored.
    {
        settings::General g = settings::parseGeneral(cfg);
        ck("parse startFullscreen=false",   g.startFullscreen == false);
        ck("parse minimizeOnLaunch=true",   g.minimizeOnLaunch == true);
        ck("parse defenderExclusions=false",g.defenderExclusions == false);
        ck("parse discordRichPresence=true",g.discordRichPresence == true);
        ck("parse downloadLimitKBps=0",     g.downloadLimitKBps == 0);
    }

    // 2. Missing keys fall back to defaults (config predating a field).
    {
        settings::General g = settings::parseGeneral("{\n  \"windowWidth\":800\n}\n");
        ck("default minimizeOnLaunch=true",   g.minimizeOnLaunch == true);
        ck("default discordRichPresence=true",g.discordRichPresence == true);
        ck("default startFullscreen=false",   g.startFullscreen == false);
        ck("default downloadLimitKBps=0",     g.downloadLimitKBps == 0);
    }

    // 3. applyGeneral rewrites only the General keys; re-parsing yields the new
    //    values, and unrelated keys are preserved byte-for-byte.
    {
        settings::General g;
        g.startFullscreen = true;
        g.minimizeOnLaunch = false;
        g.defenderExclusions = true;
        g.discordRichPresence = false;
        g.downloadLimitKBps = 2048;

        std::string out = settings::applyGeneral(cfg, g);
        settings::General rt = settings::parseGeneral(out);
        ck("round-trip startFullscreen",    rt.startFullscreen == true);
        ck("round-trip minimizeOnLaunch",   rt.minimizeOnLaunch == false);
        ck("round-trip defenderExclusions", rt.defenderExclusions == true);
        ck("round-trip discordRichPresence",rt.discordRichPresence == false);
        ck("round-trip downloadLimitKBps",  rt.downloadLimitKBps == 2048);

        // Unrelated keys untouched.
        ck("preserved firstLaunchDone",
           out.find("\"firstLaunchDone\":true") != std::string::npos);
        ck("preserved windowWidth",
           out.find("\"windowWidth\":1280") != std::string::npos);
        ck("preserved steamGridDbApiKey",
           out.find("\"steamGridDbApiKey\":\"abc123\"") != std::string::npos);
        ck("preserved discordClientId",
           out.find("\"discordClientId\":\"1515119921795960882\"") != std::string::npos);
        ck("preserved steamPath",
           out.find("\"steamPath\":\"D:\\\\Steam\"") != std::string::npos);
    }

    // 4. A key absent from the JSON stays absent (applyGeneral edits only what's
    //    there — it never grows the schema the portable model doesn't own).
    {
        std::string mini = "{\n  \"startFullscreen\":false\n}\n";
        settings::General g; g.startFullscreen = true; g.minimizeOnLaunch = false;
        std::string out = settings::applyGeneral(mini, g);
        ck("absent key not added", out.find("minimizeOnLaunch") == std::string::npos);
        ck("present key updated",  out.find("\"startFullscreen\":true") != std::string::npos);
    }

    // 5. buildGeneralPanel reflects the model; readGeneralPanel reads it back.
    {
        settings::General g;
        g.startFullscreen = true; g.defenderExclusions = true;
        g.discordRichPresence = false; g.downloadLimitKBps = 512;
        forms::Panel p = settings::buildGeneralPanel(g, {100, 80, 720, 320});

        ck("panel has 5 rows", p.fields.size() == 5);
        ck("panel startFullscreen checked",  forms::boolValue(p, "startFullscreen"));
        ck("panel discordRichPresence off", !forms::boolValue(p, "discordRichPresence"));
        ck("panel limit text seeded", forms::textValue(p, "downloadLimitKBps") == "512");

        // Toggle a checkbox via the model and read back.
        forms::find(p, "minimizeOnLaunch")->checkbox.checked = true;
        forms::find(p, "startFullscreen")->checkbox.checked = false;
        settings::General back = settings::readGeneralPanel(p);
        ck("readback startFullscreen cleared", back.startFullscreen == false);
        ck("readback minimizeOnLaunch set",    back.minimizeOnLaunch == true);
        ck("readback defenderExclusions kept", back.defenderExclusions == true);
        ck("readback limit parsed",            back.downloadLimitKBps == 512);
    }

    // 6. Non-numeric / empty download-limit text clamps to 0.
    {
        settings::General g; g.downloadLimitKBps = 999;
        forms::Panel p = settings::buildGeneralPanel(g, {0, 0, 600, 300});
        forms::find(p, "downloadLimitKBps")->textedit.text = "abc";
        ck("non-numeric limit -> 0", settings::readGeneralPanel(p).downloadLimitKBps == 0);
        forms::find(p, "downloadLimitKBps")->textedit.text = "";
        ck("empty limit -> 0", settings::readGeneralPanel(p).downloadLimitKBps == 0);
    }

    // 7. Real temp-file round-trip: write a config, load it, modify, save it
    //    back, and re-read the raw text — the General keys update while every
    //    other key survives, and a missing file is left untouched.
    {
        const std::string path = "settings_selfcheck_tmp.json";
        { std::ofstream f(path, std::ios::binary | std::ios::trunc); f << cfg; }

        settings::General g;
        ck("load from file", settings::loadGeneralFromFile(path, g));
        ck("loaded minimizeOnLaunch", g.minimizeOnLaunch == true);

        g.startFullscreen = true;
        g.downloadLimitKBps = 4096;
        ck("save to file", settings::saveGeneralToFile(path, g));

        std::string after;
        {   // close the handle before std::remove (Windows won't delete an open file)
            std::ifstream rf(path, std::ios::binary);
            after.assign((std::istreambuf_iterator<char>(rf)),
                         std::istreambuf_iterator<char>());
        }
        ck("file startFullscreen updated",
           after.find("\"startFullscreen\":true") != std::string::npos);
        ck("file downloadLimit updated",
           after.find("\"downloadLimitKBps\":4096") != std::string::npos);
        ck("file steamPath preserved",
           after.find("\"steamPath\":\"D:\\\\Steam\"") != std::string::npos);
        ck("file steamGridDbApiKey preserved",
           after.find("\"steamGridDbApiKey\":\"abc123\"") != std::string::npos);

        std::remove(path.c_str());
        // Saving to a now-missing file must NOT create one (schema is owned
        // elsewhere) and must report failure.
        ck("save to missing file fails", !settings::saveGeneralToFile(path, g));
        std::ifstream gone(path);
        ck("missing file not created", !gone.good());
    }

    if (g_fail == 0)
        std::printf("settings self-check: OK — parse + non-destructive apply + "
                    "panel bind/readback + file round-trip KATs passed\n");
    else
        std::printf("settings self-check: FAILED (%d)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
