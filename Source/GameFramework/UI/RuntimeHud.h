/**
 * @file RuntimeHud.h
 * @brief ImGui가 아닌 런타임 HUD 상태 (게이지 채움 + 대사 한 줄 + 화면 사각형 앵커)
 */
#pragma once
#include "GameFramework/GameFrameworkExports.h"

#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) 레이아웃 — NDC 게이지 / 화면 사각형
	// ------------------------------------------------------------------------------
	/** @brief 정규화 화면 좌표의 HP·경험치·PP 게이지 */
	struct HudGauge
	{
		float32 _fill{ 1.0f }; ///< 0~1 채움 (머티리얼 상수 버퍼와 동일)
		float32 _x{ 0.05f };   ///< 왼쪽 NDC
		float32 _y{ 0.05f };   ///< 위쪽 NDC
		float32 _w{ 0.28f };   ///< 너비
		float32 _h{ 0.04f };   ///< 높이
	};

	/** @brief HUD가 붙는 화면 사각형 (NDC) */
	struct ScreenRect
	{
		float32 _x{ 0.0f }; ///< 왼쪽
		float32 _y{ 0.0f }; ///< 위쪽
		float32 _w{ 1.0f }; ///< 너비
		float32 _h{ 1.0f }; ///< 높이
	};

	// ------------------------------------------------------------------------------
	// 2) RuntimeHud — 게이지·대사·페이드, 에디터 Game View로 스냅샷
	// ------------------------------------------------------------------------------
	/** @brief 런타임 HUD 상태 (ImGui가 아님) */
	class SW_GF_API RuntimeHud
	{
	public:
		/** @brief 기본 앵커·게이지 채움 1로 시작합니다. */
		RuntimeHud();

		/** @brief 화면 사각형 앵커를 설정합니다. */
		void setScreenRect( float32 x, float32 y, float32 w, float32 h );
		/** @brief 대사 한 줄을 설정합니다. */
		void setDialogue( string_view line );
		/** @brief 대사를 지웁니다. */
		void clearDialogue();
		/** @brief 턴제 전투 HUD 게이지를 설정합니다. */
		void setBattleGauges( float32 playerHpFill, float32 foeHpFill, float32 expFill, float32 ppFill );
		/** @brief 액션 HUD: 플레이어 HP / 보스 HP / 대시 쿨다운(pp 슬롯). */
		void setActionGauges( float32 playerHpFill, float32 bossHpFill, float32 dashFill );
		/** @brief 페이드 오버레이 알파를 설정합니다. */
		void setFadeOverlay( float32 alpha );
		/** @brief HUD 표시 여부를 설정합니다. */
		void setVisible( bool v ) { _bVisible = v ? 1 : 0; }

		/** @brief HUD 표시 여부를 반환합니다. */
		bool isVisible() const { return _bVisible != 0; }
		/** @brief 화면 사각형을 반환합니다. */
		const ScreenRect& getScreenRect() const { return _screen; }
		/** @brief 현재 대사 한 줄을 반환합니다. */
		const string& getDialogue() const { return _dialogue; }
		/** @brief 플레이어 HP 게이지를 반환합니다. */
		const HudGauge& getPlayerHp() const { return _playerHp; }
		/** @brief 적 HP 게이지를 반환합니다. */
		const HudGauge& getFoeHp() const { return _foeHp; }
		/** @brief 경험치 게이지를 반환합니다. */
		const HudGauge& getExp() const { return _exp; }
		/** @brief PP/대시 게이지를 반환합니다. */
		const HudGauge& getPp() const { return _pp; }
		/** @brief 페이드 알파를 반환합니다. */
		float32 getFadeAlpha() const { return _fadeAlpha; }

		/** @brief 에디터 Game View 오버레이용 DebugOverlayState에 게시합니다. */
		void publishSnapshot( bool actionMode = false ) const;
		/** @brief HUD 스냅샷을 로그합니다. */
		void logSnapshot() const;

	private:
		ScreenRect			   _screen{};
		HudGauge			   _playerHp{ 1.0f, 0.05f, 0.82f, 0.30f, 0.04f }; ///< 하단 플레이어 HP
		HudGauge			   _foeHp{ 1.0f, 0.65f, 0.08f, 0.30f, 0.04f };	  ///< 상단 적 HP
		HudGauge			   _exp{ 0.0f, 0.05f, 0.88f, 0.30f, 0.02f };	  ///< 경험치
		HudGauge			   _pp{ 1.0f, 0.05f, 0.78f, 0.18f, 0.03f };		  ///< PP 또는 대시
		string				   _dialogue;									  ///< 대사 한 줄 (없으면 빈 문자열)
		float32				   _fadeAlpha{ 0.0f };							  ///< 0=투명, 1=검정
		uint8				   _bVisible : 1;
		[[maybe_unused]] uint8 _reserved : 7;
	};
} // namespace sw
