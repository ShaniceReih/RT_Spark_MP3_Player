#ifndef USER_BUTTON_PLAYER_H
#define USER_BUTTON_PLAYER_H

#include "player_state.h"
#include "user_button_policy.h"
#include "song.h"

enum class UserButtonPlayerResult { Ignored, Stopped, Replayed, ReplayFailed };

// PollButtonsTask only. The snapshot is its private outgoing control data;
// no shared mutex is held while the existing audio APIs perform transactions.
UserButtonPlayerResult UserButtonPlayer_Apply(UserButtonAction action,
                                             PlayerStateSnapshot &control,
                                             const Song *lastConfirmedSong,
                                             uint32_t now);

#endif
