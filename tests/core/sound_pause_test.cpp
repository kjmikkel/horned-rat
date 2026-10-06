#include "core/sound_pause.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

using hr::core::SoundPause;

namespace {
// Buffers by id: playing?, looping?
struct FakeSound final : SoundPause::Control {
    std::map<std::uintptr_t, std::pair<bool, bool>> state;
    std::vector<std::uintptr_t> played;

    std::optional<bool> stop_if_playing(std::uintptr_t buffer) override {
        auto& [playing, looping] = state[buffer];
        if (!playing) {
            return std::nullopt;
        }
        playing = false;
        return looping;
    }
    void play(std::uintptr_t buffer, bool looping) override {
        state[buffer] = {true, looping};
        played.push_back(buffer);
    }
};
} // namespace

TEST_CASE("pausing stops the playing buffers and resuming plays them again", "[sound_pause]") {
    FakeSound sound;
    sound.state = {{1, {true, true}}, {2, {false, false}}, {3, {true, false}}};
    SoundPause pause;
    for (std::uintptr_t id : {1u, 2u, 3u}) {
        pause.add(id);
    }
    pause.pause(sound);
    CHECK(sound.state[1] == std::pair{false, true});
    CHECK(sound.state[3] == std::pair{false, false});
    pause.resume(sound);
    CHECK(sound.played == std::vector<std::uintptr_t>{1, 3});
    CHECK(sound.state[1] == std::pair{true, true});   // still looping
    CHECK(sound.state[3] == std::pair{true, false});  // one-shot, resumes where it stopped
    CHECK(sound.state[2] == std::pair{false, false}); // was silent, stays silent
}

TEST_CASE("a buffer released while paused is not played again", "[sound_pause]") {
    FakeSound sound;
    sound.state = {{1, {true, true}}};
    SoundPause pause;
    pause.add(1);
    pause.pause(sound);
    pause.remove(1);
    pause.resume(sound);
    CHECK(sound.played.empty());
}

TEST_CASE("resuming twice, or without pausing, plays nothing twice", "[sound_pause]") {
    FakeSound sound;
    sound.state = {{1, {true, false}}};
    SoundPause pause;
    pause.add(1);
    pause.resume(sound);
    CHECK(sound.played.empty());
    pause.pause(sound);
    pause.resume(sound);
    pause.resume(sound);
    CHECK(sound.played.size() == 1);
}

TEST_CASE("removed buffers are no longer paused", "[sound_pause]") {
    FakeSound sound;
    sound.state = {{1, {true, true}}};
    SoundPause pause;
    pause.add(1);
    pause.remove(1);
    pause.pause(sound);
    CHECK(sound.state[1].first); // untouched
}

TEST_CASE("a buffer is forgotten when its last reference is released", "[sound_pause]") {
    FakeSound sound;
    sound.state = {{1, {true, true}}};
    SoundPause pause;
    pause.add(1);
    CHECK(pause.release(1, [] { return 1ul; }) == 1ul); // still referenced
    pause.pause(sound);
    CHECK_FALSE(sound.state[1].first);
    CHECK(pause.release(1, [] { return 0ul; }) == 0ul); // gone
    pause.resume(sound);
    CHECK(sound.played.empty());
}
