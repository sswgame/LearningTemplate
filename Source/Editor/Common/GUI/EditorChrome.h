/**
 * @file EditorChrome.h
 * @brief 에디터 셸의 틀(크롬)입니다. Panel, Section, Toolbar, FloatingBar, Overlay, SearchOverlay 를 제공하며 ImGui 헤더를 포함하지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorColor.h"
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/GUI/EditorTextExtent.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    // ------------------------------------------------------------------------------
    // 1) Panel Flags & Descs
    // ------------------------------------------------------------------------------
    ENUM( Flags )
    enum class EditorPanelFlags : uint8
    {
        None            = 0,
        NoCollapse      = SW_BIT( 0 ),
        MenuBar         = SW_BIT( 1 ),
        NoScrollbar     = SW_BIT( 2 ),
        NoPadding       = SW_BIT( 3 ),
        UnsavedDocument = SW_BIT( 4 )
    };

    // ------------------------------------------------------------------------------
    // 2) Section Flags & Descs
    // ------------------------------------------------------------------------------
    enum class EditorSectionKind : uint8
    {
        Toolbar = 0, ///< 가로 컨트롤 줄(BeginGroup)
        Child        ///< 스크롤 가능한 Child 영역
    };

    ENUM( Flags )
    enum class EditorSectionFlags : uint8
    {
        None                = 0,
        Border              = SW_BIT( 0 ),
        ResizeX             = SW_BIT( 1 ),
        HorizontalScrollbar = SW_BIT( 2 ),
        NoScrollbar         = SW_BIT( 3 ),
        NoScrollWithMouse   = SW_BIT( 4 ),
        FillRemaining       = SW_BIT( 5 ) ///< Child 높이를 아래쪽 한 줄만 남기고 채운다
    };

    /** @brief 섹션을 여는 설정입니다. */
    struct EditorSectionDesc
    {
        const utf8*        _pID{ "##Section" };
        EditorSectionKind  _kind{ EditorSectionKind::Toolbar };
        float2             _childSize{ 0.0f, 0.0f };
        EditorSectionFlags _flags{ EditorSectionFlags::None };
    };
} // namespace sw::editor

namespace sw::editor
{
    // ------------------------------------------------------------------------------
    // 3) FloatingBar Flags & Descs
    // ------------------------------------------------------------------------------
    ENUM( Flags )
    enum class EditorFloatingBarFlags : uint8
    {
        None                    = 0,
        AutoResize              = SW_BIT( 0 ),
        NoMove                  = SW_BIT( 1 ),
        PassThroughWhenDisabled = SW_BIT( 2 )
    };

    /** @brief 플로팅 바를 여는 설정입니다. */
    struct EditorFloatingBarDesc
    {
        const utf8*            _pID;
        float2                 _anchorPos;
        float2                 _pivot;
        float32                _maxWidth; ///< 0 보다 크면 이 너비로 자르고 가로 스크롤로 넘긴다. 호출하는 패널보다 넓어지지 않게 하기 위해서다
        EditorFloatingBarFlags _flags;
        bool                   _bEnabled;

        /** @brief AutoResize, NoMove, PassThroughWhenDisabled 를 합친 기본값입니다. */
        EditorFloatingBarDesc();
    };
} // namespace sw::editor

namespace sw::editor
{
    // ------------------------------------------------------------------------------
    // 4) Overlay Flags & Descs
    // ------------------------------------------------------------------------------
    ENUM( Flags )
    enum class EditorOverlayFlags : uint16
    {
        None               = 0,
        NoTitleBar         = SW_BIT( 0 ),
        NoResize           = SW_BIT( 1 ),
        NoMove             = SW_BIT( 2 ),
        NoInputs           = SW_BIT( 3 ),
        NoNav              = SW_BIT( 4 ),
        AutoResize         = SW_BIT( 5 ),
        NoFocusOnAppearing = SW_BIT( 6 ),
        NoSavedSettings    = SW_BIT( 7 ),
        NoDecoration       = SW_BIT( 8 )
    };

    /** @brief 오버레이를 여는 설정입니다. */
    struct EditorOverlayDesc
    {
        const utf8*        _pID{ "##Overlay" };
        bool*              _pOpen{ nullptr };
        float2             _anchorPos{ 0.0f, 0.0f };
        float2             _pivot{ 0.0f, 0.0f };
        EditorTextExtent   _size{ EditorTextExtent::ofText( 0.0f, 0.0f ) }; ///< 0 이면 그 축은 정하지 않는다(내용 크기)
        EditorOverlayFlags _flags{ EditorOverlayFlags::NoSavedSettings };
        float32            _rounding{ 0.0f };
        float32            _borderSize{ 0.0f };
        float32            _bgAlpha{ -1.0f };
    };
} // namespace sw::editor

namespace sw::editor
{
    // ------------------------------------------------------------------------------
    // 5) Search Overlay: 뷰포트 중앙의 검색 팝업(커맨드 팔레트, 퀵 런처)
    // ------------------------------------------------------------------------------
    /** @brief 검색 오버레이를 여는 설정입니다. */
    struct EditorSearchOverlayDesc
    {
        const utf8*      _pID{ "##SearchOverlay" };
        bool*            _pOpen{ nullptr };
        EditorTextExtent _size{ EditorTextExtent::ofText( 77.0f, 19.0f ) };
        float32          _viewportYFrac{ 0.28f };
        float32          _rounding{ 8.0f };
        float32          _borderSize{ 1.5f };
        float32          _bgAlpha{ 0.96f };     ///< 바탕은 테마 오버레이 색(`getOverlayBgColor`)에 이 알파를 곱한다
        float32          _borderAlpha{ 0.75f }; ///< 테두리는 테마 강조색에 이 알파를 곱한다
        bool*            _pFocusOnOpen{ nullptr };
    };
} // namespace sw::editor

namespace sw::editor
{
    // ------------------------------------------------------------------------------
    // 6) EditorChrome 클래스
    // ------------------------------------------------------------------------------
    class SW_EDITOR_API EditorChrome
    {
    public:
        /** @brief 도킹 패널을 엽니다. false 이면 접혔거나 탭이 가려진 상태입니다. 반환값과 관계없이 endPanel() 을 호출해야 합니다. */
        static bool beginPanel( const utf8* pTitle, bool* pOpen, EditorPanelFlags flags = EditorPanelFlags::None );
        /** @brief beginPanel()과 짝을 이룹니다. */
        static void endPanel();
        /** @brief 다음 패널의 FirstUseEver 크기와 위치를 정합니다. 크기는 `resolveTextExtent` 로 정하고, 위치는 주 뷰포트 가운데입니다. 칸 또는 줄이 0 이하이면 아무것도 하지 않습니다. */
        static void setNextPanelSize( const EditorTextExtent& extent );
        /**
         * @brief 다음 창의 크기를 정합니다(`ImGui::SetNextWindowSize` 를 직접 부르지 않는 입구). 크기는 `resolveTextExtent` 로 정합니다.
         * @param bAlways true 면 매번, false 면 처음 나타날 때(`ImGuiCond_Appearing`)만 정합니다.
         */
        static void setNextWindowExtent( const EditorTextExtent& extent, bool bAlways );
        /** @brief 처음 사용할 때(`ImGuiCond_FirstUseEver`)만 다음 창의 크기를 정합니다. */
        static void setNextWindowExtentFirstUse( const EditorTextExtent& extent );
        /**
         * @brief 글자 칸 · 줄 수를 픽셀로 바꿉니다. 칸 수 × 숫자 "0" 의 폭, 줄 수 × 글자 줄 높이이고 각 축을 주 뷰포트 작업 영역의 90 % 로 자릅니다.
         * @details 작은 창에서도 닫기 단추가 화면 안에 남게 하는 상한이다. 0 이하인 축은 0 을 돌려 정하지 않는다.
         */
        [[nodiscard]] static float2 resolveTextExtent( const EditorTextExtent& extent );
        /** @brief 메인 뷰포트의 위치와 크기를 얻습니다. */
        [[nodiscard]] static bool tryGetMainViewportRect( float2& outPos, float2& outSize );

        /** @brief 섹션을 엽니다. 반환값과 관계없이 endSection() 을 호출해야 합니다. */
        static bool beginSection( const EditorSectionDesc& desc );
        /** @brief beginSection()과 짝을 이룹니다. */
        static void endSection();

        /** @brief 가로 툴바 섹션을 엽니다. 반환값과 관계없이 endToolbar() 를 호출해야 합니다. */
        static bool beginToolbar( const utf8* pID );
        /** @brief beginToolbar()와 짝을 이룹니다. */
        static void endToolbar();

        /** @brief 플로팅 바를 엽니다. 반환값과 관계없이 endFloatingBar() 를 호출해야 합니다. */
        static bool beginFloatingBar( const EditorFloatingBarDesc& desc );
        /** @brief beginFloatingBar()와 짝을 이룹니다. */
        static void endFloatingBar();

        /** @brief 오버레이를 엽니다. 반환값과 관계없이 endOverlay() 를 호출해야 합니다. */
        static bool beginOverlay( const EditorOverlayDesc& desc );
        /** @brief beginOverlay()와 짝을 이룹니다. */
        static void endOverlay();

        /** @brief 뷰포트 중앙 검색 오버레이를 엽니다. 반환값과 관계없이 endSearchOverlay() 를 호출해야 합니다. */
        static bool beginSearchOverlay( const EditorSearchOverlayDesc& desc );
        /** @brief beginSearchOverlay()와 짝을 이룹니다. */
        static void endSearchOverlay();
    };
} // namespace sw::editor
