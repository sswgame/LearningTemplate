#include "pch.h"

#include "Editor/Panels/AnimationRewindPanel.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Gui/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Animation/AnimationRewind.h"
#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( AnimationRewindPanel, "animation_rewind", EditorPanelCategory::Tool, 1650 );

    namespace
    {
        struct AnimationRewindPanelInternal
        {
            /** @brief 뼈대 색(고른 것 · 나머지)입니다. */
            static constexpr float4 kSelectedColor{ 1.0f, 0.75f, 0.1f, 1.0f };
            static constexpr float4 kOtherColor{ 0.35f, 0.7f, 1.0f, 1.0f };
            /** @brief 디버그 드로우 카테고리입니다(`debugdraw.category AnimationRewind off` 로 끈다). */
            static constexpr const utf8* kDrawCategory = "AnimationRewind";

            /** @brief 고른 오브젝트의 스켈레탈 유닛 핸들입니다(없으면 무효). */
            static ComponentHandle findSelectedUnit( Scene* pScene )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr || pScene == nullptr || pScene->getObjectManager() == nullptr )
                    return ComponentHandle{};
                const uint64                 objectID = pContext->getWorkspace().getSelectedObjectID();
                GameObject*                  pObject  = objectID != 0 ? pScene->getObjectManager()->findGameObjectByID( objectID ) : nullptr;
                const SkeletalMeshComponent* pUnit    = pObject != nullptr ? pObject->getComponent<SkeletalMeshComponent>() : nullptr;
                return pUnit != nullptr ? pUnit->getHandle() : ComponentHandle{};
            }

            /** @brief 기록된 뼈대(부모 → 자식 선)와 상태 이름을 그립니다(이번 프레임만). */
            static void drawSkeleton( DebugDrawQueue& queue, const AnimationRewindTrack& track, const AnimationRewindFrame& frame, vector<float3>& inoutListPosition,
                                      const float4& color, bool bLabel )
            {
                AnimationRewindRecorder::computeWorldBonePositions( track, frame, inoutListPosition );
                if ( inoutListPosition.empty() )
                    return;
                const hashed_string category( kDrawCategory );
                for ( size_t boneIndex = 0; boneIndex < inoutListPosition.size(); ++boneIndex )
                {
                    const int32 parentIndex = track._listParentIndex[boneIndex];
                    if ( parentIndex >= 0 )
                        queue.drawLine( inoutListPosition[static_cast<size_t>( parentIndex )], inoutListPosition[boneIndex], color, 0.0f, category );
                }
                if ( bLabel && frame._state._stateName.empty() == false )
                    queue.drawText( inoutListPosition[0] + float3{ 0.0f, 0.2f, 0.0f }, frame._state._stateName.c_str(), color, 0.0f, category );
            }
        };
    } // namespace

    AnimationRewindPanel::AnimationRewindPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _listScratchPosition{}
        , _scrubSecondsAgo{ 0.0f }
        , _bDrawSkeleton{ SW_TRUE }
        , _bAllTracks{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void AnimationRewindPanel::drawContent()
    {
        Scene* pScene = editor::getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
        {
            EditorWidgets::drawEmptyHint( "No active scene." );
            return;
        }
        AnimationRewindRecorder& rewind = pScene->getObjectManager()->getAnimationSystem().getRewind();
        drawRecordingControls( rewind );
        if ( rewind.getTracks().empty() )
        {
            EditorWidgets::drawEmptyHint( "Nothing recorded yet. Turn recording on and play (PIE) - or 'anim.rewind on' in the console." );
            return;
        }
        drawTimeline( rewind );

        // 보는 시각 — 되감는 중이면 그 시각, 아니면 가장 늦은 기록.
        const float64               viewTime  = rewind.isScrubbing() ? rewind.getScrubTime() : rewind.getLatestTime();
        const ComponentHandle       selected  = AnimationRewindPanelInternal::findSelectedUnit( pScene );
        const AnimationRewindTrack* pSelected = selected.isValid() ? rewind.findTrack( selected ) : nullptr;
        DebugDrawQueue*             pQueue    = getService<DebugDrawQueue>();
        if ( _bDrawSkeleton == SW_TRUE && pQueue != nullptr )
        {
            for ( const AnimationRewindTrack& track : rewind.getTracks() )
            {
                const bool bSelected = &track == pSelected;
                if ( bSelected == false && _bAllTracks == SW_FALSE )
                    continue;
                const AnimationRewindFrame* pFrame = track.findFrame( viewTime );
                if ( pFrame != nullptr )
                    AnimationRewindPanelInternal::drawSkeleton( *pQueue, track, *pFrame, _listScratchPosition,
                                                                bSelected ? AnimationRewindPanelInternal::kSelectedColor : AnimationRewindPanelInternal::kOtherColor,
                                                                bSelected );
            }
        }

        ImGui::Separator();
        if ( pSelected == nullptr )
        {
            EditorWidgets::drawEmptyHint( "Select a character (Hierarchy or Scene view) to see its recorded state." );
            return;
        }
        const AnimationRewindFrame* pFrame = pSelected->findFrame( viewTime );
        if ( pFrame != nullptr )
            drawFrame( *pSelected, *pFrame );
    }

    void AnimationRewindPanel::drawRecordingControls( const AnimationRewindRecorder& rewind )
    {
        bool bRecord = AnimationRewindRecorder::isRecordingRequested();
        if ( ImGui::Checkbox( "Record", &bRecord ) )
            AnimationRewindRecorder::setRecordingRequested( bRecord );
        ImGui::SameLine();
        float32 seconds = AnimationRewindRecorder::getRequestedWindowSeconds();
        ImGui::SetNextItemWidth( 120.0f * EditorThemeUtil::getDpiScale() );
        if ( ImGui::SliderFloat( "Window (s)", &seconds, 1.0f, 60.0f, "%.0f" ) )
            AnimationRewindRecorder::setRequestedWindowSeconds( seconds );
        ImGui::SameLine();
        bool bDraw = _bDrawSkeleton == SW_TRUE;
        if ( ImGui::Checkbox( "Skeleton", &bDraw ) )
            _bDrawSkeleton = bDraw ? SW_TRUE : SW_FALSE;
        ImGui::SameLine();
        bool bAll = _bAllTracks == SW_TRUE;
        if ( ImGui::Checkbox( "All", &bAll ) )
            _bAllTracks = bAll ? SW_TRUE : SW_FALSE;
        ImGui::TextDisabled( "%u tracks · %.1f KB · %.1f s of history", static_cast<uint32>( rewind.getTracks().size() ),
                             static_cast<float64>( rewind.getByteCount() ) / 1024.0, rewind.getLatestTime() - rewind.getEarliestTime() );
    }

    void AnimationRewindPanel::drawTimeline( AnimationRewindRecorder& rewind )
    {
        const float32 history = static_cast<float32>( rewind.getLatestTime() - rewind.getEarliestTime() );
        if ( rewind.isScrubbing() == false )
            _scrubSecondsAgo = 0.0f;
        ImGui::SetNextItemWidth( -90.0f );
        // 왼쪽이 과거다 — 값은 "가장 늦은 기록에서 몇 초 전" 이라 막대를 뒤집어 그린다.
        float32 position = history - _scrubSecondsAgo;
        if ( ImGui::SliderFloat( "##RewindTimeline", &position, 0.0f, MathUtil::max( history, 0.001f ), "%.2f s" ) )
        {
            _scrubSecondsAgo = MathUtil::clamp( history - position, 0.0f, history );
            // 훑기는 PIE 를 멈춘다 — 멈추지 않으면 다음 틱이 새 기록을 쌓아 막대가 밀린다.
            if ( EditorPlaySession::isPlaying() )
                EditorPlaySession::pause();
            rewind.setScrubTime( rewind.getLatestTime() - static_cast<float64>( _scrubSecondsAgo ) );
        }
        ImGui::SameLine();
        if ( rewind.isScrubbing() )
        {
            if ( ImGui::Button( "Resume" ) )
            {
                rewind.clearScrub();
                _scrubSecondsAgo = 0.0f;
                if ( EditorPlaySession::isPaused() )
                    EditorPlaySession::setState( PlaySessionState::Playing );
            }
        }
        else
        {
            ImGui::TextDisabled( "live" );
        }
    }

    void AnimationRewindPanel::drawFrame( const AnimationRewindTrack& track, const AnimationRewindFrame& frame )
    {
        const AnimationDebugState& state = frame._state;
        ImGui::Text( "%s  ·  frame %u  ·  t %.3f s", track._label.c_str(), static_cast<uint32>( frame._frameIndex ), frame._time );
        ImGui::Text( "State: %s  (%.2f s)", state._stateName.empty() ? "-" : state._stateName.c_str(), static_cast<float64>( state._stateTime ) );
        if ( state._spriteFrame >= 0 )
            ImGui::Text( "Sprite frame: %d", state._spriteFrame );
        ImGui::Text( "Root motion: (%.3f, %.3f, %.3f)", static_cast<float64>( state._rootMotionTranslation._x ),
                     static_cast<float64>( state._rootMotionTranslation._y ), static_cast<float64>( state._rootMotionTranslation._z ) );
        ImGui::Text( "Bones: %u  ·  pose %u B", frame._boneCount, static_cast<uint32>( frame._poseByte.size() ) );
        if ( state._listNotify.empty() == false )
        {
            ImGui::TextUnformatted( "Notifies:" );
            for ( const hashed_string& notify : state._listNotify )
            {
                ImGui::SameLine();
                ImGui::TextColored( ImVec4{ 1.0f, 0.8f, 0.3f, 1.0f }, "%s", notify.c_str() );
            }
        }
        if ( state._listCurveName.empty() == false && ImGui::BeginTable( "##RewindCurves", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                                                                         ImVec2{ 0.0f, 160.0f } ) )
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
