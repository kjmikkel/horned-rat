#include "core/sound_pause.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

using hr::core::SoundPause;

namespace {
constexpr std::uintptr_t kDevice = 100; // the DirectSound object the buffers belong to
constexpr std::uintptr_t kOtherDevice = 200;

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

void add_all(SoundPause& pause, std::initializer_list<std::uintptr_t> ids,
             std::uintptr_t device = kDevice) {
    for (const std::uintptr_t id : ids) {
        pause.add(id, device);
    }
}
} // namespace

TEST_CASE("pausing stops the playing buffers and resuming plays them again", "[sound_pause]") {
    FakeSound sound;
    sound.state = {{1, {true, true}}, {2, {false, false}}, {3, {true, false}}};
    SoundPause pause;
    add_all(pause, {1, 2, 3});
    pause.pause(sound);
    CHECK(sound.state[1] == std::pair{false, true});
    CHECK(sound.state[3] == std::pair{false, false});
    pause.resume(sound);
    CHECK(sound.played == std::vector<std::uintptr_t>{1, 3});
    CHECK(sound.state[1] == std::pair{true, true});   // still looping
    CHECK(sound.state[3] == std::pair{true, false});  // one-shot, resumes where it stopped
    CHECK(sound.state[2] == std::pair{false, false}); // was silent, stays silent
}

TEST_CASE("pausing reports how many buffers it stopped out of how many it knows", "[sound_pause]") {
    FakeSound sound;
    sound.state = {{1, {true, true}}, {2, {false, false}}, {3, {true, false}}};
    SoundPause pause;
    add_all(pause, {1, 2, 3});
    const auto counts = pause.pause(sound);
    CHECK(counts.stopped == 2);
    CHECK(counts.known == 3);
}

TEST_CASE("resuming twice, or without pausing, plays nothing twice", "[sound_pause]") {
    FakeSound sound;
    sound.state = {{1, {true, false}}};
    SoundPause pause;
    add_all(pause, {1});
    pause.resume(sound);
    CHECK(sound.played.empty());
    pause.pause(sound);
    pause.resume(sound);
    pause.resume(sound);
    CHECK(sound.played.size() == 1);
}

TEST_CASE("a buffer is forgotten when its last reference is released", "[sound_pause]") {
    FakeSound sound;
    sound.state = {{1, {true, true}}};
    SoundPause pause;
    add_all(pause, {1});
    CHECK(pause.release(1, [] { return 1ul; }) == 1ul); // still referenced
    pause.pause(sound);
    CHECK_FALSE(sound.state[1].first);
    CHECK(pause.release(1, [] { return 0ul; }) == 0ul); // gone
    pause.resume(sound);
    CHECK(sound.played.empty());
}

TEST_CASE("releasing a DirectSound object forgets the buffers it freed", "[sound_pause]") {
    FakeSound sound;
    sound.state = {{1, {true, true}}, {2, {true, true}}, {3, {true, true}}};
    SoundPause pause;
    add_all(pause, {1, 2});
    add_all(pause, {3}, kOtherDevice);
    CHECK(pause.release_device(kDevice, [] { return 1ul; }) == 1ul); // still referenced
    CHECK(pause.release_device(kDevice, [] { return 0ul; }) == 0ul); // frees buffers 1 and 2
    const auto counts = pause.pause(sound);
    CHECK(counts.known == 1);
    CHECK(sound.state[1].first); // never touched: it no longer exists
    CHECK_FALSE(sound.state[3].first);
}

TEST_CASE("while paused, the game's own Play is held back until resume", "[sound_pause]") {
    // The game's sound thread runs on while its main thread is held.
    FakeSound sound;
    SoundPause pause;
    add_all(pause, {1});
    CHECK(pause.allow_play(1, true)); // not paused: plays at once
    pause.pause(sound);
    CHECK_FALSE(pause.allow_play(1, true));
    CHECK(pause.reported_playing(1) == std::optional<bool>{true});
    pause.resume(sound);
    CHECK(sound.played == std::vector<std::uintptr_t>{1});
    CHECK(sound.state[1] == std::pair{true, true});
}

TEST_CASE("while paused, the game's own Stop cancels the buffer's restart", "[sound_pause]") {
    FakeSound sound;
    sound.state = {{1, {true, true}}};
    SoundPause pause;
    add_all(pause, {1});
    pause.pause(sound);
    pause.note_stop(1);
    CHECK_FALSE(pause.reported_playing(1).has_value());
    pause.resume(sound);
    CHECK(sound.played.empty());
}

TEST_CASE("buffers stopped by the pause still look playing to the game", "[sound_pause]") {
    FakeSound sound;
    sound.state = {{1, {true, false}}, {2, {false, false}}};
    SoundPause pause;
    add_all(pause, {1, 2});
    CHECK_FALSE(pause.reported_playing(1).has_value()); // not paused: ask DirectSound
    pause.pause(sound);
    CHECK(pause.reported_playing(1) == std::optional<bool>{false}); // playing, not looping
    CHECK_FALSE(pause.reported_playing(2).has_value());
}

TEST_CASE("unknown buffers are never held back", "[sound_pause]") {
    FakeSound sound;
    SoundPause pause;
    pause.pause(sound);
    CHECK(pause.allow_play(42, false));
}
