#ifndef USER_BUTTON_POLICY_H
#define USER_BUTTON_POLICY_H

#include "user_button_debounce.h"

#include <cstdint>

enum class UserButtonAction { None, Stop, Replay };

// Optional observation of the existing filter/policy; no second debounce.
struct UserButtonPolicyTrace
{
    UserButtonTransition transition = UserButtonTransition::None;
    bool selectionConsumed = false;
    bool consumedUntilRelease = false;
};

// PollButtonsTask owns this input policy. Transport, confirmed song identity
// and published player state remain with the existing player controller.
class UserButtonPolicy
{
public:
    // Logical true means the active-low input is LOW. Preserve the verified
    // filter's boot-held behavior without manufacturing a press transition.
    void initialize(bool pressed, uint32_t now)
    {
        button.initialize(pressed, now);
        consumedUntilRelease = pressed;
    }

    UserButtonAction update(bool pressed, uint32_t now,
                            bool selectionPriority, bool playbackActive,
                            bool hasConfirmedSong, UserButtonPolicyTrace *trace = nullptr)
    {
        bool selectionConsumed = false;
        if (selectionPriority && pressed && !consumedUntilRelease)
        {
            // Consume the gesture at its raw onset, even if selection ends
            // before the press would pass debounce. Seed the same verified
            // filter as held so only its stable RELEASED edge can rearm it.
            // This also handles a suppressed pulse shorter than DebounceMs.
            button.initialize(true, now);
            consumedUntilRelease = true;
            selectionConsumed = true;
        }

        const UserButtonTransition transition = button.update(pressed, now);
        if (transition == UserButtonTransition::Released)
            consumedUntilRelease = false;

        if (trace != nullptr)
        {
            trace->transition = transition;
            trace->selectionConsumed = selectionConsumed;
            trace->consumedUntilRelease = consumedUntilRelease;
        }

        if (selectionPriority || consumedUntilRelease ||
            transition != UserButtonTransition::Pressed)
            return UserButtonAction::None;

        if (playbackActive)
            return UserButtonAction::Stop;

        return hasConfirmedSong ? UserButtonAction::Replay
                                : UserButtonAction::None;
    }

private:
    UserButtonDebounce button;
    bool consumedUntilRelease = false;
};

#endif
