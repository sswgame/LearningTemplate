#include "pch.h"

#include "Engine/Sequencer/SequenceTimelineUtil.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayer.h"

namespace sw
{
    namespace
    {
        struct SequenceTimelineUtilInternal
        {
            /** @brief 구간 동안 대상의 활성 · 트랜스폼을 정하는 종류인지 반환합니다. 표에 없는 종류는 아무것도 하지 않습니다. */
            static bool drivesTarget( const SequenceTrackItem& item )
            {
                const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( item._kind );
                return pInfo != nullptr && pInfo->_bDrivesTarget;
            }

            /** @brief 시작 프레임을 지날 때 알리는 종류인지 반환합니다. */
            static bool firesOnCross( const SequenceTrackItem& item )
            {
                const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( item._kind );
                return pInfo != nullptr && pInfo->_bFiresOnCross;
            }

            /** @brief 그 대상을 덮는 활성 클립이 하나라도 있으면 true 입니다. */
            static bool isTargetCovered( const vector<const SequenceTrackItem*>& listActive, const string& targetObject )
            {
                for ( const SequenceTrackItem* pActive : listActive )
                {
                    if ( pActive != nullptr && drivesTarget( *pActive ) && pActive->_targetObject == targetObject )
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

            /** @brief 축 하나(0 x · 1 y · 2 z)입니다. */
            static float32& axisOf( float3& value, uint32 axis )
            {
                if ( axis == 0 )
                    return value._x;
                if ( axis == 1 )
                    return value._y;
                return value._z;
            }

            /** @brief 프로퍼티 트랙이 가리키는 컴포넌트와 숫자 프로퍼티입니다. 못 찾거나 숫자(`float32` · `int32`)가 아니면 false 입니다. */
            static bool findTrackProperty( GameObject* pTarget, const SequenceKeyTrack& track, Component*& pOutComponent, const PropertyInfo*& pOutProperty )
            {
                pOutComponent    = nullptr;
                pOutProperty     = nullptr;
                const size_t dot = track._propertyPath.find( '.' );
                if ( pTarget == nullptr || dot == string::npos )
                    return false;
                Component* pComponent = pTarget->findComponentByTypeName( hashed_string( track._propertyPath.substr( 0, dot ).c_str() ) );
                if ( pComponent == nullptr || pComponent->getTypeInfo() == nullptr )
                    return false;
                const PropertyInfo* pProperty = pComponent->getTypeInfo()->findPropertyInHierarchy( hashed_string( track._propertyPath.substr( dot + 1 ).c_str() ) );
                if ( pProperty == nullptr )
                    return false;
                if ( pProperty->_typeName != hashed_string( "float32" ) && pProperty->_typeName != hashed_string( "int32" ) )
                    return false;
                pOutComponent = pComponent;
                pOutProperty  = pProperty;
                return true;
            }

            /** @brief 트랜스폼 트랙의 아홉 채널을 대상 SceneComponent 에 씁니다. 키가 없는 채널은 지금 값을 둔다. */
            static void applyTransformTrack( GameObject* pTarget, const SequenceKeyTrack& track, float32 frame )
            {
                SceneComponent* pScene = pTarget->getPrimarySceneComponent();
                if ( pScene == nullptr || track._listChannel.size() < kSequenceTransformChannelCount )
                    return;
                float3 arrValue[3] = { pScene->getLocalPosition(), pScene->getLocalRotation(), pScene->getLocalScale() };
                bool   arrKeyed[3] = { false, false, false };
                for ( uint32 channelIndex = 0; channelIndex < kSequenceTransformChannelCount; ++channelIndex )
                {
                    if ( track._listChannel[channelIndex]._listKey.empty() )
                        continue;
                    const uint32 groupIndex = channelIndex / 3;
                    float32&     component  = axisOf( arrValue[groupIndex], channelIndex % 3 );
                    component               = track._listChannel[channelIndex].evaluate( frame, component );
                    arrKeyed[groupIndex]    = true;
                }
                // 키가 있는 묶음만 쓴다 — 키 없는 회전 · 크기까지 쓰면 트랜스폼이 프레임마다 더티가 된다.
                if ( arrKeyed[0] )
                    pScene->setLocalPosition( arrValue[0] );
                if ( arrKeyed[1] )
                    pScene->setLocalRotation( arrValue[1] );
                if ( arrKeyed[2] )
                    pScene->setLocalScale( arrValue[2] );
            }

            static void applyPropertyTrack( GameObject* pTarget, const SequenceKeyTrack& track, float32 frame )
            {
                Component*          pComponent{ nullptr };
                const PropertyInfo* pProperty{ nullptr };
                if ( track._listChannel.empty() || track._listChannel[0]._listKey.empty() || findTrackProperty( pTarget, track, pComponent, pProperty ) == false )
                    return;
                if ( pProperty->_typeName == hashed_string( "float32" ) )
                {
                    float32* pValue = pProperty->getValuePtr<float32>( pComponent );
                    *pValue         = track._listChannel[0].evaluate( frame, *pValue );
                    return;
                }
                int32* pValue = pProperty->getValuePtr<int32>( pComponent );
                *pValue       = static_cast<int32>( MathUtil::round( track._listChannel[0].evaluate( frame, static_cast<float32>( *pValue ) ) ) );
            }

            /** @brief 그 프레임을 덮는 카메라 컷 가운데 가장 늦게 시작한 것의 카메라를 게임 카메라로 고릅니다. 덮는 컷이 없으면 풉니다. */
            static void applyCameraCut( GameObjectManager* pManager, const vector<const SequenceTrackItem*>& listActive )
            {
                const SequenceTrackItem* pCut = nullptr;
                for ( const SequenceTrackItem* pItem : listActive )
                {
                    const SequenceItemKindInfo* pInfo = pItem != nullptr ? SequenceAsset::findItemKindInfo( pItem->_kind ) : nullptr;
                    if ( pInfo == nullptr || pInfo->_bCutsCamera == false )
                        continue;
                    if ( pCut == nullptr || pItem->_start >= pCut->_start )
                        pCut = pItem;
                }
                GameObject*      pTarget = pCut != nullptr ? findTarget( pManager, pCut->_targetObject ) : nullptr;
                CameraComponent* pCamera = pTarget != nullptr ? pTarget->getComponent<CameraComponent>() : nullptr;
                pManager->getCameraRegistry().setCutCamera( pCamera );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "SequenceTimeline" );

    void SequenceTimelineUtil::applyFrame( GameObjectManager* pManager, const SequenceAsset& asset, int32 frame, int32 previousFrame,
                                           vector<const SequenceTrackItem*>* pOutListCrossedEvent )
    {
        if ( pOutListCrossedEvent != nullptr )
            pOutListCrossedEvent->clear();
        if ( pManager == nullptr )
            return;

        vector<const SequenceTrackItem*> listActive;
        asset.collectActiveItems( frame, listActive );

        // 대상마다 원하는 상태는 하나다 — 그 대상을 덮는 활성 클립이 있으면 켜짐. **바뀔 때만** 세팅한다(클립마다 끄고 켜면
        // 클립이 둘인 대상이 매 프레임 꺼졌다 켜져 렌더 집합이 두 번 흔들린다).
        for ( const SequenceTrackItem& item : asset._listItem )
        {
            if ( SequenceTimelineUtilInternal::drivesTarget( item ) == false || item._targetObject.empty() )
                continue;
            GameObject* pTarget = SequenceTimelineUtilInternal::findTarget( pManager, item._targetObject );
            if ( pTarget == nullptr )
                continue;
            const bool bCovered = SequenceTimelineUtilInternal::isTargetCovered( listActive, item._targetObject );
            if ( pTarget->isActive() != bCovered )
                pTarget->setActive( bCovered );
        }

        if ( asset.hasCameraCut() )
            SequenceTimelineUtilInternal::applyCameraCut( pManager, listActive );

        const float32 keyFrame = static_cast<float32>( frame );
        for ( const SequenceKeyTrack& track : asset._listTrack )
        {
            GameObject* pTarget = SequenceTimelineUtilInternal::findTarget( pManager, track._targetObject );
            if ( pTarget == nullptr )
                continue;
            if ( track._kind == SequenceTrackKind::Transform )
                SequenceTimelineUtilInternal::applyTransformTrack( pTarget, track, keyFrame );
            else if ( track._kind == SequenceTrackKind::Property )
                SequenceTimelineUtilInternal::applyPropertyTrack( pTarget, track, keyFrame );
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
            if ( SequenceTimelineUtilInternal::firesOnCross( item ) == false )
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

    bool SequenceTimelineUtil::readTrackValues( GameObjectManager* pManager, const SequenceKeyTrack& track, float32* pOutArrValue, uint32 valueCapacity )
    {
        GameObject* pTarget = SequenceTimelineUtilInternal::findTarget( pManager, track._targetObject );
        if ( pTarget == nullptr || pOutArrValue == nullptr || valueCapacity < SequenceKeyTrack::getChannelCount( track._kind ) )
            return false;
        if ( track._kind == SequenceTrackKind::Transform )
        {
            const SceneComponent* pScene = pTarget->getPrimarySceneComponent();
            if ( pScene == nullptr )
                return false;
            float3 arrValue[3] = { pScene->getLocalPosition(), pScene->getLocalRotation(), pScene->getLocalScale() };
            for ( uint32 channelIndex = 0; channelIndex < kSequenceTransformChannelCount; ++channelIndex )
            {
                pOutArrValue[channelIndex] = SequenceTimelineUtilInternal::axisOf( arrValue[channelIndex / 3], channelIndex % 3 );
            }
            return true;
        }
        if ( track._kind != SequenceTrackKind::Property )
            return false;
        Component*          pComponent{ nullptr };
        const PropertyInfo* pProperty{ nullptr };
        if ( SequenceTimelineUtilInternal::findTrackProperty( pTarget, track, pComponent, pProperty ) == false )
            return false;
        if ( pProperty->_typeName == hashed_string( "float32" ) )
            pOutArrValue[0] = *pProperty->getValuePtr<float32>( pComponent );
        else
            pOutArrValue[0] = static_cast<float32>( *pProperty->getValuePtr<int32>( pComponent ) );
        return true;
    }

    void SequenceTimelineUtil::releaseCameraCut( GameObjectManager* pManager, const SequenceAsset& asset )
    {
        if ( pManager != nullptr && asset.hasCameraCut() )
            pManager->getCameraRegistry().setCutCamera( nullptr );
    }
} // namespace sw
