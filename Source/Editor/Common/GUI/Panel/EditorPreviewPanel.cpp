// 미리보기 창 바탕의 그리기(뷰포트 · 재생 막대)입니다 — MVVM View. 미리보기 씬 · 선택 · 창 스택은 `Model/EditorPreviewPanelScene.cpp` 다.
#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorPreviewPanelViewInternal
        {
            /// @brief 재생 속도 칸 — 언리얼 Persona 재생 속도 메뉴 · 유니티 타임라인 배속과 같은 결의 고정 단계.
            static constexpr float32     kArrPreviewSpeed[] = { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f };
            static constexpr const utf8* kArrSpeedLabel[]   = { "0.1x", "0.25x", "0.5x", "1x", "2x" };
        };
    } // namespace

    void EditorPreviewPanel::beginPreviewFrame()
    {
        preparePreviewFrame( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) );
    }

    float2 EditorPreviewPanel::computeViewportFillSize( float32 minHeight ) const
    {
        const ImVec2  available  = ImGui::GetContentRegionAvail();
        const bool    bScrollbar = ImGui::GetScrollMaxY() > 0.0f; // 이 창에 지금 세로 스크롤바가 있다 — 그 폭은 이미 빠졌다
        const float32 width      = available.x - ( bScrollbar ? 0.0f : ImGui::GetStyle().ScrollbarSize );
        return float2{ width > 1.0f ? width : 1.0f, available.y > minHeight ? available.y : minHeight };
    }

    void EditorPreviewPanel::drawPreviewViewport( const float2& size, bool bOrbitBlocked )
    {
        const bool   bFocused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
        const bool   bHovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows );
        const ImVec2 origin   = ImGui::GetCursorScreenPos();
        // 막대는 이미지보다 먼저 연 자식 창이다 — 자식 창은 부모 내용 위에 그려지고 마우스도 먼저 받으며, 뒤에 놓는 이미지가 커서를 정상으로 되돌린다.
        drawPreviewBar( float2{ origin.x, origin.y }, size );
        ImGui::SetCursorScreenPos( origin );
        applyPreviewBar( EditorPreviewPanelViewInternal::kArrPreviewSpeed[_speedIndex] );
        _viewport.draw( size, ( bFocused || bHovered ) && bOrbitBlocked == false );
        const bool bImageHovered = ImGui::IsItemHovered();
        if ( _bAxesShown )
            _viewport.drawAxisIndicator();
        if ( bImageHovered && EditorInputGUI::wasActionTriggered( "view.frameHovered" ) )
            framePreviewSubject();
        publishPreviewBarState( EditorPreviewPanelViewInternal::kArrPreviewSpeed[_speedIndex] );
    }

    void EditorPreviewPanel::framePreviewSubject()
    {
        float3  center{};
        float32 radius{ 0.0f };
        if ( findPreviewFrameSphere( center, radius ) )
            _viewport.frameSphere( center, radius );
    }

    void EditorPreviewPanel::drawPreviewBar( const float2& origin, const float2& size )
    {
        if ( _viewport.isInitialized() == false || size._x <= 1.0f || size._y <= 1.0f )
            return;
        const string  scope  = string{ getDocumentKind().getID() } + ".preview";
        const float32 margin = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorScreenPos( ImVec2{ origin._x + margin, origin._y + margin } );
        ImGui::PushStyleColor( ImGuiCol_ChildBg, EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "PanelBg" ) ) );
        const ImGuiChildFlags childFlags = ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding;
        if ( ImGui::BeginChild( "##previewBar", ImVec2{ 0.0f, 0.0f }, childFlags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings ) )
        {
            const ImVec4 toggleOn  = EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "ToggleActive" ) );
            const ImVec4 toggleOff = EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "ToggleInactive" ) );
            // 재생 · 멈춤 한 단추(라벨이 상태를 말한다) — 언리얼 Persona · 유니티 미리보기의 재생 단추.
            if ( ImGui::SmallButton( _bPreviewPaused ? EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "PreviewPlay" ), "PreviewPlayPause" )
                                                     : EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "PreviewPause" ), "PreviewPlayPause" ) ) )
                _bPreviewPaused = _bPreviewPaused == false;
            EditorSelfTestMarks::recordFormat( "%#.pause", scope );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PreviewPauseTip" ) );
            ImGui::SameLine();
            ImGui::BeginDisabled( _bPreviewPaused == false );
            if ( ImGui::SmallButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "PreviewStep" ), "PreviewStep" ) ) )
                _stepFramesLeft = 1;
            ImGui::EndDisabled();
            EditorSelfTestMarks::recordFormat( "%#.step", scope );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PreviewStepTip" ) );
            ImGui::SameLine();
            EditorChrome::setNextItemWidthColumns( 6.0f );
            if ( EditorWidgets::beginCombo( "##previewSpeed", EditorPreviewPanelViewInternal::kArrSpeedLabel[_speedIndex] ) )
            {
                for ( uint32 index = 0; index < static_cast<uint32>( std::size( EditorPreviewPanelViewInternal::kArrPreviewSpeed ) ); ++index )
                {
                    if ( ImGui::Selectable( EditorPreviewPanelViewInternal::kArrSpeedLabel[index], index == _speedIndex ) )
                        _speedIndex = index;
                }
                ImGui::EndCombo();
            }
            EditorSelfTestMarks::recordFormat( "%#.speed", scope );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PreviewSpeedTip" ) );

            float32 time{ 0.0f };
            float32 duration{ 0.0f };
            if ( readPreviewTimeline( time, duration ) && duration > 0.0f )
            {
                ImGui::SameLine();
                EditorChrome::setNextItemWidthColumns( 16.0f );
                if ( EditorWidgets::sliderFloat( SW_WIDGET_ID( "##previewScrub" ), &time, 0.0f, duration, "%.2f s" ) )
                {
                    _bPreviewPaused = SW_TRUE; // 문지르는 동안은 멈춘다(언리얼 Persona 타임라인과 같다)
                    seekPreviewTimeline( time );
                }
                EditorSelfTestMarks::recordFormat( "%#.scrub", scope );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PreviewScrubTip" ) );
            }

            float3     center{};
            float32    radius{ 0.0f };
            const bool bCanFrame = findPreviewFrameSphere( center, radius );
            ImGui::SameLine();
            ImGui::BeginDisabled( bCanFrame == false );
            if ( ImGui::SmallButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "PreviewFrame" ), "PreviewFrame" ) ) )
                framePreviewSubject();
            ImGui::EndDisabled();
            EditorSelfTestMarks::recordFormat( "%#.frame", scope );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PreviewFrameTip" ) );
            if ( _viewport.hasFloor() || _bFloorShown == false )
            {
                ImGui::SameLine();
                ImGui::PushStyleColor( ImGuiCol_Button, _bFloorShown ? toggleOn : toggleOff );
                if ( ImGui::SmallButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "PreviewFloor" ), "PreviewFloor" ) ) )
                {
                    _bFloorShown   = _bFloorShown == false;
                    _bFloorChanged = SW_TRUE; // 씬의 바닥 오브젝트는 Model(`applyPreviewBar`)이 켜고 끈다
                }
                ImGui::PopStyleColor();
                EditorSelfTestMarks::recordFormat( "%#.floor", scope );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PreviewFloorTip" ) );
            }
            ImGui::SameLine();
            ImGui::PushStyleColor( ImGuiCol_Button, _bAxesShown ? toggleOn : toggleOff );
            if ( ImGui::SmallButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "PreviewAxes" ), "PreviewAxes" ) ) )
                _bAxesShown = _bAxesShown == false;
            ImGui::PopStyleColor();
            EditorSelfTestMarks::recordFormat( "%#.axes", scope );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PreviewAxesTip" ) );
            const ImVec2 barSize = ImGui::GetWindowSize();
            _previewBarSize      = float2{ barSize.x, barSize.y };
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

} // namespace sw::editor
