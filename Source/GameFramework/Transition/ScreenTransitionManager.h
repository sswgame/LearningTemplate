/**
 * @file ScreenTransitionManager.h
 * @brief 화면 페이드 인/아웃 및 씬 전환 시퀀스(FadeOut -> OnExecute -> FadeIn) FSM 관리자
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) FadeService — 화면 페이드 아웃/인 알파 보간 서비스
	// ------------------------------------------------------------------------------
	/** @brief 화면 페이드 아웃/인 단계 */
	enum class FadePhase : uint8
	{
		Idle = 0,
		FadingOut,
		HoldBlack,
		FadingIn
	};

	/** @brief 화면 페이드 아웃/인 알파를 갱신합니다. */
	class SW_GF_API FadeService
	{
	public:
		/** @brief Idle·알파 0으로 시작합니다. */
		FadeService();

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
		/** @brief 오버레이 알파를 반환합니다 (0 = 투명, 1 = 완전 검정). */
		float32 getOverlayAlpha() const { return _alpha; }

	private:
		float32				   _duration; ///< 현재 구간 길이(초)
		float32				   _elapsed;
		float32				   _alpha; ///< 0=투명, 1=검정
		FadePhase			   _phase;
		uint8				   _bFinished : 1;
		[[maybe_unused]] uint8 _reserved  : 7;
	};

	// ------------------------------------------------------------------------------
	// 2) TransitionCallbacks — 전환 시 입력 잠금/복원 및 커스텀 훅 콜백
	// ------------------------------------------------------------------------------
	/** @brief 씬/화면 전환 시 호출되는 콜백 모음 */
	struct TransitionCallbacks
	{
		Delegate<void( bool bEnable )> setPlayerInputEnabled; ///< 전환 중 입력 잠금/복원
		Delegate<void()>			   onTransitionStarted;	  ///< 전환 시작 훅
		Delegate<void()>			   onTransitionFinished;  ///< 전환 완료 훅
	};

	// ------------------------------------------------------------------------------
	// 3) ScreenTransitionManager — 페이드 효과와 결합된 범용 화면 전환 관리자
	// ------------------------------------------------------------------------------
	/** @brief 화면 페이드와 연동하여 씬 전환 시퀀스(FadeOut -> Execute -> FadeIn)를 일원화 관리합니다. */
	class SW_GF_API ScreenTransitionManager
	{
	public:
		/** @brief 전환 단계 (None -> FadeOut -> Loading -> FadeIn -> None) */
		enum class Phase : uint8
		{
			None = 0,
			FadeOut,
			Loading,
			FadeIn
		};

		/** @brief 초기 상태로 생성합니다. */
		ScreenTransitionManager();

		/** @brief 범용 전환 시퀀스(FadeOut -> onExecute 콜백 실행 -> FadeIn)를 시작합니다. */
		void beginTransition( Delegate<void()> onExecute, float32 fadeOutDuration = 0.35f, float32 fadeInDuration = 0.35f );

		/** @brief 전환 FSM 및 페이드 알파를 갱신합니다. */
		void update( float32 deltaTime );
		/** @brief 전환 상태를 초기화합니다. */
		void reset();

		/** @brief 전환 또는 페이드가 진행 중인지 반환합니다. */
		bool isBusy() const { return _phase != Phase::None || _fade.isBusy(); }
		/** @brief 현재 전환 단계를 반환합니다. */
		Phase getPhase() const { return _phase; }
		/** @brief 페이드 서비스를 반환합니다. */
		FadeService& fade() { return _fade; }
		/** @brief 페이드 서비스를 반환합니다. */
		const FadeService& fade() const { return _fade; }

		/** @brief 전환 콜백들을 설정합니다. */
		void setCallbacks( TransitionCallbacks callbacks ) { _callbacks = std::move( callbacks ); }

	private:
		FadeService			   _fade;
		TransitionCallbacks	   _callbacks;
		Delegate<void()>	   _pendingAction;
		float32				   _pendingFadeInDuration;
		Phase				   _phase;
		[[maybe_unused]] uint8 _arrReserved[7];
	};
} // namespace sw
