#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearanceComponent.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Character/PoseModifier/CharacterPoseUtil.h"
#include "Engine/Character/Socket/SocketBindingComponent.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearance.h"

namespace sw
{
    SW_LOG_CALLER( "CharacterAppearance" );

    namespace
    {
        struct CharacterAppearanceComponentInternal
        {
            static bool isSamePart( const ResolvedPart& part, const hashed_string& owner, const hashed_string& partName, const hashed_string& asset )
            {
                return part._owner == owner && part._partName == partName && part._asset == asset;
            }

            /** @brief 오브젝트의 메시 컴포넌트마다 @p func 를 부릅니다. */
            template <typename Func>
            static void forEachMesh( const GameObject& object, Func&& func )
            {
                object.forEachComponentOfType<MeshComponent>( [&func]( MeshComponent* pMesh )
                { func( *pMesh ); } );
            }

            /** @brief 탐침 대상 오브젝트의 외형 컴포넌트입니다. 없으면 @p outValue 에 까닭을 적고 nullptr 입니다. */
            static const CharacterAppearanceComponent* findProbeTarget( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const GameObject* pObject = query.findTargetObject();
                if ( pObject == nullptr )
                {
                    outValue.setText( query.getTargetProblem() );
                    return nullptr;
                }
                const CharacterAppearanceComponent* pAppearance = pObject->getComponent<CharacterAppearanceComponent>();
                if ( pAppearance == nullptr )
                    outValue.setText( string( "'" ) + pObject->getName().c_str() + "' has no CharacterAppearanceComponent" );
                return pAppearance;
            }

            /** @brief 조립한 외형의 프리셋 id 입니다. 아직 조립하지 않았으면 빈 글입니다. */
            [[nodiscard]] static bool readPreset( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const CharacterAppearanceComponent* pAppearance = findProbeTarget( query, outValue );
                if ( pAppearance == nullptr )
                    return false;
                outValue.setText( pAppearance->getResolvedAppearance()._presetID.c_str() );
                return true;
            }

            /** @brief 조립한 외형에서 슬롯(@p query 의 key)에 입은 아이템 id 입니다. 빈 슬롯이면 `(empty)` 입니다. */
            [[nodiscard]] static bool readSlotItem( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const CharacterAppearanceComponent* pAppearance = findProbeTarget( query, outValue );
                if ( pAppearance == nullptr )
                    return false;
                const hashed_string slot( query._key );
                for ( const ResolvedPart& part : pAppearance->getResolvedAppearance()._listPart )
                {
                    if ( part._owner == slot && part._itemID.empty() == false )
                    {
                        outValue.setText( part._itemID.c_str() );
                        return true;
                    }
                }
                outValue.setText( "(empty)" );
                return true;
            }

            /** @brief 메시에 머티리얼 값을 적용합니다. 메시 전용 인스턴스에 넣고, 없으면 메시의 머티리얼에서 새로 만듭니다. */
            static void applyParameter( MeshComponent& mesh, const ResolvedMaterialValue& value )
            {
                shared_ptr<MaterialInstance> instance = mesh.getMaterialInstance();
                if ( instance == nullptr )
                {
                    if ( mesh.getMaterial() == nullptr )
                        return;
                    instance = MaterialInstance::create( mesh.getMaterial() );
                    if ( instance == nullptr )
                        return;
                    mesh.setMaterialInstance( instance );
                }
                instance->setVectorParameter( value._name, value._value );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CharacterAppearanceSocketTask::CharacterAppearanceSocketTask( CharacterAppearanceComponent& owner )
        : _owner{ owner }
    {
    }

    void CharacterAppearanceSocketTask::runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context )
    {
        (void)phase;
        (void)unit;
        (void)context;
    }

    void CharacterAppearanceSocketTask::finishAnimationFrame( SkeletalMeshComponent& unit )
    {
        _owner.refreshSocketTransforms( unit );
    }

    void CharacterAppearanceSocketTask::onAnimationUnitDetached( SkeletalMeshComponent& unit )
    {
        (void)unit;
        _owner._taskUnit = TComponentHandle<SkeletalMeshComponent>{};
    }
} // namespace sw

namespace sw
{
    CharacterAppearanceComponent::CharacterAppearanceComponent()
        : _presetID{}
        , _seed{ 0 }
        , _socketTask{ *this }
        , _rig{}
        , _socketCache{}
        , _resolved{}
        , _bodyBindBones{}
        , _bodyBones{}
        , _listSpawnedPart{}
        , _listSlotOverride{}
        , _taskUnit{}
        , _previewSelection{}
        , _pPreviewDatabase{ nullptr }
        , _assembleCount{ 0 }
        , _bodyPart{ -1 }
        , _bPartsVisible{ true }
        , _bAssembleScheduled{ SW_FALSE }
        , _bStarted{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    CharacterAppearanceComponent::~CharacterAppearanceComponent() = default;

    void CharacterAppearanceComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _bStarted = SW_TRUE;
        if ( _presetID.empty() == false )
            requestAssemble();
    }

    void CharacterAppearanceComponent::onEndPlay()
    {
        unregisterSocketTask();
        despawnParts();
        _bStarted = SW_FALSE;
        Component::onEndPlay();
    }

    void CharacterAppearanceComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        if ( _bStarted == SW_TRUE && ( propertyName == hashed_string( "_presetID" ) || propertyName == hashed_string( "_seed" ) ) )
            requestAssemble();
    }

    void CharacterAppearanceComponent::setPreset( const hashed_string& presetID, uint32 seed )
    {
        _presetID = string{ presetID.c_str() };
        _seed     = static_cast<int32>( seed );
        if ( _bStarted == SW_TRUE )
            requestAssemble();
    }

    void CharacterAppearanceComponent::previewSelection( const AppearanceSelection& selection, const AppearanceDatabase& database )
    {
        _previewSelection = selection;
        _pPreviewDatabase = &database;
        requestAssemble(); // 미리보기 씬은 플레이를 돌리지 않는다 — 시작을 기다리지 않는다
    }

    void CharacterAppearanceComponent::setSlotItem( const hashed_string& slot, const hashed_string& itemID )
    {
        for ( SlotOverride& slotOverride : _listSlotOverride )
        {
            if ( slotOverride._slot != slot )
                continue;
            if ( slotOverride._itemID == itemID )
                return;
            slotOverride._itemID = itemID;
            if ( _bStarted == SW_TRUE )
                requestAssemble();
            return;
        }
        _listSlotOverride.push_back( SlotOverride{ slot, itemID } );
        if ( _bStarted == SW_TRUE )
            requestAssemble();
    }

    void CharacterAppearanceComponent::setPartsVisible( bool bVisible )
    {
        if ( _bPartsVisible == bVisible )
            return;
        _bPartsVisible              = bVisible;
        GameObjectManager* pManager = findManager();
        if ( pManager == nullptr )
            return;
        // 메시 보임은 렌더 상태이므로, 틱 안이면 틱 뒤에 적용한다.
        pManager->executeOrDeferPostTickOnComponent( *this,
                                                     []( CharacterAppearanceComponent& appearance, GameObjectManager& manager )
        { appearance.applyVisibility( manager ); } );
    }

    void CharacterAppearanceComponent::despawnParts()
    {
        GameObjectManager* pManager = findManager();
        if ( pManager != nullptr )
        {
            for ( const SpawnedPart& part : _listSpawnedPart )
            {
                GameObject* pObject = pManager->resolveGameObject( part._object );
                if ( pObject != nullptr )
                    pManager->destroyObject( pObject );
            }
        }
        _listSpawnedPart.clear();
    }

    bool CharacterAppearanceComponent::findSocketWorldTransform( const hashed_string& fullName, float4x4& outWorldTransform ) const
    {
        GameObjectManager* pManager = findManager();
        if ( pManager == nullptr || _assembleCount == 0 )
            return false;
        // 유닛 번호 순서의 월드이다. 몸은 이 오브젝트이고, 부품은 그 부품 오브젝트이며, 지난 프레임에 적용된 위치를 쓴다.
        vector<float4x4> listUnitWorld;
        listUnitWorld.reserve( _rig.getUnitCount() );
        for ( uint32 unitIndex = 0; unitIndex < _rig.getUnitCount(); ++unitIndex )
        {
            const GameObject*     pObject = findUnitObject( *pManager, unitIndex );
            const SceneComponent* pScene  = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pScene == nullptr )
                break;
            listUnitWorld.push_back( pScene->getWorldMatrix() );
        }
        return _rig.findSocketWorldTransform( fullName, _bodyBones, vector_reference<const float4x4>( listUnitWorld ), outWorldTransform );
    }

    bool CharacterAppearanceComponent::findBindSocketTransform( const hashed_string& fullName, float4x4& outUnitTransform ) const
    {
        const float4x4 arrIdentity[1] = { float4x4::Identity };
        return _rig.findSocketWorldTransform( fullName, _bodyBindBones, vector_reference<const float4x4>( arrIdentity ), outUnitTransform );
    }

    void CharacterAppearanceComponent::collectPartObjects( vector<GameObjectHandle>& outListObject ) const
    {
        outListObject.clear();
        for ( const SpawnedPart& part : _listSpawnedPart )
        {
            outListObject.push_back( part._object );
        }
    }

    // ------------------------------------------------------------------------------
    // 조립(게임 스레드, 틱 밖)
    // ------------------------------------------------------------------------------
    void CharacterAppearanceComponent::requestAssemble()
    {
        GameObjectManager* pManager = findManager();
        if ( pManager == nullptr || _bAssembleScheduled == SW_TRUE )
            return;
        _bAssembleScheduled = SW_TRUE;
        pManager->executeOrDeferPostTickOnComponent( *this, []( CharacterAppearanceComponent& appearance )
        {
            appearance._bAssembleScheduled = SW_FALSE;
            appearance.assemble();
        } );
    }

    void CharacterAppearanceComponent::assemble()
    {
        GameObject*            pOwner   = getOwner();
        GameObjectManager*     pManager = findManager();
        SkeletalMeshComponent* pBody    = pOwner != nullptr ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        // 미리보기 입히기는 창이 준 데이터만, 아니면 게임 서비스만 본다(`previewSelection`) — 한쪽이 없다고 다른 쪽으로 물러서지 않는다.
        const AppearanceDatabase* pDatabase = _pPreviewDatabase != nullptr ? _pPreviewDatabase : game::findSubsystem<AppearanceDatabase>();
        if ( pManager == nullptr || pBody == nullptr )
        {
            SW_LOG_WARNING( "'%#' has no skeletal mesh to wear appearance '%#'", pOwner != nullptr ? pOwner->getName().c_str() : "?", _presetID.c_str() );
            return;
        }
        if ( pDatabase == nullptr )
        {
            SW_LOG_ERROR( "Appearance '%#' cannot be assembled - no AppearanceDatabase game service", _presetID.c_str() );
            return;
        }

        CharacterAppearanceSpec spec;
        if ( expandSpec( *pDatabase, spec ) == false )
            return;
        for ( const SlotOverride& slotOverride : _listSlotOverride )
        {
            AppearanceSlotRequest* pSlot = spec.findSlot( slotOverride._slot );
            if ( pSlot == nullptr )
            {
                spec._listSlot.push_back( AppearanceSlotRequest{} );
                pSlot        = &spec._listSlot.back();
                pSlot->_slot = slotOverride._slot;
            }
            pSlot->_itemID        = slotOverride._itemID;
            pSlot->_visibleVisual = hashed_string{};
            pSlot->_state         = hashed_string{};
        }
        AppearanceResolver::resolve( *pDatabase, spec, _resolved );

        _bodyPart = applyBodyPart( *pBody );
        CharacterPoseUtil::makeBindBones( pBody->getSkeleton(), _bodyBindBones );
        _bodyBones = _bodyBindBones;
        (void)CharacterPoseUtil::copyUnitPose( *pBody, _bodyBones ); // 포즈가 아직 없으면 바인드 본 그대로 소켓을 계산한다
        string socketError;
        if ( _rig.rebuild( _resolved, _bodyBindBones, _socketCache, &socketError ) == false )
            SW_LOG_ERROR( "Appearance '%#' sockets: %#", _presetID.c_str(), socketError.c_str() );

        syncParts( *pManager, *pBody, _bodyPart );
        bindSocketParts( *pManager );
        applyMaterialValues( *pManager, *pBody );
        applyVisibility( *pManager );
        registerSocketTask( *pBody );
        ++_assembleCount;
    }

    bool CharacterAppearanceComponent::expandSpec( const AppearanceDatabase& database, CharacterAppearanceSpec& outSpec ) const
    {
        const bool              bPreview = _pPreviewDatabase != nullptr;
        const hashed_string     presetID = bPreview ? _previewSelection._basePresetID : hashed_string( _presetID );
        const uint32            seed     = bPreview ? _previewSelection._seed : static_cast<uint32>( _seed );
        CharacterAppearanceSpec base;
        if ( database.getPresets().expand( presetID, seed, database.getSlotTable(), database.getSets(), database.getSchemas(), base ) == false )
        {
            SW_LOG_ERROR( "Appearance preset '%#' could not be expanded", presetID.c_str() );
            return false;
        }
        if ( bPreview == false )
        {
            outSpec = std::move( base );
            return true;
        }
        AppearanceSelectionReport report;
        AppearanceSelectionUtil::applySelection( database, _previewSelection, base, nullptr, outSpec, report );
        if ( report.isClean() == false )
            SW_LOG_WARNING( "Previewed appearance on base '%#' dropped %# values and reset %# slots to their defaults", presetID.c_str(),
                            static_cast<uint32>( report._listDroppedParameter.size() ), static_cast<uint32>( report._listSlotFallback.size() ) );
        return true;
    }

    int32 CharacterAppearanceComponent::applyBodyPart( SkeletalMeshComponent& body )
    {
        for ( size_t partIndex = 0; partIndex < _resolved._listPart.size(); ++partIndex )
        {
            const ResolvedPart& part = _resolved._listPart[partIndex];
            if ( part._owner.empty() == false || part._kind != AppearancePartKind::Skinned )
                continue;
            // 스켈레톤을 먼저 적용한다. 메시의 스킨 사본이 그 본 수를 참조하기 때문이다.
            // 적지 않은 스켈레톤은 빈 경로(암묵 스켈레톤)다 — 빈 이름의 `c_str()` 은 "None" 이라 그대로 넘기면 경로 "None" 이 된다(C-59).
            const string_view skeletonPath = part._skeleton.empty() ? string_view{} : string_view( part._skeleton.c_str() );
            if ( string_view( body.getSkeletonPath() ) != skeletonPath )
                body.setSkeletonPath( skeletonPath );
            if ( string_view( body.getMeshID() ) != string_view( part._asset.c_str() ) )
                body.setMeshID( part._asset.c_str() );
            if ( part._material.empty() == false && body.getMaterialPath() != part._material )
                body.setMaterialPath( part._material.c_str() );
            return static_cast<int32>( partIndex );
        }
        SW_LOG_WARNING( "Appearance '%#' has no body part (a Skinned part without an owner)", _presetID.c_str() );
        return -1;
    }

    void CharacterAppearanceComponent::syncParts( GameObjectManager& manager, SkeletalMeshComponent& body, int32 bodyPart )
    {
        using Internal = CharacterAppearanceComponentInternal;
        // 남길 것을 고르고 없어진 것은 지운다. 같은 부품(주인, 이름, 에셋)은 그 오브젝트를 그대로 쓴다. 그래서 무기를 바꿔도 투구는 그대로 남는다.
        vector<SpawnedPart> listKeep;
        vector<uint8>       listPartCovered( _resolved._listPart.size(), SW_FALSE );
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            int32 matched = -1;
            for ( size_t partIndex = 0; partIndex < _resolved._listPart.size(); ++partIndex )
            {
                const bool bCandidate = listPartCovered[partIndex] == SW_FALSE && static_cast<int32>( partIndex ) != bodyPart;
                if ( bCandidate && Internal::isSamePart( _resolved._listPart[partIndex], spawned._owner, spawned._partName, spawned._asset ) )
                {
                    matched = static_cast<int32>( partIndex );
                    break;
                }
            }
            GameObject* pObject = manager.resolveGameObject( spawned._object );
            if ( matched < 0 || pObject == nullptr )
            {
                if ( pObject != nullptr )
                    manager.destroyObject( pObject );
                continue;
            }
            listPartCovered[static_cast<size_t>( matched )] = SW_TRUE;
            SpawnedPart kept                                = spawned;
            kept._partIndex                                 = static_cast<uint32>( matched );
            listKeep.push_back( kept );
        }
        _listSpawnedPart = std::move( listKeep );

        AssetManager*                                 pAssetManager = game::findSubsystem<AssetManager>();
        SceneComponent*                               pBodyScene    = body.getOwner() != nullptr ? body.getOwner()->getPrimarySceneComponent() : nullptr;
        const TComponentHandle<SkeletalMeshComponent> bodyHandle{ &body };
        for ( size_t partIndex = 0; partIndex < _resolved._listPart.size(); ++partIndex )
        {
            const ResolvedPart& part      = _resolved._listPart[partIndex];
            const bool          bSkipPart = listPartCovered[partIndex] == SW_TRUE || static_cast<int32>( partIndex ) == bodyPart || part._asset.empty();
            if ( bSkipPart )
                continue;
            GameObject* pObject = nullptr;
            SpawnedPart spawned;
            spawned._owner     = part._owner;
            spawned._partName  = part._partName;
            spawned._asset     = part._asset;
            spawned._partIndex = static_cast<uint32>( partIndex );
            if ( part._kind == AppearancePartKind::SocketPrefab )
            {
                pObject = pAssetManager != nullptr ? pAssetManager->getAssetCache<PrefabCache>().spawn( &manager, part._asset.c_str(), part._partName.c_str() ) : nullptr;
                if ( pObject != nullptr && pObject->getComponent<SocketBindingComponent>() == nullptr )
                    (void)pObject->addComponent<SocketBindingComponent>();
            }
            else if ( part._kind == AppearancePartKind::Skinned )
            {
                // 몸을 리더로 따르는 스킨드 부품(옷, 머리카락)이다. 몸의 자식이 되고 위치는 몸과 같다.
                pObject                         = manager.createGameObject( part._partName );
                SkeletalMeshComponent* pSkinned = pObject->addComponent<SkeletalMeshComponent>();
                pSkinned->setSkeletonPath( part._skeleton.empty() ? string_view( body.getSkeletonPath() ) : string_view( part._skeleton.c_str() ) );
                pSkinned->setMeshID( part._asset.c_str() );
                if ( part._material.empty() == false )
                    pSkinned->setMaterialPath( part._material.c_str() );
                pSkinned->setLeaderPose( manager.resolveComponent( bodyHandle ) );
                if ( pBodyScene != nullptr )
                    (void)pSkinned->attachToComponent( pBodyScene, AttachRule::KeepRelative ); // 붙일 수 없는 부모이면 false 이고, 그 부위는 제자리에 남는다
                spawned._bSocketPart = SW_FALSE;
            }
            if ( pObject == nullptr )
            {
                SW_LOG_WARNING( "Appearance '%#' part '%#' (%#) could not be spawned", _presetID.c_str(), part._partName.c_str(), part._asset.c_str() );
                continue;
            }
            spawned._object = pObject->getHandle();
            _listSpawnedPart.push_back( spawned );
        }
    }

    void CharacterAppearanceComponent::bindSocketParts( GameObjectManager& manager )
    {
        for ( SpawnedPart& spawned : _listSpawnedPart )
        {
            if ( spawned._bSocketPart == SW_FALSE )
                continue;
            GameObject*             pObject  = manager.resolveGameObject( spawned._object );
            SocketBindingComponent* pBinding = pObject != nullptr ? pObject->getComponent<SocketBindingComponent>() : nullptr;
            const ResolvedPart&     part     = _resolved._listPart[spawned._partIndex];
            float4x4                inUnit;
            uint32                  unitIndex = AppearanceSocketRig::kNoUnit;
            if ( pBinding == nullptr || _rig.computePlacement( part._placement, _bodyBones, inUnit, unitIndex ) == false )
            {
                SW_LOG_WARNING( "Appearance '%#' part '%#' has no active socket among its candidates", _presetID.c_str(), part._partName.c_str() );
                continue;
            }
            GameObject* pHolder = findUnitObject( manager, unitIndex );
            if ( pHolder == nullptr || pHolder == pObject )
                continue;
            spawned._holderUnit            = unitIndex;
            const hashed_string socketName = part._placement._listSocket.empty() ? hashed_string{} : part._placement._listSocket[0];
            // 같은 holder 와 소켓이면 위치만 고친다. 다시 붙이면 되돌아가기 블렌드가 끊기기 때문이다.
            const bool bSameSocketBinding = pBinding->getState() == SocketBindingState::Bound && pBinding->getHolder() == pHolder->getHandle() && pBinding->getSocketName() == socketName;
            if ( bSameSocketBinding )
                pBinding->updateSocketTransform( inUnit );
            else
                (void)pBinding->bindToSocket( pHolder, socketName, inUnit );
        }
    }

    void CharacterAppearanceComponent::applyMaterialValues( GameObjectManager& manager, SkeletalMeshComponent& body )
    {
        using Internal = CharacterAppearanceComponentInternal;
        for ( const ResolvedMaterialValue& value : _resolved._listMaterialValue )
        {
            if ( value._target != ResolvedMaterialTarget::Parameter )
                continue; // 염색 마스크 채널과 팔레트 슬롯은 그것을 읽는 머티리얼이 생기면 적용한다
            if ( value._owner.empty() && ( value._part.empty() || ( _bodyPart >= 0 && _resolved._listPart[static_cast<size_t>( _bodyPart )]._partName == value._part ) ) )
                Internal::applyParameter( body, value );
            for ( const SpawnedPart& spawned : _listSpawnedPart )
            {
                const bool  bOwned  = spawned._owner == value._owner && ( value._part.empty() || spawned._partName == value._part );
                GameObject* pObject = bOwned ? manager.resolveGameObject( spawned._object ) : nullptr;
                if ( pObject != nullptr )
                    Internal::forEachMesh( *pObject, [&value]( MeshComponent& mesh )
                    { Internal::applyParameter( mesh, value ); } );
            }
        }
    }

    void CharacterAppearanceComponent::applyVisibility( GameObjectManager& manager )
    {
        using Internal     = CharacterAppearanceComponentInternal;
        GameObject* pOwner = getOwner();
        if ( pOwner != nullptr )
            Internal::forEachMesh( *pOwner, [this]( MeshComponent& mesh )
            { mesh.setVisible( _bPartsVisible ); } );
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            GameObject* pObject = manager.resolveGameObject( spawned._object );
            if ( pObject != nullptr )
                Internal::forEachMesh( *pObject, [this]( MeshComponent& mesh )
                { mesh.setVisible( _bPartsVisible ); } );
        }
    }

    void CharacterAppearanceComponent::refreshSocketTransforms( const SkeletalMeshComponent& body )
    {
        GameObjectManager* pManager = findManager();
        if ( pManager == nullptr || CharacterPoseUtil::copyUnitPose( body, _bodyBones ) == false )
            return;
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            if ( spawned._bSocketPart == SW_FALSE || spawned._holderUnit != AppearanceSocketRig::kBodyUnit )
                continue;
            GameObject*             pObject  = pManager->resolveGameObject( spawned._object );
            SocketBindingComponent* pBinding = pObject != nullptr ? pObject->getComponent<SocketBindingComponent>() : nullptr;
            float4x4                inUnit;
            uint32                  unitIndex = AppearanceSocketRig::kNoUnit;
            if ( pBinding != nullptr && _rig.computePlacement( _resolved._listPart[spawned._partIndex]._placement, _bodyBones, inUnit, unitIndex ) )
                pBinding->updateSocketTransform( inUnit );
        }
    }

    void CharacterAppearanceComponent::registerSocketTask( SkeletalMeshComponent& body )
    {
        const TComponentHandle<SkeletalMeshComponent> bodyHandle{ &body };
        if ( _taskUnit == bodyHandle )
            return;
        unregisterSocketTask();
        body.registerAnimationPhaseTask( &_socketTask );
        _taskUnit = bodyHandle;
    }

    void CharacterAppearanceComponent::unregisterSocketTask()
    {
        GameObjectManager*     pManager = findManager();
        SkeletalMeshComponent* pUnit    = pManager != nullptr ? pManager->resolveComponent( _taskUnit ) : nullptr;
        _taskUnit                       = TComponentHandle<SkeletalMeshComponent>{};
        if ( pUnit != nullptr )
            pUnit->unregisterAnimationPhaseTask( &_socketTask );
    }

    GameObject* CharacterAppearanceComponent::findUnitObject( GameObjectManager& manager, uint32 unitIndex ) const
    {
        if ( unitIndex == AppearanceSocketRig::kBodyUnit )
            return getOwner();
        const uint32 partIndex = _rig.getUnitPart( unitIndex );
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            if ( spawned._partIndex == partIndex )
                return manager.resolveGameObject( spawned._object );
        }
        return nullptr;
    }

    GameObjectManager* CharacterAppearanceComponent::findManager() const
    {
        GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getManager() : nullptr;
    }
} // namespace sw

namespace sw
{
    SW_AUTOMATION_QUERY_PROBE( appearancePreset, "Appearance.Preset", "Preset id the object's CharacterAppearanceComponent last assembled",
                               &CharacterAppearanceComponentInternal::readPreset, true, false );
    SW_AUTOMATION_QUERY_PROBE( appearanceSlotItem, "Appearance.SlotItem", "Item the assembled appearance wears in the slot named by key ('(empty)' when the slot is empty)",
                               &CharacterAppearanceComponentInternal::readSlotItem, true, true );
} // namespace sw
