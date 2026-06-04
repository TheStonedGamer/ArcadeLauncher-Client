#pragma once

struct IgdbPlatformInfo { int id; const wchar_t* name; };

// Ordered from most-common to least-common so the combobox reads naturally.
// id=0 is the sentinel meaning "auto-detect from library type".
inline const IgdbPlatformInfo kIgdbPlatforms[] = {
    {   0, L"Auto (default for library type)"  },
    // PC
    {   6, L"PC (Windows)"                     },
    {  14, L"macOS"                             },
    {   3, L"Linux"                             },
    {  13, L"DOS"                               },
    // Modern consoles
    { 167, L"PlayStation 5"                     },
    { 169, L"Xbox Series X|S"                   },
    { 130, L"Nintendo Switch"                   },
    {  48, L"PlayStation 4"                     },
    {  49, L"Xbox One"                          },
    {  41, L"Wii U"                             },
    // 7th gen
    {   9, L"PlayStation 3"                     },
    {  12, L"Xbox 360"                          },
    {   5, L"Wii"                               },
    // 6th gen
    {   8, L"PlayStation 2"                     },
    {  11, L"Xbox"                              },
    {  21, L"GameCube"                          },
    {  23, L"Dreamcast"                         },
    // 5th gen
    {   4, L"Nintendo 64"                       },
    {   7, L"PlayStation"                       },
    {  32, L"Sega Saturn"                       },
    // 4th gen
    {  19, L"SNES"                              },
    {  29, L"Sega Genesis / Mega Drive"         },
    // 3rd gen
    {  18, L"NES"                               },
    // Handhelds
    {  46, L"PlayStation Vita"                  },
    {  38, L"PSP"                               },
    {  37, L"Nintendo 3DS"                      },
    {  20, L"Nintendo DS"                       },
    {  24, L"Game Boy Advance"                  },
    {  22, L"Game Boy Color"                    },
    {  33, L"Game Boy"                          },
    // Other
    {  52, L"Arcade"                            },
    {  16, L"Amiga"                             },
    {  59, L"Atari 2600"                        },
};
inline constexpr int kIgdbPlatformCount =
    (int)(sizeof(kIgdbPlatforms) / sizeof(kIgdbPlatforms[0]));

// Returns the display name for a given IGDB platform id, or nullptr if unknown.
inline const wchar_t* IgdbPlatformName(int id) {
    for (auto& p : kIgdbPlatforms)
        if (p.id == id) return p.name;
    return nullptr;
}
