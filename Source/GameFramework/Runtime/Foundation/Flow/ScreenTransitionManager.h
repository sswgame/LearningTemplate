/**
 * @file ScreenTransitionManager.h
 * @brief 화면 페이드 인/아웃과 씬 전환 시퀀스(FadeOut → OnExecute → FadeIn) FSM 관리자입니다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/GameInstanceSubsystem.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) ScreenFade: 화면 페이드 아웃/인 알파 보간 서비스
    // ------------------------------------------------------------------------------
    /** @brief 화면 페이드 아웃 · 인 단계입니다. */
    enum class FadePhase : uint8
    {
        Idle = 0,
        FadingOut,
        HoldBlack,
        FadingIn
    };

    /** @brief 화면 페이드 아웃 · 인 알파를 갱신합니다. */
    class SW_GF_API ScreenFade
    {
    public:
        /** @brief Idle · 알파 0 으로 시작합니다. */
        ScreenFade();

        /** @brief 페이드 아웃을 시작합니다. */
        void beginFadeOut( float32 duration = 0.35f );
        /** @brief 페이드 인을 시작합니다. */
        void beginFadeIn( float32 duration = 0.35f );
        /** @brief 페이드 알파와 페이즈를 갱신합니다. */
        void update( float32 deltaTime );

        /** @brief 현재 페이드가 끝났는지 반환합니다. */
        bool isFinished() const;
        /** @brief 페이드가 진행 중인지 반환합니다. */
        bool isBusy() const { return _phase != FadePhase::Idle; }
        /** @brief 현재 페이드 페이즈를 반환합니다. */
        FadePhase getPhase() const { return _phase; }
        /** @brief 오버레이 알파를 반환합니다(0 = 투명, 1 = 완전 검정). */
        float32 getOverlayAlpha() const { return _alpha; }

    private:
        float32                _duration; ///< 현재 구간 길이(초)
        float32                _elapsed;
        float32                _alpha; ///< 0=투명, 1=검정
        FadePhase              _phase;
        uint8                  _bFinished : 1;
        [[maybe_unused]] uint8 _reserved  : 7;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) ScreenTransitionManager: 페이드 효과와 결합된 범용 화면 전환 관리자
    // ------------------------------------------------------------------------------
    /** @brief 화면 페이드와 연동해 씬 전환 시퀀스(FadeOut → Execute → FadeIn)를 한곳에서 관리합니다. */
    class SW_GF_API ScreenTransitionManager final : public GameInstanceSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "ScreenTransitionManager";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        /** @brief 게임 인스턴스가 내려갈 때 전환 상태를 지웁니다. */
        void shutdownSubsystem() override { reset(); }

        /** @brief 전환 단계입니다(None → FadeOut → Loading → FadeIn → None). */
        enum class Phase : uint8
        {
            None = 0,
            FadeOut,
            Loading,
            FadeIn
        };

        /** @brief 초기 상태로 만듭니다. */
        ScreenTransitionManager();

        /**
         * @brief 범용 전환 시퀀스(FadeOut → onExecute 콜백 실행 → FadeIn)를 시작합니다.
         * @details 어두워지는 중(`Phase::FadeOut`)이면 `AlreadyExists` 로 거절한다 — 받으면 아직 안 부른 앞 액션을 덮어 잃는다(G-13, 언리얼 `SeamlessTravel` 도 진행 중 새 이동을 거절한다).
         *          액션 안(`Phase::Loading`, "다음 맵이 또 전환을 건다")과 밝아지는 중에는 받는다 — 앞 액션은 이미 불렸다.
         */
        [[nodiscard]] expected<void> beginTransition( Delegate<void()> onExecute, float32 fadeOutDuration = 0.35f, float32 fadeInDuration = 0.35f );

        /** @brief 전환 FSM 과 페이드 알파를 갱신합니다. */
        void update( float32 deltaTime );
        /** @brief 전환 상태를 초기화합니다. */
        void reset();

        /** @brief 전환 또는 페이드가 진행 중인지 반환합니다. */
        bool isBusy() const { return _phase != Phase::None || _fade.isBusy(); }
        /** @brief 현재 전환 단계를 반환합니다. */
        Phase getPhase() const { return _phase; }
        /**
         * @brief 전환 밖에서(로딩 화면이 닫혔다) 검은 화면에서 밝힙니다. 전환이 어두워지는 중이면 아무것도 하지 않습니다 — 그 전환이 액션을 부른 뒤 스스로 밝힌다.
         * @details 페이드를 직접 밝히면 어두워지기가 끝나지 않아 전환의 액션이 영영 불리지 않고 다음 전환도 모두 거절된다 — 그래서 고치는 페이드를 내주지 않는다.
         */
        void fadeInFromBlack( float32 duration );
        /** @brief 페이드 서비스를 반환합니다(읽기 전용). */
        const ScreenFade& fade() const { return _fade; }

    private:
        ScreenFade             _fade;
        Delegate<void()>       _pendingAction;
        float32                _pendingFadeInDuration;
        Phase                  _phase;
        [[maybe_unused]] uint8 _arrReserved[7];
    };
} // namespace sw
