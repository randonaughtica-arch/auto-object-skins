#include "BlockSets.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_map>

using namespace blocksets;

namespace {
    // Object keys used by GD's level format
    constexpr std::string_view KEY_ID = "1";
    constexpr std::string_view KEY_X = "2";
    constexpr std::string_view KEY_Y = "3";
    constexpr std::string_view KEY_FLIP_X = "4";
    constexpr std::string_view KEY_FLIP_Y = "5";
    constexpr std::string_view KEY_ROTATION = "6";
    constexpr std::string_view KEY_SCALE = "32";
    constexpr std::string_view KEY_SCALE_X = "128";
    constexpr std::string_view KEY_SCALE_Y = "129";

    // Properties that tie an object to its original level
    constexpr std::string_view LEVEL_SPECIFIC_KEYS[] = {
        "57",  // groups
        "108", // linked group
        "20",  // editor layer
        "61",  // editor layer 2
    };

    // Properties that change how a block or decoration looks
    constexpr std::string_view LOOK_KEYS[] = {
        "4", "5", "6", "32", "128", "129", "131", "132", // flips, rotation, scale, warp
        "21", "22",                                      // color channels
        "24", "25",                                      // z layer, z order
        "41", "42", "43", "44",                          // HSV
        "64", "65", "96",                                // no glow, etc.
    };

    constexpr float GRID_CELL = 60.f;

    std::vector<std::string_view> split(std::string_view s, char delim) {
        std::vector<std::string_view> parts;
        size_t start = 0;
        while (start <= s.size()) {
            auto end = s.find(delim, start);
            if (end == std::string_view::npos) end = s.size();
            parts.push_back(s.substr(start, end - start));
            start = end + 1;
        }
        return parts;
    }

    float parseFloat(std::string_view s, float def) {
        if (s.empty()) return def;
        // Float from_chars isn't available on every platform's standard library
        std::string str(s);
        char* end = nullptr;
        float value = std::strtof(str.c_str(), &end);
        return end == str.c_str() ? def : value;
    }

    struct Box {
        float minX, minY, maxX, maxY;

        bool contains(float x, float y) const {
            return x >= minX && x <= maxX && y >= minY && y <= maxY;
        }
    };

    float scaleX(LevelObject const& obj) {
        auto uniform = obj.getFloat(KEY_SCALE, 1.f);
        return std::abs(obj.getFloat(KEY_SCALE_X, uniform));
    }

    float scaleY(LevelObject const& obj) {
        auto uniform = obj.getFloat(KEY_SCALE, 1.f);
        return std::abs(obj.getFloat(KEY_SCALE_Y, uniform));
    }

    // Axis aligned box around a (possibly rotated) object
    Box boundsOf(LevelObject const& obj, ObjectInfo const& info, float padding) {
        float w = info.width * scaleX(obj);
        float h = info.height * scaleY(obj);
        float rad = obj.getFloat(KEY_ROTATION) * 3.14159265f / 180.f;
        float c = std::abs(std::cos(rad)), s = std::abs(std::sin(rad));
        float halfW = (w * c + h * s) / 2 + padding;
        float halfH = (w * s + h * c) / 2 + padding;
        return { obj.x - halfW, obj.y - halfH, obj.x + halfW, obj.y + halfH };
    }

    int64_t cellKey(int cx, int cy) {
        return (static_cast<int64_t>(cx) << 32) ^ static_cast<uint32_t>(cy);
    }

    // Rounds to half a unit so tiny float differences don't split identical sets
    std::string roundedOffset(float v) {
        auto r = std::round(v * 2.f) / 2.f;
        if (r == 0.f) r = 0.f; // no "-0"
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", r);
        return buf;
    }

    std::string lookSignature(LevelObject const& obj) {
        std::string sig = std::to_string(obj.id);
        for (auto key : LOOK_KEYS) {
            auto value = obj.get(key);
            if (!value.empty() && value != "0") {
                sig += '|';
                sig += key;
                sig += '=';
                sig += value;
            }
        }
        return sig;
    }
}

std::string_view LevelObject::get(std::string_view key) const {
    for (auto const& [k, v] : props) {
        if (k == key) return v;
    }
    return {};
}

float LevelObject::getFloat(std::string_view key, float def) const {
    return parseFloat(this->get(key), def);
}

std::string LevelObject::toString(bool stripGroups) const {
    std::string out;
    for (auto const& [k, v] : props) {
        if (stripGroups && std::find(std::begin(LEVEL_SPECIFIC_KEYS), std::end(LEVEL_SPECIFIC_KEYS), k) != std::end(LEVEL_SPECIFIC_KEYS)) {
            continue;
        }
        if (!out.empty()) out += ',';
        out += k;
        out += ',';
        out += v;
    }
    return out;
}

std::vector<LevelObject> blocksets::parseLevel(std::string_view levelString) {
    std::vector<LevelObject> objects;
    for (auto part : split(levelString, ';')) {
        // The header is made of "kA..."/"kS..." keys; objects always start with their ID
        if (part.size() < 3 || part.substr(0, 2) != "1,") continue;

        LevelObject obj;
        auto fields = split(part, ',');
        for (size_t i = 0; i + 1 < fields.size(); i += 2) {
            obj.props.emplace_back(std::string(fields[i]), std::string(fields[i + 1]));
        }
        obj.id = static_cast<int>(parseFloat(obj.get(KEY_ID), 0.f));
        obj.x = obj.getFloat(KEY_X);
        obj.y = obj.getFloat(KEY_Y);
        if (obj.id > 0) {
            objects.push_back(std::move(obj));
        }
    }
    return objects;
}

ScanResult blocksets::scan(std::string_view levelString, InfoProvider const& infoFor, Options const& options) {
    ScanResult result;
    result.objects = parseLevel(levelString);
    auto const& objects = result.objects;

    std::unordered_map<int, ObjectInfo> infoCache;
    auto info = [&](int id) -> ObjectInfo const& {
        auto it = infoCache.find(id);
        if (it == infoCache.end()) {
            it = infoCache.emplace(id, infoFor(id)).first;
        }
        return it->second;
    };

    // Spatial grid of base blocks, with each block registered in every cell its padded box touches
    std::vector<size_t> bases;
    std::vector<Box> baseBoxes;
    std::unordered_map<int64_t, std::vector<size_t>> grid;
    for (size_t i = 0; i < objects.size(); i++) {
        auto kind = info(objects[i].id).kind;
        if (kind != Kind::Block && !(options.includeSlopes && kind == Kind::Slope)) continue;

        auto box = boundsOf(objects[i], info(objects[i].id), options.attachDistance);
        // Skip absurdly large bases (huge scaled blocks used as backgrounds)
        if (box.maxX - box.minX > GRID_CELL * 20 || box.maxY - box.minY > GRID_CELL * 20) continue;

        size_t baseIndex = bases.size();
        bases.push_back(i);
        baseBoxes.push_back(box);
        for (int cx = static_cast<int>(std::floor(box.minX / GRID_CELL)); cx <= static_cast<int>(std::floor(box.maxX / GRID_CELL)); cx++) {
            for (int cy = static_cast<int>(std::floor(box.minY / GRID_CELL)); cy <= static_cast<int>(std::floor(box.maxY / GRID_CELL)); cy++) {
                grid[cellKey(cx, cy)].push_back(baseIndex);
            }
        }
    }

    // Attach every decoration to the closest base whose padded box contains it
    std::vector<std::vector<size_t>> attached(bases.size());
    for (size_t i = 0; i < objects.size(); i++) {
        if (info(objects[i].id).kind != Kind::Decoration) continue;
        auto const& deco = objects[i];

        auto cell = grid.find(cellKey(
            static_cast<int>(std::floor(deco.x / GRID_CELL)),
            static_cast<int>(std::floor(deco.y / GRID_CELL))
        ));
        if (cell == grid.end()) continue;

        std::optional<size_t> best;
        float bestDist = 0.f;
        for (auto baseIndex : cell->second) {
            if (!baseBoxes[baseIndex].contains(deco.x, deco.y)) continue;
            auto const& base = objects[bases[baseIndex]];
            float dist = std::hypot(base.x - deco.x, base.y - deco.y);
            if (!best || dist < bestDist) {
                best = baseIndex;
                bestDist = dist;
            }
        }
        if (best) {
            attached[*best].push_back(i);
        }
    }

    // Group bases that look the same and have the same deco in the same places
    struct Group {
        size_t firstBase;
        int count = 0;
    };
    std::map<std::string, Group> groups;
    for (size_t b = 0; b < bases.size(); b++) {
        if (attached[b].empty()) continue;
        result.decoratedBases++;

        auto const& base = objects[bases[b]];
        std::vector<std::string> decoSigs;
        for (auto i : attached[b]) {
            auto const& deco = objects[i];
            decoSigs.push_back(
                roundedOffset(deco.x - base.x) + "," + roundedOffset(deco.y - base.y) + "," + lookSignature(deco)
            );
        }
        std::sort(decoSigs.begin(), decoSigs.end());

        std::string sig = lookSignature(base);
        for (auto const& s : decoSigs) {
            sig += ';';
            sig += s;
        }

        auto [it, inserted] = groups.try_emplace(std::move(sig), Group{ b });
        it->second.count++;
    }

    std::vector<Group> kept;
    for (auto const& [sig, group] : groups) {
        if (group.count >= options.minRepeats) kept.push_back(group);
    }
    std::stable_sort(kept.begin(), kept.end(), [&](Group const& a, Group const& b) {
        if (a.count != b.count) return a.count > b.count;
        return attached[a.firstBase].size() > attached[b.firstBase].size();
    });
    if (kept.size() > options.maxSets) {
        kept.resize(options.maxSets);
    }

    for (auto const& group : kept) {
        BlockSet set;
        set.repeats = group.count;
        set.objects.push_back(bases[group.firstBase]);
        for (auto i : attached[group.firstBase]) {
            set.objects.push_back(i);
        }
        result.sets.push_back(std::move(set));
    }
    return result;
}

std::string blocksets::customObjectString(ScanResult const& result, BlockSet const& set, bool stripGroups) {
    std::string out;
    for (auto index : set.objects) {
        out += result.objects[index].toString(stripGroups);
        out += ';';
    }
    return out;
}
