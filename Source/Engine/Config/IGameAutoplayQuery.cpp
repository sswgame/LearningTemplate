#include "pch.h"

#include "Engine/Config/IGameAutoplayQuery.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    // 구현(개발 구성의 `GameAutoplaySubsystem`)이 범위에 인터페이스로 건다. 게임 모듈(게임 모드)도 읽는다.
    SW_ENGINE_SERVICE( IGameAutoplayQuery, GameVisible );
} // namespace sw
