#include "pch.h"

#include "Editor/Common/Gui/EditorChrome.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Gui/EditorDockLayout.h"
#include "Editor/Common/Gui/EditorThemeUtil.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorChromeInternal
        {
            static constexpr int32   kMaxSectionDepth       = 8;
            static constexpr int32   kMaxOverlayDepth       = 8;
            static constexpr float32 kMaxPanelViewportRatio = 0.9f; ///< 처음 여는 떠 있는 창이 주 뷰포트 작업 영역에서 차지할 수 있는 최대 비율

            static ImGuiWindowFlags toImGuiPanelFlags( EditorPanelFlags flags )
            {
                ImGuiWindowFlags imguiFlags = 0;
                if ( ( flags & EditorPanelFlags::NoCollapse ) != EditorPanelFlags::None )
                    imguiFlags |= ImGuiWindowFlags_NoCollapse;
                if ( ( flags & EditorPanelFlags::MenuBar ) != EditorPanelFlags::None )
                    imguiFlags |= ImGuiWindowFlags_MenuBar;
                if ( ( flags & EditorPanelFlags::NoScrollbar ) != EditorPanelFlags::None )
                    imguiFlags |= ImGuiWindowFlags_NoScrollbar;
                if ( ( flags & EditorPanelFlags::UnsavedDocument ) != EditorPanelFlags::None )
                    imguiFlags |= ImGuiWindowFlags_UnsavedDocument;
                return imguiFlags;
            }

            static ImGuiWindowFlags toImGuiOverlayFlags( EditorOverlayFlags flags )
            {
                ImGuiWindowFlags imguiFlags = 0;
                if ( ( flags & EditorOverlayFlags::NoTitleBar ) != EditorOverlayFlags::None )
                    imguiFlags |= ImGuiWindowFlags_NoTitleBar;
                if ( ( flags & EditorOverlayFlags::NoResize ) != EditorOverlayFlags::None )
                    imguiFlags |= ImGuiWindowFlags_NoResize;
                if ( ( flags & EditorOverlayFlags::NoMove ) != EditorOverlayFlags::None )
                    imguiFlags |= ImGuiWindowFlags_NoMove;
                if ( ( flags & EditorOverlayFlags::NoInputs ) != EditorOverlayFlags::None )
                    imguiFlags |= ImGuiWindowFlags_NoInputs;
                if ( ( flags & EditorOverlayFlags::NoNav ) != EditorOverlayFlags::None )
                    imguiFlags |= ImGuiWindowFlags_NoNav;
                if ( ( flags & EditorOverlayFlags::AutoResize ) != EditorOverlayFlags::None )
                    imguiFlags |= ImGuiWindowFlags_AlwaysAutoResize;
                if ( ( flags & EditorOverlayFlags::NoFocusOnAppearing ) != EditorOverlayFlags::None )
                    imguiFlags |= ImGuiWindowFlags_NoFocusOnAppearing;
                if ( ( flags & EditorOverlayFlags::NoSavedSettings ) != EditorOverlayFlags::None )
                    imguiFlags |= ImGuiWindowFlags_NoSavedSettings;
                if ( ( flags & EditorOverlayFlags::NoDecoration ) != EditorOverlayFlags::None )
                    imguiFlags |= ImGuiWindowFlags_NoDecoration;
                return imguiFlags;
            }

            static inline thread_local EditorSectionKind s_arrSectionStack[kMaxSectionDepth]{};
            static inline thread_local int32             s_sectionDepth{ 0 };
            static inline thread_local int32             s_floatingBarDisabledDepth{ 0 };
            static inline thread_local int32             s_arrOverlayStyleVars[kMaxOverlayDepth]{};
            static inline thread_local int32             s_overlayDepth{ 0 };

            /**
             * @brief 깊이 상한을 넘겨 **담지 못한** begin 의 수입니다. 두 스택이 각자 하나씩 듭니다.
             * @details 상한을 넘은 begin 은 스택에 담기지 않는데, 그 짝인 end 는 그것을 모르고 **한 칸을 꺼냅니다.** 그 순간부터
             *          모든 짝이 한 칸씩 어긋납니다. 섹션 쪽은 엉뚱한 `kind` 로 닫히고(그룹을 자식으로 닫습니다), 오버레이 쪽은
             *          **틀린 개수로 `PopStyleVar`** 를 불러 그 프레임의 스타일 스택이 통째로 무너집니다. 담지 못한 수를 세어 두면
             *          end 가 그만큼을 먼저 흘려보내 짝이 맞습니다.
             */
            static inline thread_local int32 s_sectionOverflow{ 0 };
            static inline thread_local int32 s_overlayOverflow{ 0 };
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorFloatingBarDesc::EditorFloatingBarDesc()
        : _pID{ "##FloatingBar" }
        , _anchorPos{ 0.0f, 0.0f }
        , _pivot{ 0.5f, 0.0f }
        , _maxWidth{ 0.0f }
        , _flags{ EditorFloatingBarFlags::AutoResize | EditorFloatingBarFlags::NoMove |
                  EditorFloatingBarFlags::PassThroughWhenDisabled }
        , _bEnabled{ true }
    {
    }

    bool EditorChrome::beginPanel( const utf8* pTitle, bool* pOpen, EditorPanelFlags flags )
    {
        if ( pTitle == nullptr )
            return false;

        // 진단 스위치가 켜지면 저장된 레이아웃 없이 전부 떠 있는 창이 된다. ImGui 기본 크기는
        // 내용에 비해 작아 내부 Child 가 한 줄 높이로 눌리고, 그러면 덤프가 "내용 없음" 으로 읽는다.
        // 재는 것이 목적이므로 첫 사용에 넉넉한 크기를 준다(사용자가 줄이면 그대로 따른다).
        if ( EditorDockLayout::isOpeningAllPanels() )
            ImGui::SetNextWindowSize( ImVec2{ 900.0f * EditorThemeUtil::getDpiScale(), 620.0f * EditorThemeUtil::getDpiScale() }, ImGuiCond_FirstUseEver );

        const bool bNoPadding = ( flags & EditorPanelFlags::NoPadding ) != EditorPanelFlags::None;
        if ( bNoPadding )
            ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f } );

        const bool bVisible = ImGui::Begin( pTitle, pOpen, EditorChromeInternal::toImGuiPanelFlags( flags ) );
        if ( bNoPadding )
            ImGui::PopStyleVar();
        return bVisible;
    }

    void EditorChrome::endPanel()
    {
        ImGui::End();
    }

    void EditorChrome::setNextPanelSize( const float2& size )
    {
        if ( size._x <= 0.0f || size._y <= 0.0f )
            return;
        // 크기는 96 DPI 기준값이다 — 글자 · 여백처럼 UI 배율을 곱하고, 작은 창(960×540 · 배율 1.5)에서도 닫기 단추가 화면 안에 남게 작업 영역의 90 % 로 자른다.
        const float32        dpiScale  = EditorThemeUtil::getDpiScale();
        const ImGuiViewport* pViewport = ImGui::GetMainViewport();
        ImVec2               windowSize{ size._x * dpiScale, size._y * dpiScale };
        if ( pViewport != nullptr )
        {
            windowSize.x = MathUtil::min( windowSize.x, pViewport->WorkSize.x * EditorChromeInternal::kMaxPanelViewportRatio );
            windowSize.y = MathUtil::min( windowSize.y, pViewport->WorkSize.y * EditorChromeInternal::kMaxPanelViewportRatio );
            // 떠 있는 창은 모두 같은 자리(왼쪽 위)에 열려 Hierarchy 를 덮었다 — 처음에는 주 뷰포트 가운데에 연다. 도킹된 창에는 둘 다 걸리지 않는다.
            const ImVec2 center{ pViewport->WorkPos.x + pViewport->WorkSize.x * 0.5f, pViewport->WorkPos.y + pViewport->WorkSize.y * 0.5f };
            ImGui::SetNextWindowPos( center, ImGuiCond_FirstUseEver, ImVec2{ 0.5f, 0.5f } );
        }
        ImGui::SetNextWindowSize( windowSize, ImGuiCond_FirstUseEver );
    }

    bool EditorChrome::tryGetMainViewportRect( float2& outPos, float2& outSize )
    {
        const ImGuiViewport* pViewport = ImGui::GetMainViewport();
        if ( pViewport == nullptr )
            return false;

        outPos  = float2{ pViewport->Pos.x, pViewport->Pos.y };
        outSize = float2{ pViewport->Size.x, pViewport->Size.y };
        return true;
    }

    bool EditorChrome::beginSection( const EditorSectionDesc& desc )
    {
        const utf8* pID = desc._pID != nullptr ? desc._pID : "##Section";
        if ( EditorChromeInternal::s_sectionDepth < EditorChromeInternal::kMaxSectionDepth )
        {
            EditorChromeInternal::s_arrSectionStack[EditorChromeInternal::s_sectionDepth] = desc._kind;
            ++EditorChromeInternal::s_sectionDepth;
        }
        else
        {
            // 담지 못했다. 짝인 end 가 이것을 알아야 한 칸씩 어긋나지 않는다.
            ++EditorChromeInternal::s_sectionOverflow;
        }

        if ( desc._kind == EditorSectionKind::Child )
        {
            ImVec2 childSize{ desc._childSize._x, desc._childSize._y };
            if ( ( desc._flags & EditorSectionFlags::FillRemaining ) != EditorSectionFlags::None )
                childSize.y = -ImGui::GetFrameHeightWithSpacing();

            ImGuiChildFlags childFlags = 0;
            if ( ( desc._flags & EditorSectionFlags::Border ) != EditorSectionFlags::None )
                childFlags |= ImGuiChildFlags_Borders;
            if ( ( desc._flags & EditorSectionFlags::ResizeX ) != EditorSectionFlags::None )
                childFlags |= ImGuiChildFlags_ResizeX;

            ImGuiWindowFlags windowFlags = 0;
            if ( ( desc._flags & EditorSectionFlags::HorizontalScrollbar ) != EditorSectionFlags::None )
                windowFlags |= ImGuiWindowFlags_HorizontalScrollbar;
            if ( ( desc._flags & EditorSectionFlags::NoScrollbar ) != EditorSectionFlags::None )
                windowFlags |= ImGuiWindowFlags_NoScrollbar;
            if ( ( desc._flags & EditorSectionFlags::NoScrollWithMouse ) != EditorSectionFlags::None )
                windowFlags |= ImGuiWindowFlags_NoScrollWithMouse;

            return ImGui::BeginChild( pID, childSize, childFlags, windowFlags );
        }

        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 3.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2{ 4.0f, 2.0f } );
        ImGui::BeginGroup();
        return true;
    }

    void EditorChrome::endSection()
    {
        EditorSectionKind kind = EditorSectionKind::Toolbar;
        if ( EditorChromeInternal::s_sectionOverflow > 0 )
        {
            // 담지 못한 begin 의 짝이다. 스택에서 꺼내면 남의 칸을 꺼내게 된다.
            --EditorChromeInternal::s_sectionOverflow;
        }
        else if ( EditorChromeInternal::s_sectionDepth > 0 )
        {
            --EditorChromeInternal::s_sectionDepth;
            kind = EditorChromeInternal::s_arrSectionStack[EditorChromeInternal::s_sectionDepth];
        }

        if ( kind == EditorSectionKind::Child )
        {
            ImGui::EndChild();
            return;
        }

        ImGui::EndGroup();
        ImGui::PopStyleVar( 2 );
    }

    bool EditorChrome::beginFloatingBar( const EditorFloatingBarDesc& desc )
    {
        const utf8* pID = desc._pID != nullptr ? desc._pID : "##FloatingBar";

        ImGui::SetNextWindowPos( ImVec2{ desc._anchorPos._x, desc._anchorPos._y }, ImGuiCond_Always,
                                 ImVec2{ desc._pivot._x, desc._pivot._y } );

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus;
        if ( ( desc._flags & EditorFloatingBarFlags::AutoResize ) != EditorFloatingBarFlags::None )
            flags |= ImGuiWindowFlags_AlwaysAutoResize;
        if ( ( desc._flags & EditorFloatingBarFlags::NoMove ) != EditorFloatingBarFlags::None )
        {
            flags |= ImGuiWindowFlags_NoMove;
            flags |= ImGuiWindowFlags_NoResize;
        }

        // 바는 부르는 패널의 뷰포트에 묶는다. 묶지 않으면 패널 밖으로 넘친 바를 멀티 뷰포트가 자기 OS 창으로 떼어 내 화면 아무 데나 띄운다.
        ImGui::SetNextWindowViewport( ImGui::GetWindowViewport()->ID );
        if ( desc._maxWidth > 0.0f )
        {
            ImGui::SetNextWindowSizeConstraints( ImVec2{ 0.0f, 0.0f }, ImVec2{ desc._maxWidth, MathUtil::kMaxFloat } );
            flags &= ~ImGuiWindowFlags_NoScrollbar;
            flags |= ImGuiWindowFlags_HorizontalScrollbar;
        }

        const bool bPassThrough =
            ( desc._flags & EditorFloatingBarFlags::PassThroughWhenDisabled ) != EditorFloatingBarFlags::None;
        if ( desc._bEnabled == false && bPassThrough )
            flags |= ImGuiWindowFlags_NoInputs;

        const bool bVisible = ImGui::Begin( pID, nullptr, flags );
        if ( bVisible && desc._bEnabled == false )
        {
            ImGui::BeginDisabled();
            ++EditorChromeInternal::s_floatingBarDisabledDepth;
        }
        return bVisible;
    }

    void EditorChrome::endFloatingBar()
    {
        if ( EditorChromeInternal::s_floatingBarDisabledDepth > 0 )
        {
            ImGui::EndDisabled();
            --EditorChromeInternal::s_floatingBarDisabledDepth;
        }
        ImGui::End();
    }

    bool EditorChrome::beginOverlay( const EditorOverlayDesc& desc )
    {
        const utf8* pID = desc._pID != nullptr ? desc._pID : "##Overlay";

        ImGui::SetNextWindowPos( ImVec2{ desc._anchorPos._x, desc._anchorPos._y }, ImGuiCond_Always,
                                 ImVec2{ desc._pivot._x, desc._pivot._y } );
        if ( desc._size._x > 0.0f )
            ImGui::SetNextWindowSize( ImVec2{ desc._size._x, desc._size._y } );
        if ( desc._bgAlpha >= 0.0f )
            ImGui::SetNextWindowBgAlpha( desc._bgAlpha );

        int32 styleVarCount = 0;
        if ( desc._rounding > 0.0f )
        {
            ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, desc._rounding );
            ++styleVarCount;
        }
        if ( desc._borderSize > 0.0f )
        {
            ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, desc._borderSize );
            ++styleVarCount;
        }
        if ( EditorChromeInternal::s_overlayDepth < EditorChromeInternal::kMaxOverlayDepth )
        {
            EditorChromeInternal::s_arrOverlayStyleVars[EditorChromeInternal::s_overlayDepth] = styleVarCount;
            ++EditorChromeInternal::s_overlayDepth;
        }
        else
        {
            // 담지 못한 것은 **여기서 바로 되돌린다.** 아래 `Begin` 뒤의 짝인 end 는 이 개수를 알 길이 없고, 틀린 개수로
            // `PopStyleVar` 를 부르면 스타일 스택이 무너진다.
            if ( styleVarCount > 0 )
                ImGui::PopStyleVar( styleVarCount );
            ++EditorChromeInternal::s_overlayOverflow;
        }

        return ImGui::Begin( pID, desc._pOpen, EditorChromeInternal::toImGuiOverlayFlags( desc._flags ) );
    }

    void EditorChrome::endOverlay()
    {
        ImGui::End();

        int32 styleVarCount = 0;
        if ( EditorChromeInternal::s_overlayOverflow > 0 )
        {
            // 담지 못한 begin 의 짝이다. 그쪽에서 이미 되돌렸으므로 여기서는 아무것도 꺼내지 않는다.
            --EditorChromeInternal::s_overlayOverflow;
        }
        else if ( EditorChromeInternal::s_overlayDepth > 0 )
        {
            --EditorChromeInternal::s_overlayDepth;
            styleVarCount = EditorChromeInternal::s_arrOverlayStyleVars[EditorChromeInternal::s_overlayDepth];
        }
        if ( styleVarCount > 0 )
            ImGui::PopStyleVar( styleVarCount );
    }

    bool EditorChrome::beginToolbar( const utf8* pID )
    {
        EditorSectionDesc desc{};
        desc._pID  = pID != nullptr ? pID : "##Toolbar";
        desc._kind = EditorSectionKind::Toolbar;
        return beginSection( desc );
    }

    void EditorChrome::endToolbar()
    {
        endSection();
    }

    bool EditorChrome::beginSearchOverlay( const EditorSearchOverlayDesc& desc )
    {
        float2 viewportPos{};
        float2 viewportSize{};
        if ( tryGetMainViewportRect( viewportPos, viewportSize ) == false )
        {
            viewportPos  = float2{ 0.0f, 0.0f };
            viewportSize = float2{ 800.0f, 600.0f };
        }

        EditorOverlayDesc overlayDesc{};
        overlayDesc._pID        = desc._pID != nullptr ? desc._pID : "##SearchOverlay";
        overlayDesc._pOpen      = desc._pOpen;
        overlayDesc._anchorPos  = float2{ viewportPos._x + viewportSize._x * 0.5f,
                                         viewportPos._y + viewportSize._y * desc._viewportYFrac };
        overlayDesc._pivot      = float2{ 0.5f, 0.5f };
        overlayDesc._size       = desc._size;
        overlayDesc._rounding   = desc._rounding;
        overlayDesc._borderSize = desc._borderSize;
        overlayDesc._flags      = EditorOverlayFlags::NoTitleBar | EditorOverlayFlags::NoResize |
                             EditorOverlayFlags::NoMove | EditorOverlayFlags::NoSavedSettings;

        ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4{ desc._bgColor._x, desc._bgColor._y, desc._bgColor._z, desc._bgColor._w } );
        ImGui::PushStyleColor( ImGuiCol_Border,
                               ImVec4{ desc._borderColor._x, desc._borderColor._y, desc._borderColor._z, desc._borderColor._w } );

        const bool bVisible = beginOverlay( overlayDesc );
        if ( bVisible == false )
            return false;

        if ( ImGui::IsKeyPressed( ImGuiKey_Escape ) && desc._pOpen != nullptr )
            *desc._pOpen = false;

        if ( desc._pFocusOnOpen != nullptr && *desc._pFocusOnOpen == true )
        {
            ImGui::SetKeyboardFocusHere();
            *desc._pFocusOnOpen = false;
        }
        return true;
    }

    void EditorChrome::endSearchOverlay()
    {
        endOverlay();
        ImGui::PopStyleColor( 2 );
    }
} // namespace sw::editor
