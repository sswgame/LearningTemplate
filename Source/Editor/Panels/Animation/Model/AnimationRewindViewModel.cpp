#include "pch.h"

#include "Editor/Panels/Animation/Model/AnimationRewindViewModel.h"

#include "DevTools/Client/Animation/AnimationRewind.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Scene/AnimationSystem.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    namespace
    {
        struct AnimationRewindViewModelInternal
        {
            /** @brief 디버그 드로우 카테고리입니다(`debugdraw.category AnimationRewind off` 로 끈다). */
            static constexpr const utf8* kDrawCategory = "AnimationRewind";
            /** @brief 상태 이름 글을 뿌리 뼈 위로 띄우는 높이(월드 단위)입니다. */
            static constexpr float32 kLabelLift = 0.2f;

            /** @brief 활성 씬의 되감기 기록기입니다. 활성 씬이 없으면 nullptr 입니다. 기록기는 애니메이션 시스템의 프레임 관찰자다(개발 구성은 늘 등록한다 — 에디터는 개발 구성에만 있다). */
            [[nodiscard]] static AnimationRewindRecorder* findActiveRewind()
            {
                Scene* pScene = editor::getActiveScene();
                return pScene != nullptr && pScene->getObjectManager() != nullptr ? AnimationRewindRecorder::find( pScene->getObjectManager()->getSubsystem<AnimationSystem>() ) : nullptr;
            }

            /** @brief 고른 오브젝트의 스켈레탈 유닛 트랙입니다(없으면 nullptr). */
            static const AnimationRewindTrack* findSelectedTrack( const AnimationRewindRecorder& rewind )
            {
                Scene* pScene = editor::getActiveScene();
                if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
                    return nullptr;
                const uint64                 objectID = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().getSelectedObjectID();
                GameObject*                  pObject  = objectID != 0 ? pScene->getObjectManager()->findGameObjectByID( objectID ) : nullptr;
                const SkeletalMeshComponent* pUnit    = pObject != nullptr ? pObject->getComponent<SkeletalMeshComponent>() : nullptr;
                return pUnit != nullptr && pUnit->getHandle().isValid() ? rewind.findTrack( pUnit->getHandle() ) : nullptr;
            }

            /** @brief 보는 시각 — 되감는 중이면 그 시각, 아니면 가장 늦은 기록. */
            static float64 getViewTime( const AnimationRewindRecorder& rewind ) { return rewind.isScrubbing() ? rewind.getScrubTime() : rewind.getLatestTime(); }

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
                    queue.drawText( inoutListPosition[0] + float3{ 0.0f, kLabelLift, 0.0f }, frame._state._stateName.c_str(), color, 0.0f, category );
            }

            [[nodiscard]] static bool readTrackCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AnimationRewindRecorder* pRewind = findActiveRewind();
                if ( pRewind == nullptr )
                    return false;
                outValue = static_cast<float64>( pRewind->getTracks().size() );
                return true;
            }

            [[nodiscard]] static bool readHistorySeconds( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AnimationRewindRecorder* pRewind = findActiveRewind();
                if ( pRewind == nullptr )
                    return false;
                outValue = pRewind->getTracks().empty() ? 0.0 : pRewind->getLatestTime() - pRewind->getEarliestTime();
                return true;
            }

            [[nodiscard]] static bool readScrubbing( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AnimationRewindRecorder* pRewind = findActiveRewind();
                if ( pRewind == nullptr )
                    return false;
                outValue = pRewind->isScrubbing() ? 1.0 : 0.0;
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorAnimationRewindTrackCount, "Editor.AnimationRewindTrackCount", "Animation rewind tracks of the active scene (fails while there is no active scene)",
                         &AnimationRewindViewModelInternal::readTrackCount );
    SW_AUTOMATION_WATCH( editorAnimationRewindHistorySeconds, "Editor.AnimationRewindHistorySeconds", "Seconds of animation history the active scene's rewind holds",
                         &AnimationRewindViewModelInternal::readHistorySeconds );
    SW_AUTOMATION_WATCH( editorAnimationRewindScrubbing, "Editor.AnimationRewindScrubbing", "1 while the active scene's animation rewind shows a past time",
                         &AnimationRewindViewModelInternal::readScrubbing );

    AnimationRewindViewModel::AnimationRewindViewModel()
        : _listScratchPosition{}
    {
    }

    void AnimationRewindViewModel::gather( AnimationRewindViewState& outState )
    {
        using Internal                   = AnimationRewindViewModelInternal;
        outState._content                = AnimationRewindContent::NoActiveScene;
        outState._bRecordingRequested    = AnimationRewindRecorder::isRecordingRequested() ? SW_TRUE : SW_FALSE;
        outState._requestedWindowSeconds = AnimationRewindRecorder::getRequestedWindowSeconds();
        outState._trackLabel.clear();
        outState._frameState.reset();

        Scene* pScene = editor::getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;
        const AnimationRewindRecorder* pRewind = Internal::findActiveRewind();
        SW_ASSERT( pRewind != nullptr ); // 개발 구성은 기록기를 늘 등록한다
        const AnimationRewindRecorder& rewind = *pRewind;
        outState._trackCount                  = static_cast<uint32>( rewind.getTracks().size() );
        outState._byteCount                   = rewind.getByteCount();
        outState._historySeconds              = rewind.getLatestTime() - rewind.getEarliestTime();
        outState._bScrubbing                  = rewind.isScrubbing() ? SW_TRUE : SW_FALSE;
        if ( rewind.getTracks().empty() )
        {
            outState._content = AnimationRewindContent::NothingRecorded;
            return;
        }
        const AnimationRewindTrack* pSelected = Internal::findSelectedTrack( rewind );
        if ( pSelected == nullptr )
        {
            outState._content = AnimationRewindContent::NoSelection;
            return;
        }
        const AnimationRewindFrame* pFrame = pSelected->findFrame( Internal::getViewTime( rewind ) );
        if ( pFrame == nullptr )
        {
            outState._content = AnimationRewindContent::NoFrame;
            return;
        }
        outState._content       = AnimationRewindContent::Frame;
        outState._trackLabel    = pSelected->_label;
        outState._frameIndex    = pFrame->_frameIndex;
        outState._frameTime     = pFrame->_time;
        outState._boneCount     = pFrame->_boneCount;
        outState._poseByteCount = static_cast<uint32>( pFrame->_poseByte.size() );
        outState._frameState    = pFrame->_state;
    }

    void AnimationRewindViewModel::setRecordingRequested( bool bRecord )
    {
        AnimationRewindRecorder::setRecordingRequested( bRecord );
    }

    void AnimationRewindViewModel::setRequestedWindowSeconds( float32 seconds )
    {
        AnimationRewindRecorder::setRequestedWindowSeconds( seconds );
    }

    void AnimationRewindViewModel::scrubTo( float32 secondsAgo )
    {
        AnimationRewindRecorder* pRewind = AnimationRewindViewModelInternal::findActiveRewind();
        if ( pRewind == nullptr )
            return;
        // 훑기는 PIE 를 멈춘다 — 멈추지 않으면 다음 틱이 새 기록을 쌓아 막대가 밀린다.
        if ( EditorPlaySession::isPlaying() )
            EditorPlaySession::pause();
        pRewind->setScrubTime( pRewind->getLatestTime() - static_cast<float64>( secondsAgo ) );
    }

    void AnimationRewindViewModel::resume()
    {
        AnimationRewindRecorder* pRewind = AnimationRewindViewModelInternal::findActiveRewind();
        if ( pRewind == nullptr )
            return;
        pRewind->clearScrub();
        if ( EditorPlaySession::isPaused() )
            EditorPlaySession::setState( PlaySessionState::Playing );
    }

    void AnimationRewindViewModel::submitSkeletonDraw( bool bAllTracks, const float4& selectedColor, const float4& otherColor )
    {
        using Internal                         = AnimationRewindViewModelInternal;
        const AnimationRewindRecorder* pRewind = Internal::findActiveRewind();
        if ( pRewind == nullptr )
            return;
        const float64               viewTime  = Internal::getViewTime( *pRewind );
        const AnimationRewindTrack* pSelected = Internal::findSelectedTrack( *pRewind );
        DebugDrawQueue&             queue     = editor::getSubsystem<DebugDrawQueue>();
        for ( const AnimationRewindTrack& track : pRewind->getTracks() )
        {
            const bool bSelected = &track == pSelected;
            if ( bSelected == false && bAllTracks == false )
                continue;
            const AnimationRewindFrame* pFrame = track.findFrame( viewTime );
            if ( pFrame != nullptr )
                Internal::drawSkeleton( queue, track, *pFrame, _listScratchPosition, bSelected ? selectedColor : otherColor, bSelected );
        }
    }
} // namespace sw::editor
