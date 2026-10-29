/**
 * @file IntentReplayBar.h
 * @brief 의도 재생의 되감기 막대입니다 — 재생 틱을 슬라이더로 보이고 끌어 되감으며, 일시정지 · 1 초 되감기 · 닫기 버튼을 받습니다.
 * @details 화면 문서(`Content/common/ui/replaybar.ui.xml` 모양)를 UI 시스템으로 열고, 되감기 세션(`IntentRewindSession`)을 부립니다. 막대는 세계를 돌리지 않는다:
 *          호스트가 프레임마다 `update` 를 부른 뒤 `shouldHostStep` 이 참일 때만 자기 틱을 돌린다(키 찍기 `captureIfDue` 포함). 되감는 중에는 막대가 `stepSeek` 으로
 *          프레임마다 `seekTicksPerFrame` 틱씩 나눠 돌고, 그동안 `shouldHostStep` 은 거짓이라 화면은 멈춘 그림이다.
 *          막대 문서의 이름(`Scrub` · `TimeText` · `PlayPauseLabel`)과 명령(`Replay.StepBack` · `Replay.TogglePause` · `Replay.Close`)이 계약이다 — 없으면 열기가 오류로 멈춘다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/UI/Screen/UIScreenTypes.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class IntentRewindSession;
    class UISystem;
    class Widget;

    /**
     * @class IntentReplayBar
     * @brief 화면 하나와 되감기 세션을 잇는 막대입니다. 세션과 UI 시스템은 막대보다 오래 살아야 합니다. 게임 스레드에서만 씁니다.
     */
    class SW_GF_API IntentReplayBar
    {
    public:
        IntentReplayBar();
        ~IntentReplayBar();
        IntentReplayBar( const IntentReplayBar& )            = delete;
        IntentReplayBar& operator=( const IntentReplayBar& ) = delete;

        /**
         * @brief 문서 @p documentPath 로 막대를 엽니다. 이미 열려 있으면 먼저 닫습니다.
         * @param tickSeconds 한 틱의 시간(초) — 시각 글과 1 초 되감기를 계산한다
         * @param seekTicksPerFrame 되감는 동안 프레임마다 돌 틱 수(1 이상)
         * @return 문서를 열지 못했거나 필요한 위젯 · 인자가 없으면 false 이고 오류를 남깁니다(막대는 닫힌 채).
         */
        [[nodiscard]] bool open( UISystem& ui, string_view documentPath, IntentRewindSession& session, float32 tickSeconds, uint32 seekTicksPerFrame );
        /** @brief 막대를 닫습니다. 열려 있지 않으면 아무 일도 없습니다. */
        void close();
        /**
         * @brief 프레임마다 부릅니다 — 되감는 중이면 `stepSeek` 으로 돌고, 슬라이더 범위와 값 · 시각 글을 맞춥니다. 사용자가 화면을 닫았으면(뒤로 가기) 막대도 닫힌 것으로 칩니다.
         */
        void update();

        bool isOpen() const { return _bOpen != SW_FALSE; }
        bool isPaused() const { return _bPaused != SW_FALSE; }
        /** @brief 호스트가 이번 프레임에 자기 틱을 돌려야 하면 true 입니다 — 열려 있지 않거나(막대 없이 평소처럼), 일시정지도 되감기도 아닐 때입니다. */
        bool shouldHostStep() const;

    private:
        /** @brief 화면의 위젯이 낸 명령을 받습니다. */
        void onCommand( const hashed_string& command, Widget& source );
        /** @brief 사용자가 슬라이더를 끌어 바꾼 값을 되감기 목표로 삼습니다(`update` 가 맞춘 값은 건너뛴다). */
        void onScrub( float32 value );
        void updatePauseLabel();

        UISystem*            _pUI;
        IntentRewindSession* _pSession;
        Subscription         _scrubSubscription;         ///< 슬라이더 값 변경 구독. 화면이 먼저 닫혀 슬라이더가 사라지면 무효가 된다
        Subscription         _arrCommandRegistration[3]; ///< 화면 명령(뒤로 · 정지 · 닫기) 등록 구독. 화면이 먼저 닫히면 무효가 된다
        float32              _tickSeconds;
        float32              _shownMin;
        float32              _shownMax;
        uint32               _furthestTick;
        uint32               _seekTicksPerFrame;
        UIScreenHandle       _screen;
        uint8                _bOpen;
        uint8                _bPaused;
        uint8                _bSyncing; ///< `update` 가 슬라이더 값을 쓰는 동안 참 — 그 알림을 사용자 입력으로 보지 않는다
    };
} // namespace sw
