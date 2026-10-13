#include "user_button_debounce.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

unsigned checks = 0;

void expect(UserButtonTransition actual, UserButtonTransition expected,
            unsigned line)
{
    ++checks;
    if (actual != expected) {
        std::cerr << "USER_BUTTON check failed at line " << line << '\n';
        std::exit(EXIT_FAILURE);
    }
}

#define EXPECT_TRANSITION(actual, expected) \
    expect((actual), UserButtonTransition::expected, __LINE__)

void releasedStartupAndHeldStatesStayQuiet()
{
    UserButtonDebounce button;
    button.initialize(false, 0);
    for (uint32_t now = 0; now <= 1000; now += 20)
        EXPECT_TRANSITION(button.update(false, now), None);

    EXPECT_TRANSITION(button.update(true, 1020), None);
    EXPECT_TRANSITION(button.update(true, 1040), None);
    EXPECT_TRANSITION(button.update(true, 1060), Pressed);
    for (uint32_t now = 1080; now <= 3060; now += 20)
        EXPECT_TRANSITION(button.update(true, now), None);

    EXPECT_TRANSITION(button.update(false, 3080), None);
    EXPECT_TRANSITION(button.update(false, 3100), None);
    EXPECT_TRANSITION(button.update(false, 3120), Released);
    for (uint32_t now = 3140; now <= 4140; now += 20)
        EXPECT_TRANSITION(button.update(false, now), None);
}

void exactDebounceBoundary()
{
    static_assert(UserButtonDebounce::DebounceMs == 25,
                  "The smoke test must retain 25 ms debounce");
    UserButtonDebounce button;
    button.initialize(false, 0);
    EXPECT_TRANSITION(button.update(true, 50), None);
    EXPECT_TRANSITION(button.update(true, 74), None);
    EXPECT_TRANSITION(button.update(true, 75), Pressed);
    EXPECT_TRANSITION(button.update(true, 76), None);
    EXPECT_TRANSITION(button.update(false, 100), None);
    EXPECT_TRANSITION(button.update(false, 124), None);
    EXPECT_TRANSITION(button.update(false, 125), Released);
    EXPECT_TRANSITION(button.update(false, 126), None);
}

void twentyMillisecondPolling()
{
    UserButtonDebounce button;
    button.initialize(false, 0);
    // A 25 ms stable interval is observed after 40 ms at this cadence.
    EXPECT_TRANSITION(button.update(true, 20), None);
    EXPECT_TRANSITION(button.update(true, 40), None);
    EXPECT_TRANSITION(button.update(true, 60), Pressed);
    EXPECT_TRANSITION(button.update(true, 80), None);
    EXPECT_TRANSITION(button.update(false, 100), None);
    EXPECT_TRANSITION(button.update(false, 120), None);
    EXPECT_TRANSITION(button.update(false, 140), Released);
    EXPECT_TRANSITION(button.update(false, 160), None);
}

void pressBounceRestartsTheStableWindow()
{
    UserButtonDebounce button;
    button.initialize(false, 0);
    EXPECT_TRANSITION(button.update(true, 10), None);
    EXPECT_TRANSITION(button.update(false, 18), None);
    EXPECT_TRANSITION(button.update(true, 22), None);
    EXPECT_TRANSITION(button.update(false, 33), None);
    EXPECT_TRANSITION(button.update(true, 40), None);
    EXPECT_TRANSITION(button.update(true, 64), None);
    EXPECT_TRANSITION(button.update(true, 65), Pressed);
    EXPECT_TRANSITION(button.update(true, 100), None);
}

void releaseBounceDoesNotDuplicateTransitions()
{
    UserButtonDebounce button;
    button.initialize(false, 0);
    EXPECT_TRANSITION(button.update(true, 5), None);
    EXPECT_TRANSITION(button.update(true, 30), Pressed);
    EXPECT_TRANSITION(button.update(false, 35), None);
    EXPECT_TRANSITION(button.update(true, 45), None);
    EXPECT_TRANSITION(button.update(false, 50), None);
    EXPECT_TRANSITION(button.update(true, 62), None);
    EXPECT_TRANSITION(button.update(false, 70), None);
    EXPECT_TRANSITION(button.update(false, 94), None);
    EXPECT_TRANSITION(button.update(false, 95), Released);
    EXPECT_TRANSITION(button.update(false, 120), None);
}

void bootHeldRequiresReleaseBeforeNewPress()
{
    UserButtonDebounce button;
    button.initialize(true, 100);
    for (uint32_t now = 100; now <= 1100; now += 20)
        EXPECT_TRANSITION(button.update(true, now), None);

    EXPECT_TRANSITION(button.update(false, 1120), None);
    EXPECT_TRANSITION(button.update(false, 1144), None);
    EXPECT_TRANSITION(button.update(false, 1145), Released);
    EXPECT_TRANSITION(button.update(true, 1200), None);
    EXPECT_TRANSITION(button.update(true, 1224), None);
    EXPECT_TRANSITION(button.update(true, 1225), Pressed);
    EXPECT_TRANSITION(button.update(true, 1300), None);
}

void bootHeldReleaseBounceDoesNotRearmEarly()
{
    UserButtonDebounce button;
    button.initialize(true, 0);
    EXPECT_TRANSITION(button.update(false, 20), None);
    EXPECT_TRANSITION(button.update(true, 40), None);
    EXPECT_TRANSITION(button.update(true, 65), None);
    EXPECT_TRANSITION(button.update(false, 100), None);
    EXPECT_TRANSITION(button.update(true, 120), None);
    EXPECT_TRANSITION(button.update(false, 130), None);
    EXPECT_TRANSITION(button.update(false, 154), None);
    EXPECT_TRANSITION(button.update(false, 155), Released);
    EXPECT_TRANSITION(button.update(true, 200), None);
    EXPECT_TRANSITION(button.update(true, 225), Pressed);
}

void debounceSurvivesTickWraparound()
{
    const uint32_t maximum = std::numeric_limits<uint32_t>::max();
    UserButtonDebounce press;
    press.initialize(false, maximum - 10U);
    EXPECT_TRANSITION(press.update(true, maximum - 9U), None);
    EXPECT_TRANSITION(press.update(true, 14), None); // 24 ms elapsed.
    EXPECT_TRANSITION(press.update(true, 15), Pressed);
    EXPECT_TRANSITION(press.update(true, 16), None);

    UserButtonDebounce release;
    release.initialize(true, maximum - 10U);
    EXPECT_TRANSITION(release.update(false, maximum - 9U), None);
    EXPECT_TRANSITION(release.update(false, 14), None);
    EXPECT_TRANSITION(release.update(false, 15), Released);
    EXPECT_TRANSITION(release.update(false, 16), None);
}

void instancesAndReinitializationAreIndependent()
{
    UserButtonDebounce first;
    UserButtonDebounce second;
    first.initialize(false, 0);
    second.initialize(true, 0);
    EXPECT_TRANSITION(first.update(true, 10), None);
    EXPECT_TRANSITION(second.update(true, 10), None);
    EXPECT_TRANSITION(first.update(true, 35), Pressed);
    EXPECT_TRANSITION(second.update(false, 35), None);
    EXPECT_TRANSITION(first.update(true, 60), None);
    EXPECT_TRANSITION(second.update(false, 60), Released);

    first.initialize(true, 100);
    EXPECT_TRANSITION(first.update(true, 100), None);
    EXPECT_TRANSITION(first.update(false, 120), None);
    EXPECT_TRANSITION(first.update(false, 144), None);
    EXPECT_TRANSITION(first.update(false, 145), Released);
}

} // namespace

int main()
{
    releasedStartupAndHeldStatesStayQuiet();
    exactDebounceBoundary();
    twentyMillisecondPolling();
    pressBounceRestartsTheStableWindow();
    releaseBounceDoesNotDuplicateTransitions();
    bootHeldRequiresReleaseBeforeNewPress();
    bootHeldReleaseBounceDoesNotRearmEarly();
    debounceSurvivesTickWraparound();
    instancesAndReinitializationAreIndependent();
    std::cout << "USER_BUTTON debounce: 9 groups, " << checks
              << " checks passed\n";
    return EXIT_SUCCESS;
}
