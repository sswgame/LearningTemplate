/**
 * @file Engine/Presentation/IEngineAudioDriver.h
 * @brief 엔진 루프가 오디오를 돌리는 창구입니다 — 엔진 루프는 오디오 헤더를 읽지 않고 이 창구만 부릅니다(`IEngineUIDriver` 와 같은 모양).
 * @details 구현은 `IAudioService` 이고 기동 부품(`StartupParts::_pAudioDriver`)과 엔진 서비스 칸으로 건다. 전용 서버는 오디오 폴더를 짓지 않아 창구가 없고
 *          엔진은 그 단계를 건너뛴다(언리얼 데디케이티드 서버가 오디오 장치를 만들지 않는 자리 — 기억).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ServiceSlot.h"

namespace sw
{
    /** @class IEngineAudioDriver @brief 오디오의 프레임 단계입니다. 게임 스레드에서 부릅니다. */
    class IEngineAudioDriver
    {
    public:
        virtual ~IEngineAudioDriver() = default;

        /** @brief 프레임마다 부릅니다(게임이 멈춰도 — 장치 없는 렌더도 같아야 정지 · 에디터에서 오디오 상태가 움직인다). */
        virtual void update( float32 unscaledDeltaSeconds ) = 0;
    };

    SW_SERVICE_SLOT( IEngineAudioDriver, Optional );
} // namespace sw
