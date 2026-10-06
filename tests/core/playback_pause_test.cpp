#include "core/playback_pause.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <set>
#include <vector>

using hr::core::PlaybackPause;

namespace {
// Players by id; `playing` is what the device itself would report.
struct FakePlayers final : PlaybackPause::Control {
    std::set<std::uintptr_t> playing;
    std::vector<std::uintptr_t> asked;
    std::vector<std::uintptr_t> resumed;

    bool pause_if_playing(std::uintptr_t id) override {
        asked.push_back(id);
        return playing.erase(id) != 0;
    }
    void resume(std::uintptr_t id) override {
        playing.insert(id);
        resumed.push_back(id);
    }
};
} // namespace

TEST_CASE("pausing pauses what plays and resuming resumes exactly that", "[playback_pause]") {
    FakePlayers players;
    players.playing = {1, 3};
    PlaybackPause pause;
    for (std::uintptr_t id : {1u, 2u, 3u}) {
        pause.opened(id);
    }
    CHECK(pause.pause(players) == 2);
    CHECK(players.playing.empty());
    pause.resume(players);
    CHECK(players.resumed == std::vector<std::uintptr_t>{1, 3});
    pause.resume(players); // nothing twice
    CHECK(players.resumed.size() == 2);
}

TEST_CASE("closed players are forgotten", "[playback_pause]") {
    FakePlayers players;
    players.playing = {1};
    PlaybackPause pause;
    pause.opened(1);
    pause.closed(1);
    CHECK(pause.pause(players) == 0);
    CHECK(players.asked.empty());
}

TEST_CASE("a player known to be stopped is not asked", "[playback_pause]") {
    // A MIDI stream is opened paused and only runs once the game restarts it.
    FakePlayers players;
    players.playing = {1, 2};
    PlaybackPause pause;
    pause.opened(1, false);
    pause.opened(2, false);
    pause.set_running(2, true);
    CHECK(pause.pause(players) == 1);
    CHECK(players.asked == std::vector<std::uintptr_t>{2});
}

TEST_CASE("the game stopping a player while paused cancels its resume", "[playback_pause]") {
    FakePlayers players;
    players.playing = {1};
    PlaybackPause pause;
    pause.opened(1);
    pause.pause(players);
    pause.set_running(1, false);
    pause.resume(players);
    CHECK(players.resumed.empty());
}

TEST_CASE("a player closed while paused is not resumed", "[playback_pause]") {
    FakePlayers players;
    players.playing = {1};
    PlaybackPause pause;
    pause.opened(1);
    pause.pause(players);
    pause.closed(1);
    pause.resume(players);
    CHECK(players.resumed.empty());
}
