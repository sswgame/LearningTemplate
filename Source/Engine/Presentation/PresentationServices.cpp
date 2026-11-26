#include "pch.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Presentation/IEngineAudioDriver.h"
#include "Engine/Presentation/IEngineUIDriver.h"
#include "Engine/Presentation/IPlayerNotificationSink.h"
#include "Engine/Presentation/ISoundPlayer.h"
#include "Engine/Presentation/ISubtitleSink.h"
#include "Engine/Presentation/IUILayerVisibility.h"

// 표현 층 인터페이스의 서비스 칸 등록입니다 — 인터페이스 헤더에는 .cpp 가 없어 등록 줄을 한 TU 에 모은다(구현은 위 층에 있다).
namespace sw
{
    // 구현(`AudioSubsystem`)이 범위에 인터페이스로 건다. 호스트(엔진 루프)만 읽는다.
    SW_ENGINE_SUBSYSTEM_SLOT( IEngineAudioDriver, HostOnly );
    // 구현(`UISystem`)이 범위에 인터페이스로 건다. 호스트(모듈 관리자)도 읽는다.
    SW_ENGINE_SUBSYSTEM_SLOT( IEngineUIDriver, GameVisible );
    // 구현(`UINotificationSubsystem`)이 범위에 인터페이스로 건다. 게임 모듈도 읽는다.
    SW_ENGINE_SUBSYSTEM_SLOT( IPlayerNotificationSink, GameVisible );
    // 구현(오디오 서비스의 `AudioSoundPlayer`)이 범위에 멤버로 건다. 게임 모듈도 읽는다(`GameSound`).
    SW_ENGINE_SUBSYSTEM_SLOT( ISoundPlayer, GameVisible );
    // 구현(`UISubtitleSubsystem`)이 범위에 인터페이스로 건다. 게임 모듈도 읽는다.
    SW_ENGINE_SUBSYSTEM_SLOT( ISubtitleSink, GameVisible );
    // 구현(`UISystem`)이 범위에 인터페이스로 건다. 게임 모듈도 읽는다.
    SW_ENGINE_SUBSYSTEM_SLOT( IUILayerVisibility, GameVisible );
} // namespace sw
