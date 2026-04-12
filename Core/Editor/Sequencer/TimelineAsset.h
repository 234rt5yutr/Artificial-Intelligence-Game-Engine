#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {
namespace Editor {

    using EditorJson = nlohmann::json;

    enum class TrackType : uint8_t {
        Camera = 0,
        Animation = 1,
        Audio = 2,
        Event = 3
    };

    inline const char* TrackTypeToString(TrackType type) {
        switch (type) {
            case TrackType::Camera: return "camera";
            case TrackType::Animation: return "animation";
            case TrackType::Audio: return "audio";
            case TrackType::Event: return "event";
            default: return "event";
        }
    }

    struct TimelineClip {
        std::string ClipId;
        float StartTime = 0.0f;
        float EndTime = 0.0f;
        std::string ClipType;
        float BlendInSeconds = 0.0f;
        float BlendOutSeconds = 0.0f;
        std::string Easing = "linear";
        EditorJson Payload = EditorJson::object();
    };

    struct TimelineTrack {
        std::string TrackId;
        TrackType Type = TrackType::Event;
        std::string DisplayName;
        int SortOrder = 0;
        bool Muted = false;
        bool Solo = false;
        std::vector<TimelineClip> Clips;
    };

    struct TimelineAsset {
        std::string Guid;
        uint32_t SchemaVersion = 1;
        float DurationSeconds = 10.0f;
        float FrameRate = 30.0f;
        std::vector<TimelineTrack> Tracks;
    };

    struct SequencerRuntimeCache {
        std::unordered_map<std::string, TimelineAsset> Timelines;
    };

} // namespace Editor
} // namespace Core

