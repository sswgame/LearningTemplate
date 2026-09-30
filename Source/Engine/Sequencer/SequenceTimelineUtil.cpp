#include "pch.h"

#include "Engine/Sequencer/SequenceTimelineUtil.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayer.h"

namespace sw
{
    namespace
    {
        struct SequenceTimelineUtilInternal
        {
            static float32 clipProgress( const SequenceTrackItem& item, int32 frame )
            {
                const int32 span = item._end - item._start;
                if ( span <= 0 )
                    return 1.0f;
                const float32 t = static_cast<float32>( frame - item._start ) / static_cast<float32>( span );
                return MathUtil::clamp( t, 0.0f, 1.0f );
            }

            /** @brief 그 대상을 덮는 활성 클립(종류 0)이 하나라도 있으면 true 입니다. */
            static bool isTargetCovered( const vector<const SequenceTrackItem*>& listActive, const string& targetObject )
            {
                for ( const SequenceTrackItem* pActive : listActive )
                {
                    if ( pActive != nullptr && pActive->_type == 0 && pActive->_targetObject == targetObject )
                        return true;
                }
                return false;
            }

            static GameObject* findTarget( GameObjectManager* pManager, string_view name )
            {
                if ( pManager == nullptr || name.empty() )
                    return nullptr;
                return pManager->findGameObjectByName( hashed_string{ name } );
            }

            static void applyClipTransform( GameObject* pTarget, const SequenceTrackItem& item, int32 frame )
            {
                if ( pTarget == nullptr || SequenceTimelineUtil::hasTransform( item ) == false )
                    return;
                SceneComponent* pScene = pTarget->getPrimarySceneComponent();
                if ( pScene == nullptr )
                    return;

                const float32 t           = clipProgress( item, frame );
                const float3  translation = float3::lerp( float3{}, item._translation, t );
                const float3  rotation    = float3::lerp( float3{}, item._rotation, t );
                const float3  scale       = float3::lerp( float3{ 1.0f, 1.0f, 1.0f }, item._scale, t );
                pScene->setLocalPosition( translation );
                pScene->setLocalRotation( rotation );
                pScene->setLocalScale( scale );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "SequenceTimeline" );

    bool SequenceTimelineUtil::hasTransform( const SequenceTrackItem& item )
    {
        if ( ( item._translation == float3{} ) == false )
            return true;
        if ( ( item._rotation == float3{} ) == false )
            return true;
        if ( ( item._scale == float3{ 1.0f, 1.0f, 1.0f } ) == false )
            return true;
        return false;
    }

    void SequenceTimelineUtil::applyFrame( GameObjectManager* pManager, const SequenceAsset& asset, int32 frame, int32 previousFrame,
                                           vector<const SequenceTrackItem*>* pOutListCrossedEvent )
    {
        if ( pOutListCrossedEvent != nullptr )
            pOutListCrossedEvent->clear();
        if ( pManager == nullptr )
            return;

        vector<const SequenceTrackItem*> listActive;
        asset.collectActiveItems( frame, listActive );

        // 대상마다 원하는 상태는 하나다 — 그 대상을 덮는 활성 클립이 있으면 켜짐. **바뀔 때만** 세팅한다. 예전에는 클립마다 끄고 켜서,
        // 클립이 둘인 대상은 매 프레임 꺼졌다 켜졌고(렌더 집합이 두 번 흔들렸다) 모든 대상을 매 프레임 다시 썼다.
        for ( const SequenceTrackItem& item : asset._listItem )
        {
            if ( item._type != 0 || item._targetObject.empty() )
                continue;
            GameObject* pTarget = SequenceTimelineUtilInternal::findTarget( pManager, item._targetObject );
            if ( pTarget == nullptr )
                continue;
            const bool bCovered = SequenceTimelineUtilInternal::isTargetCovered( listActive, item._targetObject );
            if ( pTarget->isActive() != bCovered )
                pTarget->setActive( bCovered );
        }

        for ( const SequenceTrackItem* pItem : listActive )
        {
            if ( pItem == nullptr || pItem->_targetObject.empty() || pItem->_type != 0 )
                continue;
            GameObject* pTarget = SequenceTimelineUtilInternal::findTarget( pManager, pItem->_targetObject );
            if ( pTarget != nullptr )
                SequenceTimelineUtilInternal::applyClipTransform( pTarget, *pItem, frame );
        }

        if ( previousFrame == kNoPreviousFrame )
            return;
        appendCrossedEvents( asset, previousFrame, frame, pOutListCrossedEvent );
    }

    void SequenceTimelineUtil::appendCrossedEvents( const SequenceAsset& asset, int32 previousFrame, int32 frame,
                                                    vector<const SequenceTrackItem*>* pOutListCrossedEvent )
    {
        for ( const SequenceTrackItem& item : asset._listItem )
        {
            if ( item._type != 1 )
                continue;
            // 지나갔는가: 이전 프레임에는 아직 닿지 않았고 이번 프레임에는 닿았다.
            if ( previousFrame >= item._start || item._start > frame )
                continue;
            if ( pOutListCrossedEvent != nullptr )
                pOutListCrossedEvent->push_back( &item );
            SW_LOG_INFO( "Sequence event %# on %#", item._name.c_str(), item._targetObject.c_str() );
        }
    }

    void SequenceTimelineUtil::applyPlayback( GameObjectManager* pManager, const SequencePlayer& player,
                                              vector<const SequenceTrackItem*>* pOutListCrossedEvent )
    {
        const SequenceAsset& asset           = player.getAsset();
        const int32          frameBeforeWrap = player.getFrameBeforeWrap();

        // 되감기 전 끝 구간을 먼저 모은다. `applyFrame` 이 출력을 비우므로 따로 모아 앞에 붙인다.
        vector<const SequenceTrackItem*> listTailEvent;
        if ( frameBeforeWrap != SequencePlayer::kNoLoopWrap )
            appendCrossedEvents( asset, frameBeforeWrap, asset._frameMax, &listTailEvent );

        applyFrame( pManager, asset, player.getCurrentFrame(), player.getPreviousFrame(), pOutListCrossedEvent );
        if ( pOutListCrossedEvent != nullptr && listTailEvent.empty() == false )
            pOutListCrossedEvent->insert( pOutListCrossedEvent->begin(), listTailEvent.begin(), listTailEvent.end() );
    }
} // namespace sw
