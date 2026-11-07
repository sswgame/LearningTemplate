#include "pch.h"

#include "Engine/Presentation/ISoundPlayer.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    // 구현(오디오 서비스의 `AudioSoundPlayer`)이 범위에 멤버로 건다. 게임 모듈도 읽는다(`GameSound`).
    SW_ENGINE_SERVICE( ISoundPlayer, GameVisible );
} // namespace sw
