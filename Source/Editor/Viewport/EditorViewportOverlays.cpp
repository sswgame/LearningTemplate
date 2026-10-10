#include "pch.h"

#include "Editor/Viewport/EditorViewportOverlays.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportToolbar.h"

#include "Engine/Utility/KeyValueFile.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorViewportOverlaysInternal
        {
            static constexpr const utf8* kFileName     = "ViewportOverlays.ini";
            static constexpr const utf8* kViewPrefix   = "scene";
            static constexpr float32     kSnapDistance = 16.0f; ///< 가장자리에 붙는 거리(배율 1)
            static constexpr float32     kEdgeMargin   = 6.0f;  ///< 가장자리에 붙은 바와 뷰포트 테두리 사이(배율 1)

            /** @brief 바 하나의 고정 정보입니다(제목 · 기본 자리 · 내용). */
            struct BarDesc
            {
                const utf8*       _pID;
                const utf8*       _pTitle;
                EditorOverlayDock _defaultDock;
                float2            _defaultFraction;
            };

            static constexpr BarDesc kArrBar[] = {
                {     "view",      "View",    EditorOverlayDock::Top, float2{ 0.0f, 0.0f }},
                {"transform", "Transform",    EditorOverlayDock::Top, float2{ 0.5f, 0.0f }},
                {  "display",   "Display", EditorOverlayDock::Bottom, float2{ 0.0f, 1.0f }},
                {    "tools",     "Tools", EditorOverlayDock::Bottom, float2{ 1.0f, 1.0f }},
            };

            static const BarDesc* findDesc( string_view barID )
            {
                for ( const BarDesc& desc : kArrBar )
                {
                    if ( barID == desc._pID )
                        return &desc;
                }
                return nullptr;
            }
        };
    } // namespace

    EditorViewportOverlays::EditorViewportOverlays()
        : _layout{}
        , _listRuntime{}
        , _listPlaced{}
        , _bLoaded{ false }
    {
    }

    void EditorViewportOverlays::ensureLoaded()
    {
        if ( _bLoaded )
            return;
        _bLoaded = true;
        // 저장된 값을 먼저 읽고 등록한다 — 등록은 저장된 바를 덮지 않고 기본값만 적는다.
        const string path = EditorUtil::resolveEditorStateFile( EditorViewportOverlaysInternal::kFileName );
        KeyValueMap  mapData;
        if ( path.empty() == false && FileUtil::exists( path ) && KeyValueFile::loadFile( path, mapData ) ) // 처음엔 파일이 없다
            _layout.readFrom( EditorViewportOverlaysInternal::kViewPrefix, mapData );
        for ( const EditorViewportOverlaysInternal::BarDesc& desc : EditorViewportOverlaysInternal::kArrBar )
        {
            EditorOverlayBarState state{};
            state._id       = desc._pID;
            state._dock     = desc._defaultDock;
            state._fraction = desc._defaultFraction;
            _layout.registerBar( state );
        }
    }

    void EditorViewportOverlays::save() const
    {
        const string path = EditorUtil::resolveEditorStateFile( EditorViewportOverlaysInternal::kFileName );
        if ( path.empty() )
            return;
        KeyValueMap mapData;
        _layout.writeTo( EditorViewportOverlaysInternal::kViewPrefix, mapData );
        if ( KeyValueFile::saveFile( path, mapData, "Scene view overlay bars (position fraction, docked side, collapsed, visible)" ) == false )
            SW_LOG_WARNING( "Could not save the viewport overlay layout to '%#'", path.c_str() );
    }

    EditorOverlayBarRuntime& EditorViewportOverlays::getRuntime( string_view barID )
    {
        for ( EditorOverlayBarRuntime& runtime : _listRuntime )
        {
            if ( runtime._id == barID )
                return runtime;
        }
        EditorOverlayBarRuntime runtime{};
        runtime._id = string{ barID };
        _listRuntime.push_back( runtime );
        return _listRuntime.back();
    }

    const EditorOverlayBarRuntime* EditorViewportOverlays::findRuntime( string_view barID ) const
    {
        for ( const EditorOverlayBarRuntime& runtime : _listRuntime )
        {
            if ( runtime._id == barID )
                return &runtime;
        }
        return nullptr;
    }

    void EditorViewportOverlays::resetToDefault()
    {
        ensureLoaded();
        _layout.resetToDefault();
        save();
    }

    bool EditorViewportOverlays::setBarVisible( string_view barID, bool bVisible )
    {
        ensureLoaded();
        EditorOverlayBarState* pState = _layout.findBar( barID );
        if ( pState == nullptr )
            return false;
        pState->_bVisible = bVisible;
        save();
        return true;
    }

    void EditorViewportOverlays::drawBarMenuItems()
    {
        for ( const EditorViewportOverlaysInternal::BarDesc& desc : EditorViewportOverlaysInternal::kArrBar )
        {
            EditorOverlayBarState* pState = _layout.findBar( desc._pID );
            if ( pState != nullptr && ImGui::MenuItem( desc._pTitle, nullptr, pState->_bVisible ) )
                (void)setBarVisible( desc._pID, pState->_bVisible == false ); // 바는 위에서 찾았다
        }
        ImGui::Separator();
        if ( ImGui::MenuItem( "Reset Overlays" ) )
            resetToDefault();
    }

    void EditorViewportOverlays::drawOverlaysMenu()
    {
        ensureLoaded();
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kLayout, "Overlays" ) ) )
            ImGui::OpenPopup( "##ViewportOverlaysMenu" );
        EditorSelfTestMarks::note( "sceneView.overlays" );
        EditorWidgets::drawTooltip( "Show or hide the scene view overlay bars, or put them back where they started" );
        if ( ImGui::BeginPopup( "##ViewportOverlaysMenu" ) )
        {
            drawBarMenuItems();
            ImGui::EndPopup();
        }
    }

    void EditorViewportOverlays::draw( const float2& canvasMin, const float2& canvasSize, ViewportToolbarSettings& settings, bool bHasSelection )
    {
        ensureLoaded();
        if ( canvasSize._x <= 1.0f || canvasSize._y <= 1.0f )
            return;
        _listPlaced.clear();
        for ( EditorOverlayBarState& state : _layout._listBar )
        {
            if ( state._bVisible && EditorViewportOverlaysInternal::findDesc( state._id ) != nullptr )
                drawBar( state, canvasMin, canvasSize, settings, bHasSelection );
        }
    }

    float2 EditorViewportOverlays::pushOutOfPlacedBars( EditorOverlayDock dock, const float2& position, const float2& size ) const
    {
        const float32 spacing = 4.0f * EditorThemeUtil::getDpiScale();
        float2        moved   = position;
        // 바 수만큼만 되풀이한다 — 한 번 밀 때마다 겹친 바 하나를 넘는다.
        for ( size_t pass = 0; pass <= _listPlaced.size(); ++pass )
        {
            bool bOverlapped = false;
            for ( const PlacedBar& placed : _listPlaced )
            {
                const bool bOverlap = moved._x < placed._position._x + placed._size._x && placed._position._x < moved._x + size._x &&
                                      moved._y < placed._position._y + placed._size._y && placed._position._y < moved._y + size._y;
                if ( bOverlap == false )
                    continue;
                bOverlapped = true;
                switch ( dock )
                {
                    case EditorOverlayDock::Top:
                    {
                        moved._y = placed._position._y + placed._size._y + spacing;
                        break;
                    }
                    case EditorOverlayDock::Bottom:
                    {
                        moved._y = placed._position._y - size._y - spacing;
                        break;
                    }
                    case EditorOverlayDock::Left:
                    {
                        moved._x = placed._position._x + placed._size._x + spacing;
                        break;
                    }
                    case EditorOverlayDock::Right:
                    {
                        moved._x = placed._position._x - size._x - spacing;
                        break;
                    }
                    case EditorOverlayDock::Free:
                    {
                        break;
                    }
                }
            }
            if ( bOverlapped == false )
                break;
        }
        return moved;
    }

    void EditorViewportOverlays::drawBar( EditorOverlayBarState& state, const float2& canvasMin, const float2& canvasSize, ViewportToolbarSettings& settings,
                                          bool bHasSelection )
    {
        const EditorViewportOverlaysInternal::BarDesc* pDesc   = EditorViewportOverlaysInternal::findDesc( state._id );
        EditorOverlayBarRuntime&                       runtime = getRuntime( state._id );
        const float32                                  scale   = EditorThemeUtil::getDpiScale();
        // 가장자리에 붙은 바는 테두리에서 조금 띄운다 — 뷰포트 안쪽 자리(여백을 뺀 크기)에서 자리를 셈한다.
        const float32 margin = EditorViewportOverlaysInternal::kEdgeMargin * scale;
        const float2  innerSize{ MathUtil::max( canvasSize._x - margin * 2.0f, 1.0f ), MathUtil::max( canvasSize._y - margin * 2.0f, 1.0f ) };
        float2        position = EditorViewportOverlayLayout::computePosition( state, runtime._lastSize, innerSize );
        position               = float2{ position._x + margin, position._y + margin };
        // 같은 가장자리에 붙은 바끼리 겹치면 가장자리에서 먼 쪽으로 한 줄씩 밀어 낸다(유니티 Overlays 가 같은 쪽 바를 줄 세우듯).
        if ( state._dock != EditorOverlayDock::Free && runtime._bDragging == false )
            position = pushOutOfPlacedBars( state._dock, position, runtime._lastSize );
        if ( runtime._bDragging )
            position = runtime._dragPosition;
        runtime._lastPosition = position;
        _listPlaced.push_back( PlacedBar{ state._dock, position, runtime._lastSize } );

        const bool bVertical = EditorViewportOverlayLayout::isVertical( state );
        ImGui::SetCursorScreenPos( ImVec2{ canvasMin._x + position._x, canvasMin._y + position._y } );
        const ImVec4 background = ImGui::GetStyleColorVec4( ImGuiCol_WindowBg );
        ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4{ background.x, background.y, background.z, 0.88f } );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2{ 4.0f * scale, 3.0f * scale } );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2{ 4.0f * scale, 3.0f * scale } );
        ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, 4.0f * scale );
        const string childID = "##overlay." + state._id;
        ImGui::BeginChild( childID.c_str(), ImVec2{ 0.0f, 0.0f },
                           ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings );

        // 손잡이: 끌면 옮기고, 놓으면 가까운 가장자리에 붙인다. 오른쪽 클릭은 바 메뉴.
        const ImVec2 gripSize{ 10.0f * scale, ImGui::GetFrameHeight() };
        ImGui::InvisibleButton( "##grip", gripSize );
        {
            const ImVec2  gripMin   = ImGui::GetItemRectMin();
            const ImU32   dotColor  = ImGui::GetColorU32( ImGui::IsItemHovered() || ImGui::IsItemActive() ? ImGuiCol_Text : ImGuiCol_TextDisabled );
            ImDrawList*   pDrawList = ImGui::GetWindowDrawList();
            const float32 dotRadius = 1.2f * scale;
            for ( uint32 row = 0; row < 3; ++row )
            {
                for ( uint32 column = 0; column < 2; ++column )
                {
                    pDrawList->AddCircleFilled( ImVec2{ gripMin.x + ( 3.0f + 4.0f * static_cast<float32>( column ) ) * scale, gripMin.y + gripSize.y * ( 0.3f + 0.2f * static_cast<float32>( row ) ) },
                                                dotRadius, dotColor );
                }
            }
        }
        EditorSelfTestMarks::note( ( "overlay.grip." + state._id ).c_str() );
        EditorWidgets::drawTooltip( "Drag to move - near an edge it docks there (left and right stack vertically). Right-click for the overlay menu." );
        if ( ImGui::IsItemActivated() )
        {
            runtime._bDragging    = true;
            runtime._dragPosition = position;
        }
        if ( runtime._bDragging && ImGui::IsItemActive() )
        {
            // 끄는 동안에도 뷰포트 밖으로는 나가지 않는다.
            const ImVec2 delta    = ImGui::GetIO().MouseDelta;
            runtime._dragPosition = float2{ MathUtil::clamp( runtime._dragPosition._x + delta.x, 0.0f, MathUtil::max( canvasSize._x - runtime._lastSize._x, 0.0f ) ),
                                            MathUtil::clamp( runtime._dragPosition._y + delta.y, 0.0f, MathUtil::max( canvasSize._y - runtime._lastSize._y, 0.0f ) ) };
        }
        if ( runtime._bDragging && ImGui::IsItemDeactivated() )
        {
            runtime._bDragging = false;
            // 놓은 자리(여백 안쪽 기준)로 붙은 쪽 · 비율을 정한다.
            EditorViewportOverlayLayout::applyDrop( state, float2{ runtime._dragPosition._x - margin, runtime._dragPosition._y - margin }, runtime._lastSize, innerSize,
                                                    EditorViewportOverlaysInternal::kSnapDistance * scale );
            save();
        }
        if ( ImGui::BeginPopupContextItem( "##overlayMenu" ) )
        {
            drawBarMenuItems();
            ImGui::EndPopup();
        }

        // 접기 · 펴기(접으면 손잡이와 이름만)
        ImGui::SameLine();
        if ( ImGui::ArrowButton( "##collapse", state._bCollapsed ? ImGuiDir_Right : ImGuiDir_Down ) )
        {
            state._bCollapsed = state._bCollapsed == false;
            save();
        }
        EditorSelfTestMarks::note( ( "overlay.collapse." + state._id ).c_str() );
        EditorWidgets::drawTooltip( state._bCollapsed ? "Expand" : "Collapse" );
        if ( state._bCollapsed )
        {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled( "%s", pDesc != nullptr ? pDesc->_pTitle : state._id.c_str() );
        }
        else
        {
            EditorViewportToolbar::nextItem( bVertical );
            if ( state._id == "view" )
                EditorViewportToolbar::drawViewBar( settings, bVertical );
            else if ( state._id == "transform" )
                EditorViewportToolbar::drawTransformBar( settings, bVertical, bHasSelection );
            else if ( state._id == "display" )
                EditorViewportToolbar::drawDisplayBar( settings, bVertical );
            else if ( state._id == "tools" )
                EditorViewportToolbar::drawToolsBar( settings, bVertical );
        }

        const ImVec2 size = ImGui::GetWindowSize();
        runtime._lastSize = float2{ size.x, size.y };
        ImGui::EndChild();
        ImGui::PopStyleVar( 3 );
        ImGui::PopStyleColor();
    }
} // namespace sw::editor
