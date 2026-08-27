/**
 * @file EditorChrome.h
 * @brief 에디터 셸 크롬 — Panel / Section / FloatingBar / Overlay (ImGui 헤더 비포함)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
	// ------------------------------------------------------------------------------
	// 1) Panel Flags & Descs
	// ------------------------------------------------------------------------------
	ENUM( Flags )
	enum class EditorPanelFlags : uint32
	{
		None			= 0,
		NoCollapse		= SW_BIT( 0 ),
		MenuBar			= SW_BIT( 1 ),
		NoScrollbar		= SW_BIT( 2 ),
		NoPadding		= SW_BIT( 3 ),
		UnsavedDocument = SW_BIT( 4 )
	};

	// ------------------------------------------------------------------------------
	// 2) Section Flags & Descs
	// ------------------------------------------------------------------------------
	enum class EditorSectionKind : uint8
	{
		Toolbar = 0, ///< 가로 컨트롤 스트립 (BeginGroup)
		Child		 ///< 스크롤 가능한 Child 영역
	};

	ENUM( Flags )
	enum class EditorSectionFlags : uint32
	{
		None				= 0,
		Border				= SW_BIT( 0 ),
		ResizeX				= SW_BIT( 1 ),
		HorizontalScrollbar = SW_BIT( 2 ),
		NoScrollbar			= SW_BIT( 3 ),
		NoScrollWithMouse	= SW_BIT( 4 ),
		FillRemaining		= SW_BIT( 5 ) ///< Child 높이를 하단 한 줄 남기고 채움
	};

	/** @brief 섹션 열기 서술 */
	struct EditorSectionDesc
	{
		const utf8*		   _pId{ "##Section" };
		EditorSectionKind  _kind{ EditorSectionKind::Toolbar };
		float2			   _childSize{ 0.0f, 0.0f };
		EditorSectionFlags _flags{ EditorSectionFlags::None };
	};

	// ------------------------------------------------------------------------------
	// 3) FloatingBar Flags & Descs
	// ------------------------------------------------------------------------------
	ENUM( Flags )
	enum class EditorFloatingBarFlags : uint32
	{
		None					= 0,
		AutoResize				= SW_BIT( 0 ),
		NoMove					= SW_BIT( 1 ),
		PassThroughWhenDisabled = SW_BIT( 2 )
	};

	/** @brief 플로팅 바 열기 서술 */
	struct EditorFloatingBarDesc
	{
		const utf8*			   _pId;
		float2				   _anchorPos;
		float2				   _pivot;
		EditorFloatingBarFlags _flags;
		bool				   _bEnabled;

		/** @brief AutoResize | NoMove | PassThroughWhenDisabled 기본값. */
		EditorFloatingBarDesc();
	};

	// ------------------------------------------------------------------------------
	// 4) Overlay Flags & Descs
	// ------------------------------------------------------------------------------
	ENUM( Flags )
	enum class EditorOverlayFlags : uint32
	{
		None			   = 0,
		NoTitleBar		   = SW_BIT( 0 ),
		NoResize		   = SW_BIT( 1 ),
		NoMove			   = SW_BIT( 2 ),
		NoInputs		   = SW_BIT( 3 ),
		NoNav			   = SW_BIT( 4 ),
		AutoResize		   = SW_BIT( 5 ),
		NoFocusOnAppearing = SW_BIT( 6 ),
		NoSavedSettings	   = SW_BIT( 7 ),
		NoDecoration	   = SW_BIT( 8 )
	};

	/** @brief 오버레이 열기 서술 */
	struct EditorOverlayDesc
	{
		const utf8*		   _pId{ "##Overlay" };
		bool*			   _pOpen{ nullptr };
		float2			   _anchorPos{ 0.0f, 0.0f };
		float2			   _pivot{ 0.0f, 0.0f };
		float2			   _size{ 0.0f, 0.0f };
		EditorOverlayFlags _flags{ EditorOverlayFlags::NoSavedSettings };
		float32			   _rounding{ 0.0f };
		float32			   _borderSize{ 0.0f };
		float32			   _bgAlpha{ -1.0f };
	};

	// ------------------------------------------------------------------------------
	// 5) EditorChrome 클래스
	// ------------------------------------------------------------------------------
	class EditorChrome
	{
	public:
		/** @brief 도킹 패널을 엽니다. false면 접힘/탭 숨김. 항상 endPanel()을 호출합니다. */
		static bool beginPanel( const utf8* pTitle, bool* pOpen, EditorPanelFlags flags = EditorPanelFlags::None );
		/** @brief beginPanel()과 짝을 이룹니다. */
		static void endPanel();
		/** @brief FirstUseEver 크기를 다음 패널에 적용합니다. */
		static void setNextPanelSize( const float2& size );
		/** @brief 메인 뷰포트 위치/크기를 얻습니다. */
		static bool tryGetMainViewportRect( float2& outPos, float2& outSize );

		/** @brief 섹션을 엽니다. beginSection 이후에는 반환값과 관계없이 endSection()을 호출합니다. */
		static bool beginSection( const EditorSectionDesc& desc );
		/** @brief beginSection()과 짝을 이룹니다. */
		static void endSection();

		/** @brief 플로팅 바를 엽니다. 항상 endFloatingBar()를 호출합니다. */
		static bool beginFloatingBar( const EditorFloatingBarDesc& desc );
		/** @brief beginFloatingBar()와 짝을 이룹니다. */
		static void endFloatingBar();

		/** @brief 오버레이를 엽니다. 항상 endOverlay()를 호출합니다. */
		static bool beginOverlay( const EditorOverlayDesc& desc );
		/** @brief beginOverlay()와 짝을 이룹니다. */
		static void endOverlay();
	};

	// ------------------------------------------------------------------------------
	// 편의용 네임스페이스 인라인 포워딩
	// ------------------------------------------------------------------------------
	inline bool beginPanel( const utf8* pTitle, bool* pOpen, EditorPanelFlags flags = EditorPanelFlags::None )
	{
		return EditorChrome::beginPanel( pTitle, pOpen, flags );
	}
	inline void endPanel() { EditorChrome::endPanel(); }
	inline void setNextPanelSize( const float2& size ) { EditorChrome::setNextPanelSize( size ); }
	inline bool tryGetMainViewportRect( float2& outPos, float2& outSize ) { return EditorChrome::tryGetMainViewportRect( outPos, outSize ); }
	inline bool beginSection( const EditorSectionDesc& desc ) { return EditorChrome::beginSection( desc ); }
	inline void endSection() { EditorChrome::endSection(); }
	inline bool beginFloatingBar( const EditorFloatingBarDesc& desc ) { return EditorChrome::beginFloatingBar( desc ); }
	inline void endFloatingBar() { EditorChrome::endFloatingBar(); }
	inline bool beginOverlay( const EditorOverlayDesc& desc ) { return EditorChrome::beginOverlay( desc ); }
	inline void endOverlay() { EditorChrome::endOverlay(); }
} // namespace sw::editor
