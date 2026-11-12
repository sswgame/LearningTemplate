#include "pch.h"

#include "Editor/Panels/Animation/AnimationRewindPanel.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( AnimationRewindPanel, AnimationRewindPanel::kPanelID, EditorPanelCategory::Tool, 1650 );

    namespace
    {
        struct AnimationRewindPanelInternal
        {
            static constexpr Color kNotifyText{ 1.0f, 0.8f, 0.3f, 1.0f };
        };
    } // namespace

    AnimationRewindPanel::AnimationRewindPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _viewModel{}
        , _viewState{}
        , _scrubSecondsAgo{ 0.0f }
        , _bDrawSkeleton{ SW_TRUE }
        , _bAllTracks{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void AnimationRewindPanel::drawContent()
    {
        _viewModel.gather( _viewState );
        if ( _viewState._content == AnimationRewindContent::NoActiveScene )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAnimationRewind", "NoActiveScene" ) );
            return;
        }
        drawRecordingControls();
        if ( _viewState._content == AnimationRewindContent::NothingRecorded )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAnimationRewind", "NothingRecorded" ) );
            return;
        }
        drawTimeline();
        if ( _bDrawSkeleton == SW_TRUE )
            _viewModel.submitSkeletonDraw( _bAllTracks == SW_TRUE, SW_THEME_COLOR( "Base", "Highlight" ).toFloat4(), SW_THEME_COLOR( "Base", "Accent" ).toFloat4() );

        ImGui::Separator();
        if ( _viewState._content == AnimationRewindContent::NoSelection )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAnimationRewind", "SelectCharacter" ) );
            return;
        }
        if ( _viewState._content == AnimationRewindContent::Frame )
            drawFrame();
    }

    void AnimationRewindPanel::drawRecordingControls()
    {
        bool bRecord = _viewState._bRecordingRequested != SW_FALSE;
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Record" ), &bRecord ) )
            _viewModel.setRecordingRequested( bRecord );
        EditorSelfTestMarks::record( "animationRewind.record" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationRewind", "RecordTip" ) );
        ImGui::SameLine();
        float32 seconds = _viewState._requestedWindowSeconds;
        EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdField", "WindowS" ) );
        EditorChrome::setNextItemWidthColumns( 16.0f );
        if ( EditorWidgets::sliderFloat( SW_WIDGET_ID( "##Windows" ), &seconds, 1.0f, 60.0f, "%.0f" ) )
            _viewModel.setRequestedWindowSeconds( seconds );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationRewind", "WindowTip" ) );
        bool bDraw = _bDrawSkeleton == SW_TRUE;
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "Skeleton" ) ) );
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Skeleton" ), &bDraw ) )
            _bDrawSkeleton = bDraw ? SW_TRUE : SW_FALSE;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationRewind", "SkeletonTip" ) );
        bool bAll = _bAllTracks == SW_TRUE;
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "All" ) ) );
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "All" ), &bAll ) )
            _bAllTracks = bAll ? SW_TRUE : SW_FALSE;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationRewind", "AllTracksTip" ) );
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "TracksSOfHistory", TextArgumentList().addInteger( "tracksCount", static_cast<int64>( _viewState._trackCount ) ).addDecimal( "byteCount", static_cast<float64>( static_cast<float64>( _viewState._byteCount ) / 1024.0 ), 1 ).addDecimal( "latestTime", _viewState._historySeconds, 1 ) ).c_str() );
    }

    void AnimationRewindPanel::drawTimeline()
    {
        const float32 history = static_cast<float32>( _viewState._historySeconds );
        if ( _viewState._bScrubbing == SW_FALSE )
            _scrubSecondsAgo = 0.0f;
        EditorChrome::setNextItemWidthColumns( -12.0f );
        // 왼쪽이 과거다 — 값은 "가장 늦은 기록에서 몇 초 전" 이라 막대를 뒤집어 그린다.
        float32 position = history - _scrubSecondsAgo;
        if ( EditorWidgets::sliderFloat( SW_WIDGET_ID( "##RewindTimeline" ), &position, 0.0f, MathUtil::max( history, 0.001f ), "%.2f s" ) )
        {
            _scrubSecondsAgo = MathUtil::clamp( history - position, 0.0f, history );
            _viewModel.scrubTo( _scrubSecondsAgo );
        }
        EditorSelfTestMarks::record( "animationRewind.timeline" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationRewind", "TimelineTip" ) );
        ImGui::SameLine();
        if ( _viewState._bScrubbing != SW_FALSE )
        {
            if ( ImGui::Button( SW_LOCKEY( "EdText", "Resume" ) ) )
            {
                _viewModel.resume();
                _scrubSecondsAgo = 0.0f;
            }
            EditorSelfTestMarks::record( "animationRewind.resume" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationRewind", "ResumeTip" ) );
        }
        else
        {
            ImGui::TextDisabled( "live" );
        }
    }

    void AnimationRewindPanel::drawFrame()
    {
        const AnimationDebugState& state = _viewState._frameState;
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "FrameTS", TextArgumentList().addText( "label", _viewState._trackLabel.c_str() ).addInteger( "frameIndex", static_cast<int64>( _viewState._frameIndex ) ).addDecimal( "time", _viewState._frameTime, 3 ) ).c_str() );
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "StateS", TextArgumentList().addText( "stateName", state._stateName.empty() ? "-" : state._stateName.c_str() ).addDecimal( "stateTime", static_cast<float64>( state._stateTime ), 2 ) ).c_str() );
        if ( state._spriteFrame.has_value() )
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "SpriteFrame", TextArgumentList().addInteger( "spriteFrame", static_cast<int64>( *state._spriteFrame ) ) ).c_str() );
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "RootMotion", TextArgumentList().addDecimal( "rootMotionTranslationX", static_cast<float64>( state._rootMotionTranslation._x ), 3 ).addDecimal( "rootMotionTranslationY", static_cast<float64>( state._rootMotionTranslation._y ), 3 ).addDecimal( "rootMotionTranslationZ", static_cast<float64>( state._rootMotionTranslation._z ), 3 ) ).c_str() );
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "BonesPoseB", TextArgumentList().addInteger( "boneCount", static_cast<int64>( _viewState._boneCount ) ).addInteger( "poseByteCount", static_cast<int64>( _viewState._poseByteCount ) ) ).c_str() );
        if ( state._listNotify.empty() == false )
        {
            ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Notifies" ) );
            for ( const hashed_string& notify : state._listNotify )
            {
                ImGui::SameLine();
                ImGui::TextColored( EditorColorUtil::toImVec4( AnimationRewindPanelInternal::kNotifyText ), "%s", notify.c_str() );
            }
        }
        if ( state._listCurveName.empty() == false && ImGui::BeginTable( "##RewindCurves", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                                                                         EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, 8.0f ) ) ) )
        {
            ImGui::TableSetupColumn( "Curve" );
            ImGui::TableSetupColumn( "Value" );
            ImGui::TableHeadersRow();
            for ( size_t curveIndex = 0; curveIndex < state._listCurveName.size() && curveIndex < state._listCurveValue.size(); ++curveIndex )
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted( state._listCurveName[curveIndex].c_str() );
                ImGui::TableNextColumn();
                ImGui::Text( "%.3f", static_cast<float64>( state._listCurveValue[curveIndex] ) );
            }
            ImGui::EndTable();
        }
    }
} // namespace sw::editor
