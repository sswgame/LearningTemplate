#include "pch.h"

#include "Engine/Presentation/IEngineAudioDriver.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    // 구현(`IAudioService`)이 범위에 인터페이스로 건다. 호스트(엔진 루프)만 읽는다.
    SW_ENGINE_SERVICE( IEngineAudioDriver, HostOnly );
} // namespace sw
