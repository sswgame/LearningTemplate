#include "pch.h"

#include "Engine/Sequencer/SequenceTimelineUtil.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayer.h"

namespace sw
{
    namespace
    {
        struct SequenceTimelineUtilInternal
        {
            /** @brief 구간 동안 대상의 활성 상태를 정하는 종류인지 반환합니다. 표에 없는 종류는 아무것도 하지 않습니다. */
            static bool affectsTarget( const SequenceTrackItem& item )
            {
                const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( item._kind );
                return pInfo != nullptr && pInfo->_bDrivesTarget;
            }

            /** @brief 시작 프레임을 지날 때 알리는 종류인지 반환합니다. */
            static bool shouldFireOnCross( const SequenceTrackItem& item )
            {
                const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( item._kind );
                return pInfo != nullptr && pInfo->_bFiresOnCross;
            }

            /** @brief 그 바인딩을 덮는 활성 클립이 하나라도 있으면 true 입니다. */
            static bool isTargetCovered( const vector<const SequenceTrackItem*>& listActive, uint32 bindingID )
            {
                for ( const SequenceTrackItem* pActive : listActive )
                {
                    const bool bActiveDrivesTarget = pActive != nullptr && affectsTarget( *pActive ) && pActive->_binding == bindingID;
                    if ( bActiveDrivesTarget )
                        return true;
                }
                return false;
            }

            /** @brief 축 하나(0 은 x, 1 은 y, 2 는 z)입니다. */
            static float32& axisOf( float3& value, uint32 axis )
            {
                if ( axis == 0 )
                    return value._x;
                if ( axis == 1 )
                    return value._y;
                return value._z;
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
                    // 키가 없는 채널은 지금 값을 둔다.
                    const optional<float32> value = track._listChannel[channelIndex].evaluate( frame );
                    if ( value.has_value() == false )
                        continue;
                    const uint32 groupIndex                          = channelIndex / 3;
                    axisOf( arrValue[groupIndex], channelIndex % 3 ) = *value;
                    arrKeyed[groupIndex]                             = true;
                }
                // 키가 있는 그룹만 쓴다. 키 없는 회전과 크기까지 쓰면 트랜스폼이 프레임마다 더티가 된다.
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
                const bool          bNoKeyData = track._listChannel.empty() || track._listChannel[0]._listKey.empty() || SequenceTimelineUtil::findTrackProperty( pTarget, track, pComponent, pProperty ) == false;
                if ( bNoKeyData )
                    return;
                // 위에서 채널 0 에 키가 있음을 확인했다.
                const float32 value = *track._listChannel[0].evaluate( frame );
                // 그 타입이 담지 못하는 값(정수 범위 밖 · unorm 범위 밖)은 쓰지 않고 지금 값을 둔다.
                // findTrackProperty 가 숫자 갈래(내장 타입)를 확인했다.
                (void)ReflectedValueUtil::writeNumber( ReflectedValueUtil::findBuiltinIndex( pProperty->_typeName ), pProperty->getRawPtr( static_cast<void*>( pComponent ) ),
                                                       static_cast<float64>( value ) );
            }

            /** @brief 리그 슬롯 트랙의 가중치를 대상의 `PoseModifierComponent` 에 씁니다. 키나 슬롯 이름이 없으면 건드리지 않습니다. */
            static void applyRigSlotTrack( GameObject* pTarget, const SequenceKeyTrack& track, float32 frame )
            {
                const bool bNoRigKeys = track._propertyPath.empty() || track._listChannel.empty() || track._listChannel[0]._listKey.empty();
                if ( bNoRigKeys )
                    return;
                PoseModifierComponent* pModifier = pTarget->getComponent<PoseModifierComponent>();
                if ( pModifier == nullptr )
                    return;
                pModifier->setSlotWeight( hashed_string( track._propertyPath.c_str() ), SequenceTimelineUtil::sampleRigSlotWeight( track, frame ) );
            }

            /** @brief 렌즈 값(화각 · 초점 · 조리개 · 시프트 x · y)을 카메라에 씁니다. @p pArrKeyed 가 거짓인 채널은 지금 값을 둔다(nullptr 이면 모두 쓴다). */
            static bool isChannelKeyed( const bool* pArrKeyed, uint32 channelIndex )
            {
                return pArrKeyed == nullptr || pArrKeyed[channelIndex];
            }

            static void writeLens( CameraComponent& camera, const float32* pArrValue, const bool* pArrKeyed )
            {
                if ( isChannelKeyed( pArrKeyed, 0 ) )
                    camera.setFieldOfViewY( pArrValue[0] );
                CameraDepthOfFieldValues lens = camera.getDepthOfField();
                if ( isChannelKeyed( pArrKeyed, 1 ) )
                    lens._focusDistance = MathUtil::max( pArrValue[1], 0.0f );
                if ( isChannelKeyed( pArrKeyed, 2 ) )
                    lens._aperture = MathUtil::max( pArrValue[2], 0.0f );
                if ( isChannelKeyed( pArrKeyed, 1 ) || isChannelKeyed( pArrKeyed, 2 ) )
                    camera.setDepthOfField( lens );
                float2 shift = camera.getLensShift();
                if ( isChannelKeyed( pArrKeyed, 3 ) )
                    shift._x = pArrValue[3];
                if ( isChannelKeyed( pArrKeyed, 4 ) )
                    shift._y = pArrValue[4];
                if ( isChannelKeyed( pArrKeyed, 3 ) || isChannelKeyed( pArrKeyed, 4 ) )
                    camera.setLensShift( shift );
            }

            /** @brief 렌즈 트랙의 키가 있는 채널만 대상 카메라에 씁니다(언리얼 CineCamera 키와 같다 — 키 없는 채널은 카메라 값 그대로). */
            static void applyLensTrack( GameObject* pTarget, const SequenceKeyTrack& track, float32 frame )
            {
                CameraComponent* pCamera = pTarget->getComponent<CameraComponent>();
                if ( pCamera == nullptr || track._listChannel.size() < kSequenceLensChannelCount )
                    return;
                float32 arrValue[kSequenceLensChannelCount]{};
                bool    arrKeyed[kSequenceLensChannelCount]{};
                bool    bAnyKey = false;
                for ( uint32 channelIndex = 0; channelIndex < kSequenceLensChannelCount; ++channelIndex )
                {
                    const optional<float32> value = track._listChannel[channelIndex].evaluate( frame );
                    arrKeyed[channelIndex]        = value.has_value();
                    arrValue[channelIndex]        = value.value_or( 0.0f );
                    bAnyKey                       = bAnyKey || value.has_value();
                }
                if ( bAnyKey )
                    writeLens( *pCamera, arrValue, arrKeyed );
            }

            /** @brief 그 프레임을 덮는 카메라 컷 가운데 가장 늦게 시작한 것(`SequenceAsset::findCameraCut`)의 카메라를 게임 카메라로 고릅니다. 덮는 컷이 없으면 풉니다. */
            static void applyCameraCut( GameObjectManager* pManager, const SequenceAsset& asset, const SequenceBindingView& bindings, int32 frame )
            {
                const SequenceTrackItem* pCut    = asset.findCameraCut( frame );
                GameObject*              pTarget = pCut != nullptr ? SequenceTimelineUtil::findTarget( pManager, bindings, pCut->_binding ) : nullptr;
                CameraComponent*         pCamera = pTarget != nullptr ? pTarget->getComponent<CameraComponent>() : nullptr;
                pManager->getCameraRegistry().setCutCamera( pCamera );
            }

            /** @brief 항목 @p itemIndex 의 구간 상태입니다. 없으면 nullptr 입니다. */
            static SequenceSpanState* findSpan( vector<SequenceSpanState>& listSpan, uint32 itemIndex )
            {
                for ( SequenceSpanState& span : listSpan )
                {
                    if ( span._itemIndex == itemIndex )
                        return &span;
                }
                return nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SequenceSpanState::SequenceSpanState( uint32 itemIndex )
        : _clip{}
        , _playingID{ 0 }
        , _itemIndex{ itemIndex }
        , _bInside{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    SequenceRestoreEntry::SequenceRestoreEntry()
        : _arrValue{}
        , _sourceIndex{ 0 }
        , _bFromTrack{ SW_FALSE }
        , _bActive{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    SequencePlaybackState::SequencePlaybackState()
        : _listSpawned{}
        , _listSpan{}
        , _listRestore{}
        , _bRestoreCaptured{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    GameObjectHandle SequenceBindingView::findHandle( uint32 bindingID ) const
    {
        if ( bindingID == kSequenceNoBinding )
            return GameObjectHandle{};
        // 스폰 바인딩은 시퀀스가 채운다 — 씬의 칸을 보지 않는다(스폰 전이면 아직 없다).
        if ( _pAsset != nullptr && _pListSpawned != nullptr )
        {
            for ( const SequenceSpawnedObject& spawned : *_pListSpawned )
            {
                const bool bSameBinding = spawned._itemIndex < _pAsset->_listItem.size() && _pAsset->_listItem[spawned._itemIndex]._binding == bindingID;
                if ( bSameBinding )
                    return spawned._object;
            }
            if ( _pAsset->findSpawnItem( bindingID ) != nullptr )
                return GameObjectHandle{};
        }
        if ( _pListSlot == nullptr )
            return GameObjectHandle{};
        for ( const SequenceBindingSlot& slot : *_pListSlot )
        {
            if ( slot._binding == bindingID )
                return slot._object;
        }
        return GameObjectHandle{};
    }

    bool SequenceBindingView::hasSlot( uint32 bindingID ) const
    {
        if ( _pListSlot == nullptr )
            return false;
        for ( const SequenceBindingSlot& slot : *_pListSlot )
        {
            if ( slot._binding == bindingID )
                return true;
        }
        return false;
    }
} // namespace sw

namespace sw
{
    void SequenceTimelineUtil::applySpawns( GameObjectManager* pManager, const SequenceAsset& asset, int32 frame, vector<SequenceSpawnedObject>& inoutListSpawned )
    {
        if ( pManager == nullptr )
            return;
        for ( uint32 itemIndex = 0; itemIndex < static_cast<uint32>( asset._listItem.size() ); ++itemIndex )
        {
            const SequenceTrackItem&    item  = asset._listItem[itemIndex];
            const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( item._kind );
            if ( pInfo == nullptr || pInfo->_bSpawns == false )
                continue;
            const int32 windowStart = item._start - asset.getPrerollFrames( item );
            const bool  bInWindow   = windowStart <= frame && frame <= item._end;
            const bool  bShown      = item._start <= frame && frame <= item._end;

            size_t spawnedIndex = inoutListSpawned.size();
            for ( size_t index = 0; index < inoutListSpawned.size(); ++index )
            {
                if ( inoutListSpawned[index]._itemIndex == itemIndex )
                {
                    spawnedIndex = index;
                    break;
                }
            }
            const bool bSpawned = spawnedIndex < inoutListSpawned.size();

            if ( bInWindow == false )
            {
                if ( bSpawned )
                {
                    GameObject* pObject = pManager->resolveGameObject( inoutListSpawned[spawnedIndex]._object );
                    if ( pObject != nullptr )
                        pManager->destroyGameObject( pObject );
                    inoutListSpawned.erase( inoutListSpawned.begin() + static_cast<std::ptrdiff_t>( spawnedIndex ) );
                }
                continue;
            }

            if ( bSpawned == false )
            {
                // 스폰한 오브젝트의 이름은 바인딩 이름이다(읽기가 스폰 항목의 바인딩이 있음을 봤다).
                const SequenceBinding* pBinding = asset.findBinding( item._binding );
                const utf8*            pName    = pBinding != nullptr ? pBinding->_name.c_str() : item._name.c_str();
                AssetManager*          pAssets  = pManager->findAssetManager();
                GameObject*            pObject  = pAssets != nullptr ? pAssets->getAssetCache<PrefabCache>().spawn( pManager, item._prefab, pName ) : nullptr;
                SequenceSpawnedObject  spawned;
                spawned._itemIndex = itemIndex;
                if ( pObject == nullptr )
                {
                    SW_LOG_ERROR_ID( "SequenceTimeline.SequenceSpawnCouldNot", "Sequence spawn '%#' could not spawn prefab '%#' - the item spawns nothing until its span is left", item._name, item._prefab );
                    inoutListSpawned.push_back( spawned ); // 실패도 기억해 구간이 끝날 때까지 다시 시도하지 않는다
                    continue;
                }
                spawned._object = pObject->getHandle();
                inoutListSpawned.push_back( spawned );
                // 프리팹 상태는 틱 뒤에 채워지고 루트가 켜진 채 오므로, 프리롤 동안이면 그 뒤에 끈다(구조 변경 큐는 먼저 넣은 순서대로 돈다).
                if ( bShown == false )
                {
                    const GameObjectHandle handle = spawned._object;
                    pManager->executeOrDeferPostTick( [pManager, handle]()
                    {
                        GameObject* pSpawnedObject = pManager->resolveGameObject( handle );
                        if ( pSpawnedObject != nullptr )
                            pSpawnedObject->setActive( false );
                    } );
                }
                continue;
            }

            GameObject* pObject = pManager->resolveGameObject( inoutListSpawned[spawnedIndex]._object );
            if ( pObject != nullptr && pObject->isActive() != bShown )
                pObject->setActive( bShown );
        }
    }

    void SequenceTimelineUtil::releaseSpawns( GameObjectManager* pManager, vector<SequenceSpawnedObject>& inoutListSpawned )
    {
        if ( pManager != nullptr )
        {
            for ( const SequenceSpawnedObject& spawned : inoutListSpawned )
            {
                GameObject* pObject = pManager->resolveGameObject( spawned._object );
                if ( pObject != nullptr )
                    pManager->destroyGameObject( pObject );
            }
        }
        inoutListSpawned.clear();
    }

    void SequenceTimelineUtil::applyFrame( GameObjectManager* pManager, const SequenceAsset& asset, const SequenceBindingView& bindings, int32 frame, int32 previousFrame,
                                           vector<const SequenceTrackItem*>* pOutListCrossedEvent )
    {
        if ( pOutListCrossedEvent != nullptr )
            pOutListCrossedEvent->clear();
        if ( pManager == nullptr )
            return;

        vector<const SequenceTrackItem*> listActive;
        asset.collectActiveItems( frame, listActive );

        // 대상마다 원하는 상태는 하나다. 그 대상을 덮는 활성 클립이 있으면 켜짐이다. **바뀔 때만** 설정한다(클립마다 끄고 켜면
        // 클립이 둘인 대상이 매 프레임 꺼졌다 켜져 렌더 집합이 두 번 흔들린다).
        for ( const SequenceTrackItem& item : asset._listItem )
        {
            if ( SequenceTimelineUtilInternal::affectsTarget( item ) == false )
                continue;
            GameObject* pTarget = findTarget( pManager, bindings, item._binding );
            if ( pTarget == nullptr )
                continue;
            const bool bCovered = SequenceTimelineUtilInternal::isTargetCovered( listActive, item._binding );
            if ( pTarget->isActive() != bCovered )
                pTarget->setActive( bCovered );
        }

        if ( asset.hasCameraCut() )
            SequenceTimelineUtilInternal::applyCameraCut( pManager, asset, bindings, frame );

        const float32 keyFrame = static_cast<float32>( frame );
        for ( const SequenceKeyTrack& track : asset._listTrack )
        {
            GameObject* pTarget = findTarget( pManager, bindings, track._binding );
            if ( pTarget == nullptr )
                continue;
            switch ( track._kind )
            {
                case SequenceTrackKind::Transform:
                {
                    SequenceTimelineUtilInternal::applyTransformTrack( pTarget, track, keyFrame );
                    break;
                }
                case SequenceTrackKind::Property:
                {
                    SequenceTimelineUtilInternal::applyPropertyTrack( pTarget, track, keyFrame );
                    break;
                }
                case SequenceTrackKind::RigSlot:
                {
                    SequenceTimelineUtilInternal::applyRigSlotTrack( pTarget, track, keyFrame );
                    break;
                }
                case SequenceTrackKind::CameraLens:
                {
                    SequenceTimelineUtilInternal::applyLensTrack( pTarget, track, keyFrame );
                    break;
                }
                case SequenceTrackKind::Count:
                {
                    break;
                }
            }
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
            if ( SequenceTimelineUtilInternal::shouldFireOnCross( item ) == false )
                continue;
            // 지나갔는가: 이전 프레임에는 아직 닿지 않았고 이번 프레임에는 닿았다.
            if ( previousFrame >= item._start || item._start > frame )
                continue;
            if ( pOutListCrossedEvent != nullptr )
                pOutListCrossedEvent->push_back( &item );
            SW_LOG_INFO_ID( "SequenceTimeline.SequenceEvent", "Sequence event %# on binding %#", item._name.c_str(), item._binding );
        }
    }

    void SequenceTimelineUtil::applyPlayback( GameObjectManager* pManager, const SequencePlayer& player, const SequenceBindingView& bindings,
                                              vector<const SequenceTrackItem*>* pOutListCrossedEvent )
    {
        const SequenceAsset& asset           = player.getAsset();
        const int32          frameBeforeWrap = player.getFrameBeforeWrap();

        // 되감기 전 끝 구간을 먼저 모은다. `applyFrame` 이 출력을 비우므로 따로 모아 앞에 붙인다.
        vector<const SequenceTrackItem*> listTailEvent;
        if ( frameBeforeWrap != SequencePlayer::kNoLoopWrap )
            appendCrossedEvents( asset, frameBeforeWrap, asset._frameMax, &listTailEvent );

        applyFrame( pManager, asset, bindings, player.getCurrentFrame(), player.getPreviousFrame(), pOutListCrossedEvent );
        if ( pOutListCrossedEvent != nullptr && listTailEvent.empty() == false )
            pOutListCrossedEvent->insert( pOutListCrossedEvent->begin(), listTailEvent.begin(), listTailEvent.end() );
    }

    GameObject* SequenceTimelineUtil::findTarget( GameObjectManager* pManager, const SequenceBindingView& bindings, uint32 bindingID )
    {
        if ( pManager == nullptr || bindingID == kSequenceNoBinding )
            return nullptr;
        const GameObjectHandle handle = bindings.findHandle( bindingID );
        return handle.isValid() ? pManager->resolveGameObject( handle ) : nullptr;
    }

    void SequenceTimelineUtil::collectUnboundBindings( GameObjectManager* pManager, const SequenceAsset& asset, const SequenceBindingView& bindings, vector<uint32>& outListBindingID )
    {
        outListBindingID.clear();
        vector<uint32> listUsed;
        for ( const SequenceTrackItem& item : asset._listItem )
        {
            listUsed.push_back( item._binding );
        }
        for ( const SequenceKeyTrack& track : asset._listTrack )
        {
            listUsed.push_back( track._binding );
        }
        VectorUtil::sortUnique( listUsed );
        for ( const uint32 bindingID : listUsed )
        {
            const bool bFilledBySequence = bindingID == kSequenceNoBinding || asset.findSpawnItem( bindingID ) != nullptr;
            if ( bFilledBySequence == false && findTarget( pManager, bindings, bindingID ) == nullptr )
                outListBindingID.push_back( bindingID );
        }
    }

    bool SequenceTimelineUtil::findTrackProperty( GameObject* pTarget, const SequenceKeyTrack& track, Component*& pOutComponent, const PropertyInfo*& pOutProperty )
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
        if ( ReflectedValueUtil::getBuiltinClass( pProperty->_typeName ) != ReflectBuiltinClass::Number )
            return false;
        pOutComponent = pComponent;
        pOutProperty  = pProperty;
        return true;
    }

    bool SequenceTimelineUtil::readTrackValues( GameObject* pTarget, const SequenceKeyTrack& track, float32* pOutArrValue, uint32 valueCapacity )
    {
        const bool bInvalidSampleRequest = pTarget == nullptr || pOutArrValue == nullptr || valueCapacity < SequenceKeyTrack::getChannelCount( track._kind );
        if ( bInvalidSampleRequest )
            return false;
        switch ( track._kind )
        {
            case SequenceTrackKind::Transform:
            {
                const SceneComponent* pScene = pTarget->getPrimarySceneComponent();
                if ( pScene == nullptr )
                    return false;
                const float3 arrValue[3] = { pScene->getLocalPosition(), pScene->getLocalRotation(), pScene->getLocalScale() };
                for ( uint32 groupIndex = 0; groupIndex < 3; ++groupIndex )
                {
                    pOutArrValue[groupIndex * 3 + 0] = arrValue[groupIndex]._x;
                    pOutArrValue[groupIndex * 3 + 1] = arrValue[groupIndex]._y;
                    pOutArrValue[groupIndex * 3 + 2] = arrValue[groupIndex]._z;
                }
                return true;
            }
            case SequenceTrackKind::RigSlot:
            {
                const PoseModifierComponent* pModifier = pTarget->getComponent<PoseModifierComponent>();
                if ( pModifier == nullptr )
                    return false;
                pOutArrValue[0] = pModifier->getSlotWeight( hashed_string( track._propertyPath.c_str() ) );
                return true;
            }
            case SequenceTrackKind::CameraLens:
            {
                const CameraComponent* pCamera = pTarget->getComponent<CameraComponent>();
                if ( pCamera == nullptr )
                    return false;
                pOutArrValue[0] = pCamera->getFieldOfViewY();
                pOutArrValue[1] = pCamera->getDepthOfField()._focusDistance;
                pOutArrValue[2] = pCamera->getDepthOfField()._aperture;
                pOutArrValue[3] = pCamera->getLensShift()._x;
                pOutArrValue[4] = pCamera->getLensShift()._y;
                return true;
            }
            case SequenceTrackKind::Property:
            {
                Component*          pComponent{ nullptr };
                const PropertyInfo* pProperty{ nullptr };
                if ( findTrackProperty( pTarget, track, pComponent, pProperty ) == false )
                    return false;
                float64 number{ 0.0 };
                if ( ReflectedValueUtil::readNumber( ReflectedValueUtil::findBuiltinIndex( pProperty->_typeName ), pProperty->getRawPtr( static_cast<const void*>( pComponent ) ), number ) == false ) // findTrackProperty 가 숫자 갈래를 확인했다
                    return false;
                pOutArrValue[0] = static_cast<float32>( number );
                return true;
            }
            case SequenceTrackKind::Count:
            {
                return false;
            }
        }
        return false;
    }

    void SequenceTimelineUtil::writeTrackValues( GameObject* pTarget, const SequenceKeyTrack& track, const float32* pArrValue )
    {
        if ( pTarget == nullptr || pArrValue == nullptr )
            return;
        switch ( track._kind )
        {
            case SequenceTrackKind::Transform:
            {
                SceneComponent* pScene = pTarget->getPrimarySceneComponent();
                if ( pScene == nullptr )
                    return;
                pScene->setLocalPosition( float3{ pArrValue[0], pArrValue[1], pArrValue[2] } );
                pScene->setLocalRotation( float3{ pArrValue[3], pArrValue[4], pArrValue[5] } );
                pScene->setLocalScale( float3{ pArrValue[6], pArrValue[7], pArrValue[8] } );
                return;
            }
            case SequenceTrackKind::RigSlot:
            {
                PoseModifierComponent* pModifier = pTarget->getComponent<PoseModifierComponent>();
                if ( pModifier != nullptr )
                    pModifier->setSlotWeight( hashed_string( track._propertyPath.c_str() ), pArrValue[0] );
                return;
            }
            case SequenceTrackKind::CameraLens:
            {
                CameraComponent* pCamera = pTarget->getComponent<CameraComponent>();
                if ( pCamera != nullptr )
                    SequenceTimelineUtilInternal::writeLens( *pCamera, pArrValue, nullptr );
                return;
            }
            case SequenceTrackKind::Property:
            {
                Component*          pComponent{ nullptr };
                const PropertyInfo* pProperty{ nullptr };
                if ( findTrackProperty( pTarget, track, pComponent, pProperty ) )
                    (void)ReflectedValueUtil::writeNumber( ReflectedValueUtil::findBuiltinIndex( pProperty->_typeName ), pProperty->getRawPtr( static_cast<void*>( pComponent ) ),
                                                           static_cast<float64>( pArrValue[0] ) ); // findTrackProperty 가 숫자 갈래를 확인했다
                return;
            }
            case SequenceTrackKind::Count:
            {
                return;
            }
        }
    }

    float32 SequenceTimelineUtil::sampleRigSlotWeight( const SequenceKeyTrack& track, float32 frame )
    {
        // 무게 트랙에 키가 없으면 슬롯을 다 건다(1) — 무게 트랙의 계약이다.
        const optional<float32> weight = track.evaluateChannel( 0, frame );
        return weight.has_value() ? MathUtil::saturate( *weight ) : 1.0f;
    }

    void SequenceTimelineUtil::releaseCameraCut( GameObjectManager* pManager, const SequenceAsset& asset )
    {
        if ( pManager != nullptr && asset.hasCameraCut() )
            pManager->getCameraRegistry().setCutCamera( nullptr );
    }

    void SequenceTimelineUtil::applySpans( GameObjectManager* pManager, const SequenceAsset& asset, const SequenceBindingView& bindings, int32 frame, float32 framesPerSecond,
                                           vector<SequenceSpanState>& inoutListSpan, vector<const SequenceTrackItem*>* pOutListEntered )
    {
        for ( uint32 itemIndex = 0; itemIndex < static_cast<uint32>( asset._listItem.size() ); ++itemIndex )
        {
            const SequenceTrackItem&    item  = asset._listItem[itemIndex];
            const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( item._kind );
            if ( pInfo == nullptr || pInfo->_pSpanHandler == nullptr )
                continue;
            const SequenceSpanHandler& handler = *pInfo->_pSpanHandler;
            SequenceSpanState*         pState  = SequenceTimelineUtilInternal::findSpan( inoutListSpan, itemIndex );
            const bool                 bInside = item._start <= frame && frame <= item._end;
            const bool                 bWas    = pState != nullptr && pState->_bInside == SW_TRUE;
            if ( bInside == false && bWas == false )
                continue;
            GameObject* pTarget = findTarget( pManager, bindings, item._binding );
            // 대상이 필요한 항목인데 묶이지 않았으면 들어가지 않는다(재생은 시작 때 누락을 오류로 멈춘다).
            if ( bInside && bWas == false && item._binding != kSequenceNoBinding && pTarget == nullptr )
                continue;
            if ( pState == nullptr )
            {
                inoutListSpan.emplace_back( itemIndex );
                pState = &inoutListSpan.back();
            }
            SequenceSpanContext context;
            context._pManager        = pManager;
            context._pAsset          = &asset;
            context._pItem           = &item;
            context._pTarget         = pTarget;
            context._pState          = pState;
            context._framesPerSecond = framesPerSecond;
            context._frame           = frame;
            if ( bInside && bWas == false )
            {
                pState->_bInside = SW_TRUE;
                if ( handler._pEnter != nullptr )
                    handler._pEnter( context );
                if ( pOutListEntered != nullptr )
                    pOutListEntered->push_back( &item );
            }
            if ( bInside && handler._pUpdate != nullptr )
                handler._pUpdate( context );
            if ( bInside == false && bWas )
            {
                if ( handler._pLeave != nullptr )
                    handler._pLeave( context );
                pState->_bInside = SW_FALSE;
            }
        }
    }

    void SequenceTimelineUtil::releaseSpans( GameObjectManager* pManager, const SequenceAsset& asset, const SequenceBindingView& bindings, float32 framesPerSecond, vector<SequenceSpanState>& inoutListSpan )
    {
        for ( SequenceSpanState& state : inoutListSpan )
        {
            const bool bLive = state._bInside == SW_TRUE && state._itemIndex < asset._listItem.size();
            if ( bLive == false )
                continue;
            const SequenceTrackItem&    item  = asset._listItem[state._itemIndex];
            const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( item._kind );
            if ( pInfo == nullptr || pInfo->_pSpanHandler == nullptr || pInfo->_pSpanHandler->_pLeave == nullptr )
                continue;
            SequenceSpanContext context;
            context._pManager        = pManager;
            context._pAsset          = &asset;
            context._pItem           = &item;
            context._pTarget         = findTarget( pManager, bindings, item._binding );
            context._pState          = &state;
            context._framesPerSecond = framesPerSecond;
            context._frame           = item._end + 1;
            pInfo->_pSpanHandler->_pLeave( context );
        }
        inoutListSpan.clear();
    }

    void SequenceTimelineUtil::captureRestoreState( GameObjectManager* pManager, const SequenceAsset& asset, const SequenceBindingView& bindings, vector<SequenceRestoreEntry>& outListRestore )
    {
        outListRestore.clear();
        for ( uint32 itemIndex = 0; itemIndex < static_cast<uint32>( asset._listItem.size() ); ++itemIndex )
        {
            const SequenceTrackItem& item = asset._listItem[itemIndex];
            if ( SequenceTimelineUtilInternal::affectsTarget( item ) == false )
                continue;
            const GameObject* pTarget = findTarget( pManager, bindings, item._binding );
            if ( pTarget == nullptr )
                continue;
            SequenceRestoreEntry entry;
            entry._sourceIndex = itemIndex;
            entry._bActive     = pTarget->isActive() ? SW_TRUE : SW_FALSE;
            outListRestore.push_back( entry );
        }
        for ( uint32 trackIndex = 0; trackIndex < static_cast<uint32>( asset._listTrack.size() ); ++trackIndex )
        {
            const SequenceKeyTrack& track = asset._listTrack[trackIndex];
            SequenceRestoreEntry    entry;
            entry._sourceIndex = trackIndex;
            entry._bFromTrack  = SW_TRUE;
            if ( readTrackValues( findTarget( pManager, bindings, track._binding ), track, entry._arrValue, kSequenceMaxChannelCount ) )
                outListRestore.push_back( entry );
        }
    }

    void SequenceTimelineUtil::applyRestoreState( GameObjectManager* pManager, const SequenceAsset& asset, const SequenceBindingView& bindings, const vector<SequenceRestoreEntry>& listRestore )
    {
        // 거꾸로 돈다 — 같은 대상을 두 트랙이 몰았으면 먼저 기억한 값(재생 전의 값)이 마지막에 남는다.
        for ( auto it = listRestore.rbegin(); it != listRestore.rend(); ++it )
        {
            const SequenceRestoreEntry& entry = *it;
            if ( entry._bFromTrack == SW_TRUE )
            {
                if ( entry._sourceIndex < asset._listTrack.size() )
                {
                    const SequenceKeyTrack& track = asset._listTrack[entry._sourceIndex];
                    writeTrackValues( findTarget( pManager, bindings, track._binding ), track, entry._arrValue );
                }
                continue;
            }
            if ( entry._sourceIndex >= asset._listItem.size() )
                continue;
            GameObject* pTarget = findTarget( pManager, bindings, asset._listItem[entry._sourceIndex]._binding );
            const bool  bActive = entry._bActive == SW_TRUE;
            if ( pTarget != nullptr && pTarget->isActive() != bActive )
                pTarget->setActive( bActive );
        }
    }

    void SequenceTimelineUtil::collectCameraSignals( const SequenceAsset& asset, int32 previousFrame, int32 frame, vector<SequenceCameraSignal>& outListSignal )
    {
        const SequenceTrackItem* pPreviousCut = previousFrame == kNoPreviousFrame ? nullptr : asset.findCameraCut( previousFrame );
        appendCameraSignals( asset, previousFrame, frame, pPreviousCut, outListSignal );
    }

    void SequenceTimelineUtil::collectPlaybackCameraSignals( const SequencePlayer& player, vector<SequenceCameraSignal>& outListSignal )
    {
        const SequenceAsset& asset           = player.getAsset();
        const int32          frameBeforeWrap = player.getFrameBeforeWrap();
        if ( frameBeforeWrap == SequencePlayer::kNoLoopWrap )
        {
            collectCameraSignals( asset, player.getPreviousFrame(), player.getCurrentFrame(), outListSignal );
            return;
        }
        // 되감았다 — 끝 구간을 먼저 보고, 처음 구간은 끝을 덮던 컷에서 이어진 것으로 본다(같은 컷이 끝과 처음을 덮으면 시네마틱이 끊기지 않는다).
        appendCameraSignals( asset, frameBeforeWrap, asset._frameMax, asset.findCameraCut( frameBeforeWrap ), outListSignal );
        appendCameraSignals( asset, player.getPreviousFrame(), player.getCurrentFrame(), asset.findCameraCut( asset._frameMax ), outListSignal );
    }

    void SequenceTimelineUtil::appendCameraSignals( const SequenceAsset& asset, int32 previousFrame, int32 frame, const SequenceTrackItem* pPreviousCut,
                                                    vector<SequenceCameraSignal>& outListSignal )
    {
        // 컷이 바뀌었는가 — 덮는 컷의 시작이 이번 구간에 있거나, 이전에 덮던 컷과 다르다.
        const SequenceTrackItem* pCut = asset.findCameraCut( frame );
        if ( pCut != nullptr && pCut != pPreviousCut )
        {
            SequenceCameraSignal signal;
            signal._item = *pCut;
            signal._kind = SequenceCameraSignalKind::CutBegin;
            outListSignal.push_back( signal );
        }
        if ( pCut != nullptr && pPreviousCut == nullptr )
        {
            SequenceCameraSignal signal;
            signal._item         = *pCut;
            signal._blendSeconds = asset._cameraBlendInSeconds;
            signal._kind         = SequenceCameraSignalKind::CinematicBegin;
            outListSignal.push_back( signal );
        }
        if ( pCut == nullptr && pPreviousCut != nullptr )
        {
            SequenceCameraSignal signal;
            signal._item         = *pPreviousCut;
            signal._blendSeconds = asset._cameraBlendOutSeconds;
            signal._kind         = SequenceCameraSignalKind::CinematicEnd;
            outListSignal.push_back( signal );
        }

        const bool bHasPrevious = previousFrame != kNoPreviousFrame;
        for ( const SequenceTrackItem& item : asset._listItem )
        {
            const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( item._kind );
            if ( pInfo == nullptr )
                continue;
            // 대기 뷰 — 컷 시작 프리롤 프레임 전을 이번 구간에 지났고 컷은 아직 시작하지 않았다.
            const int32 prerollFrames = pInfo->_bCutsCamera ? asset.getPrerollFrames( item ) : 0;
            if ( prerollFrames > 0 && frame < item._start )
            {
                const int32 prewarmFrame = item._start - prerollFrames;
                const bool  bCrossed     = bHasPrevious ? ( previousFrame < prewarmFrame && prewarmFrame <= frame ) : prewarmFrame <= frame;
                if ( bCrossed )
                {
                    SequenceCameraSignal signal;
                    signal._item        = item;
                    signal._framesUntil = item._start - frame;
                    signal._kind        = SequenceCameraSignalKind::CutPrewarm;
                    outListSignal.push_back( signal );
                }
            }
            // 흔들림 — 시작 프레임을 이번 구간에 지났다.
            if ( pInfo->_bShakesCamera )
            {
                const bool bStarted = bHasPrevious ? ( previousFrame < item._start && item._start <= frame ) : item._start == frame;
                if ( bStarted )
                {
                    SequenceCameraSignal signal;
                    signal._item = item;
                    signal._kind = SequenceCameraSignalKind::Shake;
                    outListSignal.push_back( signal );
                }
            }
        }
    }
} // namespace sw
