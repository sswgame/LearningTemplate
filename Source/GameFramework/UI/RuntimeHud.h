/**
 * @file RuntimeHud.h
 * @brief 런타임 HUD 상태 (범용 게이지 맵 + 대사 한 줄 + 화면 사각형 앵커)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) 레이아웃 — NDC 게이지 / 화면 사각형
	// ------------------------------------------------------------------------------
	/** @brief 정규화 화면 좌표(NDC)의 게이지 정보 */
	struct HudGauge
	{
		float32 _fill{ 1.0f }; ///< 0~1 채움
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
	// 2) RuntimeHud — 범용 게이지 맵·대사·페이드, 에디터 Game View로 스냅샷
	// ------------------------------------------------------------------------------
	/** @brief 런타임 HUD 상태 관리 */
	class SW_GF_API RuntimeHud
	{
	public:
		/** @brief 기본 앵커로 시작합니다. */
		RuntimeHud();

		/** @brief 화면 사각형 앵커를 설정합니다. */
		void setScreenRect( float32 x, float32 y, float32 w, float32 h );
		/** @brief 대사 한 줄을 설정합니다. */
		void setDialogue( string_view line );
		/** @brief 대사를 지웁니다. */
		void clearDialogue();

		/** @brief 범용 게이지를 등록/갱신합니다. */
		void setGauge( const hashed_string& key, float32 fill, float32 x = 0.05f, float32 y = 0.05f, float32 w = 0.28f, float32 h = 0.04f );
		/** @brief 범용 게이지를 조회합니다. 없으면 nullptr 반환 */
		const HudGauge* getGauge( const hashed_string& key ) const;
		/** @brief 범용 게이지 채움 비율(0..1)을 조회합니다. */
		float32 getGaugeFill( const hashed_string& key, float32 fallback = 0.0f ) const;
		/** @brief 모든 게이지를 비웁니다. */
		void clearGauges();
		/** @brief 모든 게이지 맵을 반환합니다. */
		const unordered_map<hashed_string, HudGauge>& getAllGauges() const { return _mapGauge; }

		/** @brief 페이드 오버레이 알파를 설정합니다. */
		void setFadeOverlay( float32 alpha );
		/** @brief HUD 표시 여부를 설정합니다. */
		void setVisible( bool v ) { _bVisible = v ? SW_TRUE : SW_FALSE; }

		/** @brief HUD 표시 여부를 반환합니다. */
		bool isVisible() const { return _bVisible == SW_TRUE; }
		/** @brief 화면 사각형을 반환합니다. */
		const ScreenRect& getScreenRect() const { return _screen; }
		/** @brief 현재 대사 한 줄을 반환합니다. */
		const string& getDialogue() const { return _dialogue; }
		/** @brief 페이드 알파를 반환합니다. */
		float32 getFadeAlpha() const { return _fadeAlpha; }

		/** @brief 에디터 Game View 오버레이용 DebugOverlayState에 게시합니다. */
		void publishSnapshot( bool actionMode = false ) const;
		/** @brief HUD 스냅샷을 로그합니다. */
		void logSnapshot() const;

	private:
		ScreenRect							   _screen;
		unordered_map<hashed_string, HudGauge> _mapGauge;
		string								   _dialogue;
		float32								   _fadeAlpha;
		uint8								   _bVisible : 1;
		[[maybe_unused]] uint8				   _reserved : 7;
	};
} // namespace sw
