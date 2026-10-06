/**
 * @file GameTimeScale.h
 * @brief 게임 시간 배율(슬로 모션 · 빨리 감기)입니다. 호스트의 프레임 시간이 이 값을 곱해 게임 업데이트 · 씬 틱 · 고정 스텝으로 흐릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct GameTimeScale
     * @brief 게임 시간 배율입니다. 값은 전역 변수 `gv_timeScale` 하나에 있습니다(에디터 툴바 · 전역 변수 패널 · 콘솔 `timescale` 이 같은 값을 씁니다).
     * @details 0 이면 게임 시간이 멈춥니다(에디터 UI 와 에디터 카메라는 자기 시간으로 계속 돕니다). 범위 밖은 `kMinScale` · `kMaxScale` 로 자릅니다.
     */
    struct SW_API GameTimeScale
    {
        static constexpr float32 kMinScale = 0.0f;
        static constexpr float32 kMaxScale = 16.0f;

        /** @brief 지금 배율입니다(범위로 자른 값). 정지 요청이 하나라도 걸려 있으면 0 입니다. */
        static float32 get();
        /** @brief 배율을 정합니다(범위로 자릅니다). 정지 요청과 따로 남습니다 — 요청이 모두 풀리면 이 값으로 돌아갑니다. */
        static void set( float32 scale );
        /**
         * @brief 게임 시간을 멈추는 요청을 하나 겁니다(일시정지 메뉴 화면 — `UiScreenDesc::_bPausesGame`). 요청은 쌓이고, 하나라도 있으면 멈춥니다.
         * @details 건 쪽이 `removePauseRequest` 로 꼭 풉니다. `gv_timeScale` 은 바꾸지 않습니다(에디터 · 콘솔이 정한 배율이 남는다).
         */
        static void addPauseRequest();
        /** @brief `addPauseRequest` 로 건 요청을 하나 풉니다. */
        static void removePauseRequest();
        /** @brief 지금 걸린 정지 요청 수입니다. */
        static uint32 getPauseRequestCount();
        /**
         * @brief 호스트가 이번 프레임의 실제 경과(배율 · 정지와 무관, 최대 델타로 자름)를 적습니다(App 루프 — `FrameTime::_unscaledDeltaTime`).
         * @details 게임이 멈춰도 도는 것 — 런타임 UI(정지 메뉴의 애니메이션 · 탐색 반복) · 화면 전환 페이드 · 로딩 화면 — 이 읽습니다(유니티 `Time.unscaledDeltaTime`).
         */
        static void setUnscaledDeltaTime( float32 deltaSeconds );
        /** @brief 이번 프레임의 실제 경과입니다. 호스트가 적은 적이 없으면(시험 하니스 · 다른 호스트) @p fallbackDeltaSeconds 입니다. */
        static float32 getUnscaledDeltaTime( float32 fallbackDeltaSeconds );
    };
} // namespace sw
