#include "song_selection.h"
#include "user_button_policy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

unsigned groups = 0;
unsigned checks = 0;

void check(bool condition, unsigned line)
{
    ++checks;
    if (!condition)
    {
        std::cerr << "USER_BUTTON policy check failed at line " << line << '\n';
        std::exit(EXIT_FAILURE);
    }
}

#define CHECK(condition) check((condition), __LINE__)
#define EXPECT_ACTION(actual, expected) \
    CHECK((actual) == UserButtonAction::expected)

void qualify(UserButtonPolicy &button, uint32_t start, bool active,
             bool confirmed, UserButtonAction expected)
{
    EXPECT_ACTION(button.update(true, start, false, active, confirmed), None);
    EXPECT_ACTION(button.update(true, start + 24U, false, active, confirmed), None);
    CHECK(button.update(true, start + 25U, false, active, confirmed) == expected);
}

void release(UserButtonPolicy &button, uint32_t start)
{
    EXPECT_ACTION(button.update(false, start, false, true, true), None);
    EXPECT_ACTION(button.update(false, start + 24U, false, true, true), None);
    EXPECT_ACTION(button.update(false, start + 25U, false, true, true), None);
}

// Exercise the real, unchanged selection controller alongside the policy.
// The caller supplies selection priority to the hardware-independent policy;
// player transport and song identity deliberately stay outside this helper.
struct SelectionHarness
{
    SongSelectionInput selection;
    UserButtonPolicy user;
    SelectionEvents events;
    bool playbackActive = true;
    bool hasConfirmedSong = true;

    SelectionHarness() { user.initialize(false, 0); }

    UserButtonAction step(uint32_t now, const SelectionButtons &raw,
                          bool userPressed)
    {
        const bool selectingBefore = selection.selecting();
        events = selection.update(now, raw, playbackActive);
        const bool priority = selectingBefore || selection.selecting() || raw.anyPressed() ||
            events.captured || events.readyToConfirm || events.confirmed ||
            events.timedOut || events.pauseResume;
        return user.update(userPressed, now, priority, playbackActive,
                           hasConfirmedSong);
    }

    void enterConfirmation()
    {
        SelectionButtons chord;
        chord.up = chord.down = true;
        EXPECT_ACTION(step(20, chord, false), None);
        EXPECT_ACTION(step(60, chord, false), None);
        CHECK(events.captured);
        CHECK(selection.waitingForRelease());
        CHECK(selection.pendingSong() == 4);
        EXPECT_ACTION(step(100, SelectionButtons(), false), None);
        EXPECT_ACTION(step(140, SelectionButtons(), false), None);
        CHECK(events.readyToConfirm);
        CHECK(selection.selecting());
        CHECK(!selection.waitingForRelease());
    }
};

void playingPressRequestsStop()
{
    UserButtonPolicy button;
    button.initialize(false, 0);
    qualify(button, 20, true, true, UserButtonAction::Stop);
}

void pausedPressRequestsStopRatherThanResume()
{
    UserButtonPolicy button;
    button.initialize(false, 0);
    // The integration's active transport flag includes Playing and Paused.
    qualify(button, 20, true, true, UserButtonAction::Stop);
    EXPECT_ACTION(button.update(true, 100, false, false, true), None);
    release(button, 120);
    qualify(button, 160, false, true, UserButtonAction::Replay);
}

void idleWithConfirmedIdentityRequestsReplay()
{
    UserButtonPolicy button;
    button.initialize(false, 0);
    qualify(button, 20, false, true, UserButtonAction::Replay);
    for (uint32_t now = 60; now < 1060; now += 20)
        EXPECT_ACTION(button.update(true, now, false, true, true), None);
}

void idleWithoutConfirmedIdentityDoesNothing()
{
    UserButtonPolicy button;
    button.initialize(false, 0);
    qualify(button, 20, false, false, UserButtonAction::None);
    // An ignored press cannot become a deferred replay if history appears.
    EXPECT_ACTION(button.update(true, 100, false, false, true), None);
    release(button, 120);
    qualify(button, 160, false, true, UserButtonAction::Replay);
}

void completedSongRemainsEligibleForReplay()
{
    UserButtonPolicy button;
    button.initialize(false, 0);
    for (uint32_t now = 0; now <= 100; now += 20)
        EXPECT_ACTION(button.update(false, now, false, true, true), None);
    // Natural completion changes transport activity, retaining confirmed history.
    qualify(button, 120, false, true, UserButtonAction::Replay);
}

void twentyMillisecondCadenceDoesNotRepeatHeldActions()
{
    UserButtonPolicy button;
    button.initialize(false, 0);
    EXPECT_ACTION(button.update(true, 20, false, true, true), None);
    EXPECT_ACTION(button.update(true, 40, false, true, true), None);
    EXPECT_ACTION(button.update(true, 60, false, true, true), Stop);
    for (uint32_t now = 80; now <= 2080; now += 20)
        EXPECT_ACTION(button.update(true, now, false, false, true), None);
    EXPECT_ACTION(button.update(false, 2100, false, false, true), None);
    EXPECT_ACTION(button.update(false, 2120, false, false, true), None);
    EXPECT_ACTION(button.update(false, 2140, false, false, true), None);
    EXPECT_ACTION(button.update(true, 2160, false, false, true), None);
    EXPECT_ACTION(button.update(true, 2180, false, false, true), None);
    EXPECT_ACTION(button.update(true, 2200, false, false, true), Replay);
}

void simultaneousCaptureSuppressesUserButton()
{
    SelectionHarness harness;
    SelectionButtons chord;
    chord.up = chord.down = true;
    EXPECT_ACTION(harness.step(20, chord, true), None);
    EXPECT_ACTION(harness.step(60, chord, true), None);
    CHECK(harness.events.captured);
    CHECK(harness.selection.pendingSong() == 4);
    CHECK(harness.selection.waitingForRelease());
}

void releaseAllPhaseSuppressesUserButton()
{
    SelectionHarness harness;
    SelectionButtons chord;
    chord.up = chord.left = true;
    EXPECT_ACTION(harness.step(20, chord, false), None);
    EXPECT_ACTION(harness.step(60, chord, false), None);
    CHECK(harness.events.captured);
    EXPECT_ACTION(harness.step(80, chord, true), None);
    EXPECT_ACTION(harness.step(100, SelectionButtons(), true), None);
    CHECK(harness.selection.waitingForRelease());
    EXPECT_ACTION(harness.step(140, SelectionButtons(), true), None);
    CHECK(harness.events.readyToConfirm);
    CHECK(harness.selection.pendingSong() == 2);
}

void confirmationIgnoresInputWithoutChangingDeadline()
{
    SelectionHarness harness;
    harness.enterConfirmation();
    EXPECT_ACTION(harness.step(200, SelectionButtons(), true), None);
    EXPECT_ACTION(harness.step(240, SelectionButtons(), true), None);
    CHECK(harness.selection.selecting());
    CHECK(harness.selection.pendingSong() == 4);
    EXPECT_ACTION(harness.step(5139, SelectionButtons(), true), None);
    CHECK(!harness.events.timedOut);
    EXPECT_ACTION(harness.step(5140, SelectionButtons(), true), None);
    CHECK(harness.events.timedOut);
    CHECK(!harness.selection.selecting());
}

void heldConsumedGestureCannotFireAfterConfirmation()
{
    SelectionHarness harness;
    harness.enterConfirmation();
    SelectionButtons up;
    up.up = true;
    EXPECT_ACTION(harness.step(200, up, true), None);
    EXPECT_ACTION(harness.step(240, up, true), None);
    CHECK(harness.events.confirmed);
    CHECK(!harness.selection.selecting());
    EXPECT_ACTION(harness.step(280, SelectionButtons(), true), None);
    for (uint32_t now = 320; now <= 2320; now += 20)
        EXPECT_ACTION(harness.step(now, SelectionButtons(), true), None);
    EXPECT_ACTION(harness.step(2340, SelectionButtons(), false), None);
    EXPECT_ACTION(harness.step(2364, SelectionButtons(), false), None);
    EXPECT_ACTION(harness.step(2365, SelectionButtons(), false), None);
    EXPECT_ACTION(harness.step(2400, SelectionButtons(), true), None);
    EXPECT_ACTION(harness.step(2425, SelectionButtons(), true), Stop);
}

void rawPressNearTimeoutIsConsumedBeforeQualification()
{
    SelectionHarness harness;
    harness.enterConfirmation();
    EXPECT_ACTION(harness.step(5130, SelectionButtons(), true), None);
    EXPECT_ACTION(harness.step(5140, SelectionButtons(), true), None);
    CHECK(harness.events.timedOut);
    EXPECT_ACTION(harness.step(5155, SelectionButtons(), true), None);
    EXPECT_ACTION(harness.step(5200, SelectionButtons(), true), None);
    EXPECT_ACTION(harness.step(5220, SelectionButtons(), false), None);
    EXPECT_ACTION(harness.step(5245, SelectionButtons(), false), None);
    EXPECT_ACTION(harness.step(5300, SelectionButtons(), true), None);
    EXPECT_ACTION(harness.step(5325, SelectionButtons(), true), Stop);
}

void veryShortSuppressedPulseStillRearmsOnStableRelease()
{
    SelectionHarness harness;
    harness.enterConfirmation();
    EXPECT_ACTION(harness.step(5130, SelectionButtons(), true), None);
    EXPECT_ACTION(harness.step(5135, SelectionButtons(), false), None);
    EXPECT_ACTION(harness.step(5140, SelectionButtons(), false), None);
    CHECK(harness.events.timedOut);
    EXPECT_ACTION(harness.step(5159, SelectionButtons(), false), None);
    EXPECT_ACTION(harness.step(5160, SelectionButtons(), false), None);
    EXPECT_ACTION(harness.step(5200, SelectionButtons(), true), None);
    EXPECT_ACTION(harness.step(5225, SelectionButtons(), true), Stop);
}

void timeoutTerminalCycleAlsoConsumesNewRawPress()
{
    SelectionHarness harness;
    harness.enterConfirmation();
    EXPECT_ACTION(harness.step(5140, SelectionButtons(), true), None);
    CHECK(harness.events.timedOut);
    CHECK(!harness.selection.selecting());
    EXPECT_ACTION(harness.step(5165, SelectionButtons(), true), None);
    EXPECT_ACTION(harness.step(5200, SelectionButtons(), true), None);
}

void rawSelectionOnQualificationCycleWins()
{
    SelectionHarness harness;
    EXPECT_ACTION(harness.step(20, SelectionButtons(), true), None);
    SelectionButtons chord;
    chord.up = chord.right = true;
    // USER_BUTTON would qualify here, while UP has only just become raw LOW.
    EXPECT_ACTION(harness.step(60, chord, true), None);
    CHECK(!harness.events.captured);
    EXPECT_ACTION(harness.step(100, chord, true), None);
    CHECK(harness.events.captured);
    CHECK(harness.selection.pendingSong() == 1);
}

void standaloneRawBinaryButtonsAlsoConsumeSimultaneousUserInput()
{
    for (unsigned bit = 0; bit != 3; ++bit)
    {
        for (unsigned transport = 0; transport != 2; ++transport)
        {
            SelectionHarness harness;
            harness.playbackActive = transport != 0;
            EXPECT_ACTION(harness.step(20, SelectionButtons(), true), None);
            SelectionButtons raw;
            raw.down = bit == 0;
            raw.left = bit == 1;
            raw.right = bit == 2;
            // A raw binary button wins even without UP or an active flow.
            EXPECT_ACTION(harness.step(60, raw, true), None);
            EXPECT_ACTION(harness.step(100, raw, true), None);
            CHECK(!harness.events.captured);
            CHECK(!harness.events.confirmed);
            CHECK(!harness.events.pauseResume);
            CHECK(!harness.selection.selecting());
            CHECK(harness.selection.pendingSong() == 0);
            EXPECT_ACTION(harness.step(140, SelectionButtons(), true), None);
            EXPECT_ACTION(harness.step(180, SelectionButtons(), true), None);
            CHECK(!harness.events.captured);
            CHECK(!harness.selection.selecting());
            EXPECT_ACTION(harness.step(200, SelectionButtons(), false), None);
            EXPECT_ACTION(harness.step(224, SelectionButtons(), false), None);
            EXPECT_ACTION(harness.step(225, SelectionButtons(), false), None);
            EXPECT_ACTION(harness.step(260, SelectionButtons(), true), None);
            CHECK(harness.step(285, SelectionButtons(), true) ==
                  (harness.playbackActive ? UserButtonAction::Stop
                                          : UserButtonAction::Replay));
        }
    }
}

void longUpPauseResumeControllerBehaviorIsPreserved()
{
    SelectionHarness harness;
    SelectionButtons up;
    up.up = true;
    EXPECT_ACTION(harness.step(20, up, false), None);
    EXPECT_ACTION(harness.step(60, up, false), None);
    CHECK(!harness.events.captured);
    CHECK(!harness.selection.selecting());
    EXPECT_ACTION(harness.step(1060, up, false), None);
    EXPECT_ACTION(harness.step(1100, SelectionButtons(), false), None);
    EXPECT_ACTION(harness.step(1140, SelectionButtons(), false), None);
    CHECK(harness.events.pauseResume);
    CHECK(!harness.events.captured);
    EXPECT_ACTION(harness.step(1160, SelectionButtons(), false), None);
    CHECK(!harness.events.pauseResume);
    EXPECT_ACTION(harness.step(1200, up, false), None);
    EXPECT_ACTION(harness.step(1240, up, false), None);
    EXPECT_ACTION(harness.step(2240, up, false), None);
    EXPECT_ACTION(harness.step(2280, SelectionButtons(), false), None);
    EXPECT_ACTION(harness.step(2320, SelectionButtons(), false), None);
    CHECK(harness.events.pauseResume);
    CHECK(!harness.selection.selecting());
}

void simultaneousLongUpReleaseConsumesUserButton()
{
    SelectionHarness harness;
    SelectionButtons up;
    up.up = true;
    EXPECT_ACTION(harness.step(20, up, false), None);
    EXPECT_ACTION(harness.step(60, up, false), None);
    EXPECT_ACTION(harness.step(1100, SelectionButtons(), true), None);
    EXPECT_ACTION(harness.step(1140, SelectionButtons(), true), None);
    CHECK(harness.events.pauseResume);
    EXPECT_ACTION(harness.step(1180, SelectionButtons(), true), None);
}

void bounceKeepsVerifiedQualificationAndRearmRules()
{
    static_assert(UserButtonDebounce::DebounceMs == 25,
                  "Preserve the physically verified debounce interval");
    UserButtonPolicy button;
    button.initialize(false, 0);
    EXPECT_ACTION(button.update(true, 10, false, true, true), None);
    EXPECT_ACTION(button.update(false, 18, false, true, true), None);
    EXPECT_ACTION(button.update(true, 22, false, true, true), None);
    EXPECT_ACTION(button.update(false, 33, false, true, true), None);
    EXPECT_ACTION(button.update(true, 40, false, true, true), None);
    EXPECT_ACTION(button.update(true, 64, false, true, true), None);
    EXPECT_ACTION(button.update(true, 65, false, true, true), Stop);
    EXPECT_ACTION(button.update(false, 70, false, false, true), None);
    EXPECT_ACTION(button.update(true, 80, false, false, true), None);
    EXPECT_ACTION(button.update(false, 90, false, false, true), None);
    EXPECT_ACTION(button.update(true, 110, false, false, true), None);
    EXPECT_ACTION(button.update(true, 135, false, false, true), None);
    release(button, 150);
    qualify(button, 200, false, true, UserButtonAction::Replay);
}

void bootHeldSuppressionRemainsIntact()
{
    UserButtonPolicy button;
    button.initialize(true, 100);
    for (uint32_t now = 100; now <= 1100; now += 20)
        EXPECT_ACTION(button.update(true, now, false, true, true), None);
    EXPECT_ACTION(button.update(false, 1120, false, true, true), None);
    EXPECT_ACTION(button.update(true, 1140, false, true, true), None);
    EXPECT_ACTION(button.update(true, 1180, false, true, true), None);
    release(button, 1200);
    qualify(button, 1240, true, true, UserButtonAction::Stop);
}

void suppressedReleaseBounceDoesNotRearmEarly()
{
    UserButtonPolicy button;
    button.initialize(false, 0);
    EXPECT_ACTION(button.update(true, 10, true, true, true), None);
    EXPECT_ACTION(button.update(true, 35, false, true, true), None);
    EXPECT_ACTION(button.update(false, 40, false, true, true), None);
    EXPECT_ACTION(button.update(true, 60, false, true, true), None);
    EXPECT_ACTION(button.update(true, 85, false, true, true), None);
    release(button, 100);
    qualify(button, 140, true, true, UserButtonAction::Stop);
}

void tickWrapAndReinitializationRemainSafe()
{
    const uint32_t maximum = std::numeric_limits<uint32_t>::max();
    UserButtonPolicy button;
    button.initialize(false, maximum - 20U);
    EXPECT_ACTION(button.update(true, maximum - 9U, false, true, true), None);
    EXPECT_ACTION(button.update(true, 14, false, true, true), None);
    EXPECT_ACTION(button.update(true, 15, false, true, true), Stop);

    button.initialize(false, maximum - 20U);
    EXPECT_ACTION(button.update(true, maximum - 9U, true, true, true), None);
    EXPECT_ACTION(button.update(true, 15, false, true, true), None);
    release(button, 20);
    qualify(button, 60, false, true, UserButtonAction::Replay);

    button.initialize(true, 100);
    EXPECT_ACTION(button.update(true, 100, false, true, true), None);
    release(button, 120);
    qualify(button, 160, true, true, UserButtonAction::Stop);
}

void run(void (*test)())
{
    test();
    ++groups;
}

} // namespace

int main()
{
    run(playingPressRequestsStop);
    run(pausedPressRequestsStopRatherThanResume);
    run(idleWithConfirmedIdentityRequestsReplay);
    run(idleWithoutConfirmedIdentityDoesNothing);
    run(completedSongRemainsEligibleForReplay);
    run(twentyMillisecondCadenceDoesNotRepeatHeldActions);
    run(simultaneousCaptureSuppressesUserButton);
    run(releaseAllPhaseSuppressesUserButton);
    run(confirmationIgnoresInputWithoutChangingDeadline);
    run(heldConsumedGestureCannotFireAfterConfirmation);
    run(rawPressNearTimeoutIsConsumedBeforeQualification);
    run(veryShortSuppressedPulseStillRearmsOnStableRelease);
    run(timeoutTerminalCycleAlsoConsumesNewRawPress);
    run(rawSelectionOnQualificationCycleWins);
    run(standaloneRawBinaryButtonsAlsoConsumeSimultaneousUserInput);
    run(longUpPauseResumeControllerBehaviorIsPreserved);
    run(simultaneousLongUpReleaseConsumesUserButton);
    run(bounceKeepsVerifiedQualificationAndRearmRules);
    run(bootHeldSuppressionRemainsIntact);
    run(suppressedReleaseBounceDoesNotRearmEarly);
    run(tickWrapAndReinitializationRemainSafe);
    std::cout << "USER_BUTTON policy: " << groups << " groups, " << checks
              << " checks passed\n";
    return EXIT_SUCCESS;
}
