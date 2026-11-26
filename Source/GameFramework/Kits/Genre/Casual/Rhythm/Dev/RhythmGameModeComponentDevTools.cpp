/**
 * @file RhythmGameModeComponentDevTools.cpp
 * @brief `RhythmGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/Archive.h"
#include "Engine/Serialization/Serializer/StateIO.h"

#include "GameFramework/Kits/Genre/Casual/Rhythm/Component/RhythmGameModeComponent.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( rhythmState, "Rhythm.State", "Song state (0 playing, 1 cleared, 2 failed)", RhythmGameModeComponent, &RhythmGameModeComponent::getState );
    SW_AUTOMATION_GETTER_WATCH( rhythmMusicTime, "Rhythm.MusicTime", "Music time the song is judged at (s)", RhythmGameModeComponent, &RhythmGameModeComponent::getMusicTime );
    SW_AUTOMATION_GETTER_WATCH( rhythmJudged, "Rhythm.Judged", "Judgments so far (long notes count twice)", RhythmGameModeComponent, &RhythmGameModeComponent::getJudged );
    SW_AUTOMATION_GETTER_WATCH( rhythmMissed, "Rhythm.Missed", "Missed judgments", RhythmGameModeComponent, &RhythmGameModeComponent::getMissed );
    SW_AUTOMATION_GETTER_WATCH( rhythmMaxCombo, "Rhythm.MaxCombo", "Longest combo", RhythmGameModeComponent, &RhythmGameModeComponent::getMaxCombo );
    SW_AUTOMATION_GETTER_WATCH( rhythmScore, "Rhythm.Score", "Score", RhythmGameModeComponent, &RhythmGameModeComponent::getScore );
} // namespace sw
