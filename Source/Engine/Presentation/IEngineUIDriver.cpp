#include "pch.h"

#include "Engine/Presentation/IEngineUIDriver.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    // 구현(`UISystem`)이 범위에 인터페이스로 건다. 호스트(모듈 관리자)도 읽는다.
    SW_ENGINE_SERVICE( IEngineUIDriver, GameVisible );
} // namespace sw
