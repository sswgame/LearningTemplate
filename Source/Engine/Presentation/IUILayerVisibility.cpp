#include "pch.h"

#include "Engine/Presentation/IUILayerVisibility.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    // 구현(`UISystem`)이 범위에 인터페이스로 건다. 게임 모듈도 읽는다.
    SW_ENGINE_SERVICE( IUILayerVisibility, GameVisible );
} // namespace sw
