/**
 * @file TransitionOrchestrator.h
 * @brief 페이드와 대기 중인 워프/전투/복귀 전환 FSM을 소유합니다
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
	// 1) FadeService — 화면 검정 핸드셰이크
	//    오케스트레이터가 타이밍을 소유, HUD는 알파만 그림
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
	// 2) TransitionCallbacks — 맵 로드 / 전투 / 입력 잠금
	//    오케스트레이터가 타이밍, SWGame이 월드 데이터
	// ------------------------------------------------------------------------------
	/** @brief SWGame 월드 상태 콜백 (맵 로드 / 전투 / 복귀) */
	struct TransitionCallbacks
	{
		Delegate<bool( string_view mapPath, int32 spawnX, int32 spawnY )> loadMap;				 ///< 맵+스폰 로드
		Delegate<void()>												  startBattle;			 ///< 전투 룸/턴제 진입
		Delegate<void()>												  finishBattleReturn;	 ///< 오버월드 복귀
		Delegate<void( bool bEnable )>									  setPlayerInputEnabled; ///< 전환 중 입력 잠금
	};

	// ------------------------------------------------------------------------------
	// 3) TransitionOrchestrator — 워프/전투/복귀 페이드 FSM
	// ------------------------------------------------------------------------------
	/** @brief 워프·전투·복귀 전환을 페이드와 맞춰 진행합니다. */
	class SW_GF_API TransitionOrchestrator
	{
	public:
		/** @brief 전환 페이즈 (페이드 아웃 → 로드 → 페이드 인) */
		enum class Phase : uint8
		{
			None = 0,
			WarpFadeOut,
			WarpLoad,
			WarpFadeIn,
			BattleFadeOut,
			BattleLoad,
			BattleFadeIn,
			ReturnFadeOut,
			ReturnLoad,
			ReturnFadeIn
		};

		/** @brief 페이즈 None, 대기 워프 없음으로 시작합니다. */
		TransitionOrchestrator();

		/** @brief 워프 전환을 시작합니다. */
		void beginWarp( string_view mapPath, int32 spawnX, int32 spawnY );
		/** @brief 전투 전환을 시작합니다. */
		void beginBattle();
		/** @brief 오버월드 복귀 전환을 시작합니다. */
		void beginReturn();
		/** @brief 전환 FSM과 페이드를 갱신합니다. */
		void update( float32 deltaTime );
		/** @brief 전환 상태와 대기 워프를 비웁니다. */
		void reset();

		/** @brief 전환 또는 페이드가 진행 중인지 반환합니다. */
		bool isBusy() const { return _phase != Phase::None || _fade.isBusy(); }
		/** @brief 현재 전환 페이즈를 반환합니다. */
		Phase getPhase() const { return _phase; }
		/** @brief 페이드 서비스를 반환합니다. */
		FadeService& fade() { return _fade; }
		/** @brief 페이드 서비스를 반환합니다. */
		const FadeService& fade() const { return _fade; }

		/** @brief 월드 상태 콜백을 설정합니다. */
		void setCallbacks( TransitionCallbacks callbacks ) { _callbacks = std::move( callbacks ); }

	private:
		FadeService			   _fade;
		TransitionCallbacks	   _callbacks;
		string				   _pendingWarpMap; ///< beginWarp가 넣어 둔 대상 맵
		int32				   _pendingWarpX;
		int32				   _pendingWarpY;
		Phase				   _phase;
		[[maybe_unused]] uint8 _arrReserved[7];
	};
} // namespace sw
