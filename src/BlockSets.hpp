#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Pure detection logic, independent from GD so it can be tested on its own

namespace blocksets {
    enum class Kind {
        Block,
        Slope,
        Decoration,
        Other,
    };

    struct ObjectInfo {
        Kind kind = Kind::Other;
        // Unscaled size of the object, in units (a full block is 30x30)
        float width = 30.f;
        float height = 30.f;
    };

    struct LevelObject {
        // Key/value pairs in their original order, so the object can be written back out
        std::vector<std::pair<std::string, std::string>> props;
        int id = 0;
        float x = 0.f;
        float y = 0.f;

        std::string_view get(std::string_view key) const;
        float getFloat(std::string_view key, float def = 0.f) const;
        std::string toString(bool stripGroups) const;
    };

    struct Options {
        int minRepeats = 2;
        size_t maxSets = 30;
        float attachDistance = 10.f;
        bool includeSlopes = true;
        bool stripGroups = true;
    };

    struct BlockSet {
        // Indices into ScanResult::objects: the base block followed by its decoration
        std::vector<size_t> objects;
        int repeats = 0;
    };

    struct ScanResult {
        std::vector<LevelObject> objects;
        std::vector<BlockSet> sets;
        size_t decoratedBases = 0;
    };

    using InfoProvider = std::function<ObjectInfo(int id)>;

    // Parses an uncompressed level string ("header;obj;obj;...")
    std::vector<LevelObject> parseLevel(std::string_view levelString);

    ScanResult scan(std::string_view levelString, InfoProvider const& info, Options const& options);

    // Builds the string GD stores for a custom object
    std::string customObjectString(ScanResult const& result, BlockSet const& set, bool stripGroups);
}
