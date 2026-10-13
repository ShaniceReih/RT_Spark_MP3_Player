#ifndef USER_BUTTON_DEBOUNCE_H
#define USER_BUTTON_DEBOUNCE_H

#include <cstdint>

enum class UserButtonTransition { None, Pressed, Released };

// Smoke-test input filter only. Logical true means the active-low pin is LOW.
class UserButtonDebounce
{
public:
    static constexpr uint32_t DebounceMs = 25;

    // Seed the observed boot state without generating a transition. A held
    // boot button must reach a stable release before a new press can occur.
    void initialize(bool pressed, uint32_t now)
    {
        stablePressed = candidatePressed = pressed;
        candidateSince = now;
    }

    UserButtonTransition update(bool pressed, uint32_t now)
    {
        if (pressed != candidatePressed)
        {
            candidatePressed = pressed;
            candidateSince = now;
        }
        if (candidatePressed == stablePressed || now - candidateSince < DebounceMs)
            return UserButtonTransition::None;

        stablePressed = candidatePressed;
        return stablePressed ? UserButtonTransition::Pressed
                             : UserButtonTransition::Released;
    }

private:
    bool stablePressed = false;
    bool candidatePressed = false;
    uint32_t candidateSince = 0;
};

#endif
