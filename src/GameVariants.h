#pragma once
// GameVariants.h — portable (UTF-8) ROM-dump variant logic (Linux port L3d-c).
//
// Different dumps of the same game (Crystalis (U), [a1], [a2], (Prototype),
// SMB3 (PRG 1) [a2], …) clean down to an identical display title. The grid
// collapses them under one tile and offers a version picker. The rules that
// decide the filename stem, the grouping key, a human label, and the default
// pick score live here so BOTH the Windows GameLibrary and the Linux app share
// one implementation.
//
// This is the first slice of GameLibrary's std::wstring → UTF-8 migration: the
// Windows Game::Variant* methods become thin wrappers that narrow/widen around
// these functions (the dump tags are pure ASCII, so the behavior is identical).
// No Win32/Direct2D here, so it links into arcade_core on both platforms and is
// covered by variants_selfcheck.

#include <string>

namespace variants {

// The filename leaf of contentPath without its extension; falls back to
// titleFallback when contentPath has no usable stem. Mirrors
// Game::VariantFileStem.
std::string fileStem(const std::string& contentPath,
                     const std::string& titleFallback);

// Games sharing a key are dumps of the same logical game. The cleaned title is
// already identical across dumps, so platformId + lowercased title suffices.
// Mirrors Game::VariantKey.
std::string key(int platformId, const std::string& title);

// Short human label distinguishing one dump from its siblings, derived from the
// tags in the filename (e.g. "Verified", "Alt 1", "Prototype", "PRG 1",
// "Eng patch"). Empty for a plain base dump. Mirrors Game::VariantLabel.
std::string label(const std::string& contentPath,
                  const std::string& titleFallback);

// Lower score = better default pick for the grouped tile. Prefers verified good
// dumps and clean base dumps; demotes alternates, prototypes, bad/hack dumps.
// An installed server copy wins outright. Mirrors Game::VariantScore.
int score(const std::string& contentPath, const std::string& titleFallback,
          bool installedServerCopy);

} // namespace variants
