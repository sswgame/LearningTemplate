/**
 * @file EditorPlaySession.h
 * @brief 에디터 플레이(PIE) 세션 및 씬 스냅샷/복구 관리자
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw::editor
{
    enum class PlaySessionState : uint8
    {
        Stopped = 0,
        Playing,
        Paused
    };

    /**
     * @class EditorPlaySession
     * @brief 에디터 Play-In-Editor (PIE) 시뮬레이션 수명주기 및 씬 롤백을 관리합니다.
     */
    class EditorPlaySession
    {
    public:
        /** @brief 현재 플레이 세션 상태를 반환합니다. */
        static PlaySessionState getState();
        /** @brief 플레이 중인지 여부를 반환합니다. Step 대기 중이면 true입니다. */
        static bool isPlaying();
        /** @brief 일시정지 상태인지 여부를 반환합니다. */
        static bool isPaused();
        /** @brief 정지(편집 모드) 상태인지 여부를 반환합니다. */
        static bool isStopped();
        /** @brief 이번 프레임에 Step이 예약되어 있는지 반환합니다. */
        static bool hasPendingStep();

        /** @brief 플레이 세션 상태를 변경합니다 (스냅샷 캡처 및 롤백 복구 처리). */
        static void setState( PlaySessionState state );
        /** @brief 시뮬레이션을 시작합니다. */
        static void play() { setState( PlaySessionState::Playing ); }
        /** @brief 시뮬레이션을 일시정지합니다. */
        static void pause() { setState( PlaySessionState::Paused ); }
        /** @brief 시뮬레이션을 정지하고 씬을 롤백 복구합니다. */
        static void stop() { setState( PlaySessionState::Stopped ); }
        /** @brief 한 프레임만 시뮬레이션한 뒤 일시정지합니다. */
        static void stepOnce();
        /** @brief 예약된 Step을 소비하고 일시정지로 되돌립니다. */
        static void consumePendingStep();
    };
} // namespace sw::editor
