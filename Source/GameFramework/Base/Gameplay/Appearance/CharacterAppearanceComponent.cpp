#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearanceComponent.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/formatString.h"

#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/Fit/FitPartData.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Fit/FitTables.h"
#include "Engine/Character/PoseModifier/CharacterPoseUtil.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Character/Socket/SocketBindingComponent.h"
#include "Engine/Character/Socket/SocketPartPool.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Resource/AssetManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceMeshAssembly.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceXMLUtil.h"
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

            /** @brief 보고에 한 줄을 덧붙입니다. */
            template <typename... Args>
            static void appendReport( string& inoutReport, string_view format, Args&&... args )
            {
                utf8 arrMessage[constant::kMaxBuffer512]{};
                formatstring( arrMessage, constant::kMaxBuffer512, format, std::forward<Args>( args )... );
                if ( inoutReport.empty() == false )
                    inoutReport += "\n";
                inoutReport += arrMessage;
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
    CharacterAppearanceProportionTask::CharacterAppearanceProportionTask( CharacterAppearanceComponent& owner )
        : _owner{ owner }
    {
    }

    void CharacterAppearanceProportionTask::runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context )
    {
        (void)context;
        const BoneProportion& proportion = _owner.getBodyProportion();
        if ( phase != AnimationPhase::Attachment || proportion.getEntries().empty() )
            return;
        proportion.applyToPose( unit.getSkeleton(), unit.getLocalPose() );
    }

    void CharacterAppearanceProportionTask::onAnimationUnitDetached( SkeletalMeshComponent& unit )
    {
        (void)unit;
        _owner._bProportionTask = SW_FALSE;
    }
} // namespace sw

namespace sw
{
    CharacterAppearanceComponent::CharacterAppearanceComponent()
        : _presetID{}
        , _seed{ 0 }
        , _socketTask{ *this }
        , _proportionTask{ *this }
        , _bodyProportion{}
        , _listShapeMorph{}
        , _bodyShapeSet{}
        , _seenShapeReloadCount{ 0 }
        , _rig{}
        , _socketCache{}
        , _state{}
        , _pEquipment{ nullptr }
        , _bodyBindBones{}
        , _bodyBones{}
        , _listSpawnedPart{}
        , _listSlotOverride{}
        , _listHiddenSlot{}
        , _listRigUnit{}
        , _taskUnit{}
        , _previewSelection{}
        , _pPreviewDatabase{ nullptr }
        , _pReloadDatabase{ nullptr }
        , _reloadSubscription{}
        , _listMeshAssemblyLoad{}
        , _fitTablesLoad{}
        , _lastFitResult{}
        , _fitResultRevision{ 0 }
        , _assembleCount{ 0 }
        , _seenFitReloadStamp{ 0 }
        , _bodyPart{ -1 }
        , _bPartsVisible{ true }
        , _bAssembleScheduled{ SW_FALSE }
        , _bStarted{ SW_FALSE }
        , _bMeshAssemblyPending{ SW_FALSE }
        , _bMeshAssembled{ SW_FALSE }
        , _bProportionTask{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    CharacterAppearanceComponent::~CharacterAppearanceComponent() = default;

    void CharacterAppearanceComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _bStarted = SW_TRUE;
        // 핫 리로드: 외형 데이터를 다시 읽으면(`AppearanceDatabase::reloadFromFolder`) 다시 조립한다 — 상태가 데이터 버전으로 다시 해석한다.
        _pReloadDatabase = game::findSubsystem<AppearanceDatabase>();
        if ( _pReloadDatabase != nullptr )
            _reloadSubscription = _pReloadDatabase->registerReloaded( Delegate<void()>::create<&CharacterAppearanceComponent::onAppearanceDataReloaded>( this ) );
        if ( _presetID.empty() == false )
            requestAssemble();
    }

    void CharacterAppearanceComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObjectManager* pManager = findManager();
        if ( pManager == nullptr || _assembleCount == 0 )
            return;
        if ( _bMeshAssemblyPending == SW_TRUE )
        {
            if ( isMeshAssemblyLoading() )
                return;
            // 유닛의 메시를 바꾸는 일은 렌더 상태라 틱 뒤에 한다.
            _bMeshAssemblyPending = SW_FALSE;
            pManager->executeOrDeferPostTickOnComponent( *this, []( CharacterAppearanceComponent& appearance )
            { appearance.applyMeshAssembly(); } );
            return;
        }
        // 소켓 파일을 고치면 공유 캐시가 제자리에서 다시 읽는다 — 몸 소켓은 조립 때 덮어쓰기와 합쳐 사본으로 들었으므로 다시 조립해야 바뀐다.
        // 구운 메시도 원본 메시 · 피팅 표 · 피팅 데이터의 사본이라 같다.
        const bool bSocketsStale = _socketCache.isStale();
        if ( bSocketsStale == false && isMeshAssemblyStale() == false )
            return;
        if ( bSocketsStale )
            _socketCache.clear();
        requestAssemble();
    }

    void CharacterAppearanceComponent::onAppearanceDataReloaded()
    {
        if ( _bStarted == SW_TRUE && _presetID.empty() == false )
            requestAssemble();
    }

    void CharacterAppearanceComponent::onEndPlay()
    {
        // 데이터베이스는 게임 서비스다 — 모듈을 내릴 때 서비스가 컴포넌트보다 먼저 사라진다. 쥔 포인터로 부르지 않고 지금 서비스를 다시 찾아
        // 같은 것이 살아 있을 때만 뗀다(사라졌으면 구독도 함께 사라졌다).
        AppearanceDatabase* pLiveDatabase = game::findSubsystem<AppearanceDatabase>();
        if ( pLiveDatabase != nullptr && pLiveDatabase == _pReloadDatabase )
            pLiveDatabase->unregisterReloaded( _reloadSubscription );
        _pReloadDatabase    = nullptr;
        _reloadSubscription = DelegateHandle{};
        unregisterSocketTask();
        // 끝날 때 붙은 부품은 이미 이 오브젝트의 자식으로 함께 지울 예정이다 — 풀에 넣을 수 없다(넣으려면 부르는 쪽이 지우기 전에 `stowParts`).
        despawnParts();
        _listMeshAssemblyLoad.clear();
        _fitTablesLoad.reset();
        _bMeshAssemblyPending = SW_FALSE;
        _bStarted             = SW_FALSE;
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

    void CharacterAppearanceComponent::setEquipment( const Equipment* pEquipment )
    {
        _pEquipment = pEquipment;
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

    void CharacterAppearanceComponent::setHiddenSlots( const vector<hashed_string>& listSlot )
    {
        if ( _listHiddenSlot == listSlot )
            return;
        _listHiddenSlot             = listSlot;
        GameObjectManager* pManager = findManager();
        if ( pManager == nullptr )
            return;
        pManager->executeOrDeferPostTickOnComponent( *this,
                                                     []( CharacterAppearanceComponent& appearance, GameObjectManager& manager )
        { appearance.applyVisibility( manager ); } );
    }

    void CharacterAppearanceComponent::despawnParts()
    {
        removeParts( false );
    }

    void CharacterAppearanceComponent::destroyOwnerStowingParts()
    {
        GameObjectManager* pManager = findManager();
        if ( pManager == nullptr )
            return;
        // 소켓에서 떼기(계층 바꾸기)는 틱 밖의 일이다 — 지우기도 같은 때로 미뤄 순서를 지킨다.
        pManager->executeOrDeferPostTickOnComponent( *this, []( CharacterAppearanceComponent& appearance, GameObjectManager& manager )
        {
            appearance.removeParts( true );
            manager.destroyObject( appearance.getOwner() );
        } );
    }

    void CharacterAppearanceComponent::removeParts( bool bPool )
    {
        unbindRigUnits();
        GameObjectManager* pManager = findManager();
        if ( pManager != nullptr )
        {
            for ( const SpawnedPart& part : _listSpawnedPart )
            {
                GameObject* pObject = pManager->resolveGameObject( part._object );
                if ( pObject != nullptr )
                    retirePart( *pManager, *pObject, part, bPool );
            }
        }
        _listSpawnedPart.clear();
    }

    void CharacterAppearanceComponent::retirePart( GameObjectManager& manager, GameObject& object, const SpawnedPart& spawned, bool bPool ) const
    {
        // 소켓 부품만 풀에 넣는다 — 스킨드 부품은 이 컴포넌트가 만든 빈 오브젝트라 다시 세우는 비용이 작다. 풀이 거절하면(물리로 떨어짐) 지운다.
        using Internal       = CharacterAppearanceComponentInternal;
        const bool bPoolable = bPool && spawned._bSocketPart == SW_TRUE;
        if ( bPoolable )
        {
            // 염색은 이 외형이 만든 런타임 인스턴스다(`applyParameter`) — 버려서 프리팹 그대로 쉬게 한다. 꺼낸 쪽의 외형이 자기 값을 다시 입힌다.
            Internal::forEachMesh( object, []( MeshComponent& mesh )
            { mesh.setMaterialInstance( nullptr ); } );
            if ( manager.getSubsystem<SocketPartPool>().storePart( object, spawned._asset ) )
                return;
        }
        manager.destroyObject( &object );
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
        // 해석은 외형 상태 하나가 한다 — 장비 덮어쓰기와 떨어져 나감 판정이 같은 길을 지난다. 데이터 서비스가 바뀌었으면(다시 띄움) 처음부터 다시 잡는다.
        if ( _state.getDatabase() != pDatabase )
            _state.initialize( pDatabase, spec );
        else
            _state.setSpec( spec );
        (void)_state.update( _pEquipment ); // 바뀐 것이 없으면 지난 해석을 그대로 쓴다
        vector<AppearanceDetachEvent> listDetachEvent;
        _state.takeDetachEvents( listDetachEvent );

        _bodyPart = applyBodyPart( *pBody );
        applyBodyShapes( *pBody );
        CharacterPoseUtil::makeBindBones( pBody->getSkeleton(), _bodyBindBones );
        _bodyBones = _bodyBindBones;
        (void)CharacterPoseUtil::copyUnitPose( *pBody, _bodyBones ); // 포즈가 아직 없으면 바인드 본 그대로 소켓을 계산한다
        string socketError;
        if ( _rig.rebuild( _state.getResolved(), _bodyBindBones, _socketCache, &socketError ) == false )
            SW_LOG_ERROR( "Appearance '%#' sockets: %#", _presetID.c_str(), socketError.c_str() );

        syncParts( *pManager, *pBody, _bodyPart, listDetachEvent );
        bindSocketParts( *pManager );
        bindRigUnits( *pManager );
        applyMaterialValues( *pManager, *pBody );
        applyVisibility( *pManager );
        registerSocketTask( *pBody );
        ++_assembleCount;
        requestMeshAssembly( *pManager );
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
        for ( size_t partIndex = 0; partIndex < _state.getResolved()._listPart.size(); ++partIndex )
        {
            const ResolvedPart& part = _state.getResolved()._listPart[partIndex];
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

    void CharacterAppearanceComponent::applyBodyShapes( SkeletalMeshComponent& body )
    {
        const ResolvedAppearance& resolved = _state.getResolved();
        _bodyProportion.clear();
        _listShapeMorph.clear();
        _bodyShapeSet = nullptr;
        vector<BodyShapeValue> listValue;
        for ( const ResolvedBoneProportion& proportion : resolved._listBoneProportion )
        {
            if ( proportion._owner.empty() ) // 다른 주인(장비)의 본 비율은 그 주인의 부품이 다룬다
                listValue.push_back( BodyShapeValue{ proportion._name, proportion._value } );
        }
        const ResolvedPart* pBodyPart = _bodyPart >= 0 ? &resolved._listPart[static_cast<size_t>( _bodyPart )] : nullptr;
        if ( pBodyPart != nullptr && pBodyPart->_bodyShapes.empty() == false )
        {
            _bodyShapeSet         = SharedAssetCache<BodyShapeSet>::acquire( pBodyPart->_bodyShapes.c_str() );
            _seenShapeReloadCount = SharedAssetCache<BodyShapeSet>::getReloadCount();
            if ( _bodyShapeSet == nullptr )
                SW_LOG_ERROR( "Appearance '%#' body shapes '%#' could not be read - the body keeps its proportions", _presetID.c_str(), pBodyPart->_bodyShapes.c_str() );
        }
        else if ( listValue.empty() == false )
        {
            SW_LOG_ERROR( "Appearance '%#' drives %# body shape axes but its body part has no 'bodyShapes' - the sliders do nothing", _presetID.c_str(), static_cast<uint32>( listValue.size() ) );
        }
        if ( _bodyShapeSet != nullptr )
        {
            string error;
            if ( _bodyShapeSet->evaluate( vector_reference<const BodyShapeValue>( listValue.data(), listValue.size() ), _listShapeMorph, _bodyProportion, &error ) == false )
                SW_LOG_ERROR( "Appearance '%#' body shape axes: %#", _presetID.c_str(), error.c_str() );
        }
        // 본 비율이 있을 때만 단계 작업을 등록한다 — 포즈를 쓰는 작업이 둘이면 몸의 군중 공유가 꺼지므로, 비율이 없는 몸은 공유를 지킨다.
        const bool bWantsTask = _bodyProportion.getEntries().empty() == false;
        if ( bWantsTask && _bProportionTask == SW_FALSE )
        {
            body.registerAnimationPhaseTask( &_proportionTask );
            _bProportionTask = SW_TRUE;
        }
        else if ( bWantsTask == false && _bProportionTask == SW_TRUE )
        {
            body.unregisterAnimationPhaseTask( &_proportionTask );
            _bProportionTask = SW_FALSE;
        }
        body.markPoseDirty(); // 비율이 바뀌면 쉬는 몸도 포즈를 다시 만든다
    }

    bool CharacterAppearanceComponent::releaseDetachedPart( GameObject& object, const SpawnedPart& spawned, const vector<AppearanceDetachEvent>& listDetachEvent ) const
    {
        if ( spawned._bSocketPart == SW_FALSE )
            return false;
        for ( const AppearanceDetachEvent& detachEvent : listDetachEvent )
        {
            const ResolvedDetachedPart& detached  = detachEvent._part;
            const bool                  bSamePart = detached._owner == spawned._owner && detached._partName == spawned._partName && detached._asset == spawned._asset;
            if ( bSamePart == false )
                continue;
            SocketBindingComponent* pBinding = object.getComponent<SocketBindingComponent>();
            if ( pBinding != nullptr && pBinding->release( SocketReleaseMode::Physics, detached._impulse ) )
                return true;
            SW_LOG_WARNING( "Appearance '%#' part '%#' fell off but has no physics body to release onto - removed", _presetID.c_str(), spawned._partName.c_str() );
            return false;
        }
        return false;
    }

    void CharacterAppearanceComponent::syncParts( GameObjectManager& manager, SkeletalMeshComponent& body, int32 bodyPart,
                                                  const vector<AppearanceDetachEvent>& listDetachEvent )
    {
        using Internal = CharacterAppearanceComponentInternal;
        // 부품을 스폰하는 비용(풀 전 · 후 비교는 `-gv_socketPartPool=0`, 계획 ShooterCharacterIntegration S6)이 이 구간에 든다.
        SW_PROFILE_SCOPE( "GT.Appearance.syncParts" );
        // 남길 것을 고르고 없어진 것은 지운다. 같은 부품(주인, 이름, 에셋)은 그 오브젝트를 그대로 쓴다. 그래서 무기를 바꿔도 투구는 그대로 남는다.
        vector<SpawnedPart> listKeep;
        vector<uint8>       listPartCovered( _state.getResolved()._listPart.size(), SW_FALSE );
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            int32 matched = -1;
            for ( size_t partIndex = 0; partIndex < _state.getResolved()._listPart.size(); ++partIndex )
            {
                const bool bCandidate = listPartCovered[partIndex] == SW_FALSE && static_cast<int32>( partIndex ) != bodyPart;
                if ( bCandidate && Internal::isSamePart( _state.getResolved()._listPart[partIndex], spawned._owner, spawned._partName, spawned._asset ) )
                {
                    matched = static_cast<int32>( partIndex );
                    break;
                }
            }
            GameObject* pObject = manager.resolveGameObject( spawned._object );
            if ( matched < 0 || pObject == nullptr )
            {
                if ( pObject != nullptr && releaseDetachedPart( *pObject, spawned, listDetachEvent ) == false )
                    retirePart( manager, *pObject, spawned, true ); // 바꾼 무기는 쉬게 해 두고 다른 캐릭터가 꺼내 쓴다
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
        for ( size_t partIndex = 0; partIndex < _state.getResolved()._listPart.size(); ++partIndex )
        {
            const ResolvedPart& part      = _state.getResolved()._listPart[partIndex];
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
                pObject = manager.getSubsystem<SocketPartPool>().takePart( part._asset );
                if ( pObject == nullptr && pAssetManager != nullptr )
                    pObject = pAssetManager->getAssetCache<PrefabCache>().spawn( &manager, part._asset.c_str(), part._partName.c_str() );
                if ( pObject != nullptr && pObject->getComponent<SocketBindingComponent>() == nullptr )
                    (void)pObject->addComponent<SocketBindingComponent>();
            }
            else if ( part._kind == AppearancePartKind::Skinned )
            {
                // 스킨드 부품이다. 몸의 자식이 되고 위치는 몸과 같다. 자기 리그가 없으면 몸을 리더로 따르고(옷, 머리카락), 있으면 자기 스켈레톤을
                // 그 리그가 움직인다(스프링 망토 — 리그의 `Body` 유닛이 이 몸이다).
                pObject                         = manager.createGameObject( part._partName );
                SkeletalMeshComponent* pSkinned = pObject->addComponent<SkeletalMeshComponent>();
                pSkinned->setSkeletonPath( part._skeleton.empty() ? string_view( body.getSkeletonPath() ) : string_view( part._skeleton.c_str() ) );
                pSkinned->setMeshID( part._asset.c_str() );
                if ( part._material.empty() == false )
                    pSkinned->setMaterialPath( part._material.c_str() );
                if ( part._rig.empty() )
                {
                    // 군중 공유는 몸을 따른다 — 몸이 묶음을 나누면 부품도 같은 클립 · 위상의 자기 묶음을 나눈다(`AnimationCrowd::joinFollowerBucket`). 값의 정본은 몸 프리팹이다.
                    pSkinned->setShareCrowdPose( body.isShareCrowdPose() );
                    pSkinned->setLeaderPose( manager.resolveComponent( bodyHandle ) );
                }
                else
                {
                    PoseModifierComponent* pRig = pObject->addComponent<PoseModifierComponent>();
                    pRig->setRigPath( part._rig.c_str() );
                    pRig->bindUnit( hashed_string( kBodyRigUnit ), manager.resolveComponent( bodyHandle ) );
                    // 소켓을 적었으면 부품을 몸 바인드 포즈의 그 소켓 자리에 둔다 — 리그가 돌지 않는 곳(플레이 없는 미리보기)에서도 제자리에 보인다.
                    // 회전은 두지 않는다(리그가 뼈를 그 자리로 끈다).
                    float4x4   restInUnit;
                    uint32     restUnit    = AppearanceSocketRig::kNoUnit;
                    const bool bRestPlaced = part._placement.isEmpty() == false && _rig.computePlacement( part._placement, _bodyBindBones, restInUnit, restUnit );
                    if ( bRestPlaced && restUnit == AppearanceSocketRig::kBodyUnit )
                        pSkinned->setLocalPosition( restInUnit.getTranslation() );
                    else if ( part._placement.isEmpty() == false )
                        SW_LOG_WARNING( "Appearance '%#' rigged part '%#' names no active body socket - it rests at the body origin", _presetID.c_str(), part._partName.c_str() );
                }
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
            const ResolvedPart&     part     = _state.getResolved()._listPart[spawned._partIndex];
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

    void CharacterAppearanceComponent::bindRigUnits( GameObjectManager& manager )
    {
        PoseModifierComponent* pModifier = getOwner() != nullptr ? getOwner()->getComponent<PoseModifierComponent>() : nullptr;
        if ( pModifier == nullptr )
            return;
        vector<hashed_string> listBound;
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            const ResolvedPart& part     = _state.getResolved()._listPart[spawned._partIndex];
            const bool          bRigUnit = spawned._bSocketPart == SW_TRUE && part._owner.empty() == false && part._socketSet.empty() == false;
            if ( bRigUnit == false || AppearanceXMLUtil::containsName( listBound, part._owner ) )
                continue; // 한 칸에 소켓 에셋을 가진 부품이 둘이면 소켓 테이블처럼 첫 부품이 그 칸의 이름을 갖는다
            GameObject*                       pObject = manager.resolveGameObject( spawned._object );
            const shared_ptr<const SocketSet> sockets = _socketCache.findSharedSocketSet( part._socketSet );
            if ( pObject == nullptr || sockets == nullptr )
                continue; // 소켓 에셋을 읽지 못한 것은 소켓 테이블을 다시 만들 때 이미 알렸다
            pModifier->bindRigidUnit( part._owner, pObject, sockets );
            listBound.push_back( part._owner );
        }
        for ( const hashed_string& slot : _listRigUnit )
        {
            if ( AppearanceXMLUtil::containsName( listBound, slot ) == false )
                pModifier->bindRigidUnit( slot, nullptr, nullptr );
        }
        _listRigUnit = std::move( listBound );
    }

    void CharacterAppearanceComponent::unbindRigUnits()
    {
        PoseModifierComponent* pModifier = getOwner() != nullptr ? getOwner()->getComponent<PoseModifierComponent>() : nullptr;
        if ( pModifier != nullptr )
        {
            for ( const hashed_string& slot : _listRigUnit )
            {
                pModifier->bindRigidUnit( slot, nullptr, nullptr );
            }
        }
        _listRigUnit.clear();
    }

    void CharacterAppearanceComponent::applyMaterialValues( GameObjectManager& manager, SkeletalMeshComponent& body )
    {
        using Internal = CharacterAppearanceComponentInternal;
        for ( const ResolvedMaterialValue& value : _state.getResolved()._listMaterialValue )
        {
            if ( value._target != ResolvedMaterialTarget::Parameter )
                continue; // 염색 마스크 채널과 팔레트 슬롯은 그것을 읽는 머티리얼이 생기면 적용한다
            if ( value._owner.empty() && ( value._part.empty() || ( _bodyPart >= 0 && _state.getResolved()._listPart[static_cast<size_t>( _bodyPart )]._partName == value._part ) ) )
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
            if ( pObject == nullptr )
                continue;
            // 숨기는 칸의 부품은 몸이 보여도 숨긴다(1인칭의 머리 · 손 무기).
            const hashed_string& owner    = _state.getResolved()._listPart[spawned._partIndex]._owner;
            const bool           bVisible = _bPartsVisible && ( owner.empty() || AppearanceXMLUtil::containsName( _listHiddenSlot, owner ) == false );
            Internal::forEachMesh( *pObject, [bVisible]( MeshComponent& mesh )
            { mesh.setVisible( bVisible ); } );
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
            if ( pBinding != nullptr && _rig.computePlacement( _state.getResolved()._listPart[spawned._partIndex]._placement, _bodyBones, inUnit, unitIndex ) )
                pBinding->updateSocketTransform( inUnit );
        }
    }

    // ------------------------------------------------------------------------------
    // 구운 메시(체형 · 피팅)
    // ------------------------------------------------------------------------------
    void CharacterAppearanceComponent::requestMeshAssembly( GameObjectManager& manager )
    {
        _listMeshAssemblyLoad.clear();
        _fitTablesLoad.reset();
        _bMeshAssemblyPending                    = SW_FALSE;
        const ResolvedAppearance& resolved       = _state.getResolved();
        const ResolvedPart*       pBodyPart      = _bodyPart >= 0 ? &resolved._listPart[static_cast<size_t>( _bodyPart )] : nullptr;
        const bool                bHasTables     = pBodyPart != nullptr && pBodyPart->_fitTables.empty() == false;
        const bool                bBakes         = bHasTables || resolved._listMorph.empty() == false || _listShapeMorph.empty() == false;
        const bool                bNothingToBake = bBakes == false && _bMeshAssembled == SW_FALSE; // 굽지도 않고 되돌릴 것도 없다(대부분의 캐릭터)
        if ( pBodyPart == nullptr || bNothingToBake )
            return;

        if ( bHasTables )
            _fitTablesLoad = SharedAssetCache<FitTables>::request( pBodyPart->_fitTables.c_str() );
        // 몸이 먼저, 그 뒤 몸을 따르는 스킨드 부품. 피팅은 몸 스켈레톤의 바인드 공간에서 풀므로 다른 스켈레톤의 부품은 피팅하지 않는다(데이터 오류로 알린다).
        vector<uint32> listPartIndex{ static_cast<uint32>( _bodyPart ) };
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            if ( spawned._bSocketPart == SW_FALSE )
                listPartIndex.push_back( spawned._partIndex );
        }
        for ( const uint32 partIndex : listPartIndex )
        {
            const ResolvedPart& part = resolved._listPart[partIndex];
            MeshAssemblyLoad    load;
            load._partIndex             = partIndex;
            load._mesh                  = MeshCache::request( part._asset.c_str() );
            const bool bForeignSkeleton = part._skeleton.empty() == false && part._skeleton != pBodyPart->_skeleton;
            if ( part._fit.empty() == false && bForeignSkeleton )
                SW_LOG_ERROR( "Appearance '%#' part '%#' has fit data but its own skeleton '%#' - it is drawn unfitted", _presetID.c_str(), part._partName.c_str(), part._skeleton.c_str() );
            else if ( part._fit.empty() == false )
                load._fit = SharedAssetCache<FitPartData>::request( part._fit.c_str() );
            _listMeshAssemblyLoad.push_back( std::move( load ) );
        }
        _bMeshAssemblyPending = SW_TRUE;
        // 새로 입은 피팅 부품은 구운 메시를 걸 때까지 숨긴다 — 피팅 전의 옷은 몸을 뚫고 나온다. 이미 구운 것을 그리는 부품은 다시 조립하는 동안 그대로 보인다.
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            GameObject* pObject    = manager.resolveGameObject( spawned._object );
            const bool  bUnfitted  = spawned._bSocketPart == SW_FALSE && spawned._bBaked == SW_FALSE;
            const bool  bHidesPart = bUnfitted && resolved._listPart[spawned._partIndex]._fit.empty() == false;
            if ( pObject != nullptr && bHidesPart )
                CharacterAppearanceComponentInternal::forEachMesh( *pObject, []( MeshComponent& mesh )
                { mesh.setVisible( false ); } );
        }
        // 플레이하지 않는 씬(에디터 미리보기)은 틱이 돌지 않는다 — 이 자리에서 기다려 건다.
        if ( _bStarted == SW_FALSE )
        {
            for ( const MeshAssemblyLoad& load : _listMeshAssemblyLoad )
            {
                (void)load._mesh.wait(); // 실패는 applyMeshAssembly 가 알린다
                if ( load._fit.isValid() )
                    (void)load._fit.wait();
            }
            if ( _fitTablesLoad.isValid() )
                (void)_fitTablesLoad.wait();
            _bMeshAssemblyPending = SW_FALSE;
            applyMeshAssembly();
        }
    }

    bool CharacterAppearanceComponent::isMeshAssemblyLoading() const
    {
        if ( _fitTablesLoad.isLoading() )
            return true;
        for ( const MeshAssemblyLoad& load : _listMeshAssemblyLoad )
        {
            if ( load._mesh.isLoading() || load._fit.isLoading() )
                return true;
        }
        return false;
    }

    void CharacterAppearanceComponent::applyMeshAssembly()
    {
        // 틱이 이 걸기를 예약한 뒤 같은 틱 뒤에 다시 조립했으면(새 요청) 새 것이 아직 읽는 중이다 — 다음 틱이 건다.
        GameObjectManager* pManager        = findManager();
        const bool         bSuperseded     = _bMeshAssemblyPending == SW_TRUE || isMeshAssemblyLoading();
        const bool         bNothingToApply = _listMeshAssemblyLoad.empty() || bSuperseded;
        if ( pManager == nullptr || bNothingToApply )
            return;
        // 체형 축이 정한 몸 모프(주인이 빈 모프)를 해석의 모프에 더해 굽는다. 같은 이름이 해석에 있으면 해석의 것이 이긴다(데이터가 직접 정한 값).
        ResolvedAppearance resolved = _state.getResolved();
        for ( const BodyMorphWeight& shapeMorph : _listShapeMorph )
        {
            bool bDeclared = false;
            for ( const ResolvedMorph& morph : resolved._listMorph )
            {
                bDeclared = bDeclared || ( morph._owner.empty() && morph._name == shapeMorph._morph );
            }
            if ( bDeclared == false )
                resolved._listMorph.push_back( ResolvedMorph{ hashed_string{}, shapeMorph._morph, shapeMorph._weight } );
        }
        vector<AppearanceMeshPart> listPart;
        vector<shared_ptr<Mesh>>   listSource;
        string                     report;
        // 읽기 실패는 데이터 오류다 — 어느 것인지 알리고 원본을 그린다(구운 것과 원본이 섞인 차림을 만들지 않는다).
        if ( _fitTablesLoad.hasFailed() )
            CharacterAppearanceComponentInternal::appendReport( report, "fit tables: %#", _fitTablesLoad.getError().c_str() );
        for ( MeshAssemblyLoad& load : _listMeshAssemblyLoad )
        {
            const ResolvedPart& part = resolved._listPart[load._partIndex];
            shared_ptr<Mesh>    mesh = load._mesh.get();
            if ( load._fit.hasFailed() || mesh == nullptr )
                CharacterAppearanceComponentInternal::appendReport( report, "part '%#' mesh or fit data could not be read", part._partName.c_str() );
            load._sourceContentID = mesh != nullptr ? mesh->getContentID() : 0;
            listPart.push_back( AppearanceMeshPart{ part._partName, part._owner, mesh.get(), load._fit.get().get() } );
            listSource.push_back( std::move( mesh ) );
        }
        _seenFitReloadStamp = computeFitReloadStamp();
        // 같은 차림이면 이미 구운 것을 나눈다 — 키는 메시에 들어가는 해석(메시 해시) · 부품 · 원본 내용 · 데이터 판이다.
        uint64 bakeKey = HashUtil::combine( resolved._meshHash, _seenFitReloadStamp );
        for ( const MeshAssemblyLoad& load : _listMeshAssemblyLoad )
        {
            bakeKey = HashUtil::combine( HashUtil::combine( bakeKey, load._partIndex ), load._sourceContentID );
        }
        vector<shared_ptr<Mesh>> listBaked;
        const bool               bRead   = report.empty();
        const FitTables*         pTables = _fitTablesLoad.isValid() ? _fitTablesLoad.get().get() : nullptr;
        // 미리보기 입히기는 피팅 결과를 창에 보이므로 구운 메시 캐시를 건너뛰고 늘 푼다(같은 차림이 캐시된 메시를 나누면 푼 결과가 없다).
        const bool bPreview = _pPreviewDatabase != nullptr;
        if ( bRead && ( bPreview || AppearanceBakedMeshCache::tryGet( bakeKey, listBaked ) == false ) )
        {
            FitResult fitResult;
            if ( AppearanceMeshAssembly::assemble( resolved, listPart, pTables, _bodyBindBones, listBaked, report, bPreview ? &fitResult : nullptr ) )
            {
                if ( bPreview )
                {
                    _lastFitResult = make_shared<FitResult>( std::move( fitResult ) );
                    ++_fitResultRevision;
                }
                else
                    AppearanceBakedMeshCache::store( bakeKey, listBaked );
            }
        }
        if ( listBaked.size() != listPart.size() )
        {
            SW_LOG_ERROR( "Appearance '%#' meshes are drawn unbaked: %#", _presetID.c_str(), report.c_str() );
            listBaked.assign( listPart.size(), nullptr );
        }
        bool bAnyBaked = false;
        for ( size_t index = 0; index < _listMeshAssemblyLoad.size(); ++index )
        {
            SkeletalMeshComponent* pUnit = findSkinnedUnit( *pManager, _listMeshAssemblyLoad[index]._partIndex );
            shared_ptr<Mesh>       mesh  = listBaked[index] != nullptr ? listBaked[index] : listSource[index];
            bAnyBaked                    = bAnyBaked || listBaked[index] != nullptr;
            // 지난번에도 원본이었고 이번에도 원본이면 건드리지 않는다(사본 · 군중 묶음을 다시 만들지 않게).
            const bool bUnchanged = listBaked[index] == nullptr && _bMeshAssembled == SW_FALSE;
            const bool bApplies   = pUnit != nullptr && mesh != nullptr;
            if ( bApplies && bUnchanged == false )
                pUnit->setSkinSourceMesh( std::move( mesh ) );
            for ( SpawnedPart& spawned : _listSpawnedPart )
            {
                if ( spawned._partIndex == _listMeshAssemblyLoad[index]._partIndex && spawned._bSocketPart == SW_FALSE )
                    spawned._bBaked = listBaked[index] != nullptr ? SW_TRUE : SW_FALSE;
            }
        }
        _bMeshAssembled = bAnyBaked ? SW_TRUE : SW_FALSE;
        applyVisibility( *pManager );
    }

    bool CharacterAppearanceComponent::isMeshAssemblyStale() const
    {
        // 체형 축 데이터를 고치면 본 비율 · 모프를 다시 풀어야 한다(굽지 않는 몸도 해당).
        if ( _bodyShapeSet != nullptr && _seenShapeReloadCount != SharedAssetCache<BodyShapeSet>::getReloadCount() )
            return true;
        if ( _listMeshAssemblyLoad.empty() )
            return false;
        if ( _seenFitReloadStamp != computeFitReloadStamp() )
            return true;
        for ( const MeshAssemblyLoad& load : _listMeshAssemblyLoad )
        {
            const shared_ptr<Mesh> mesh = load._mesh.get();
            if ( mesh != nullptr && mesh->getContentID() != load._sourceContentID )
                return true;
        }
        return false;
    }

    SkeletalMeshComponent* CharacterAppearanceComponent::findSkinnedUnit( GameObjectManager& manager, uint32 partIndex ) const
    {
        if ( static_cast<int32>( partIndex ) == _bodyPart )
            return getOwner() != nullptr ? getOwner()->getComponent<SkeletalMeshComponent>() : nullptr;
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            if ( spawned._partIndex != partIndex || spawned._bSocketPart == SW_TRUE )
                continue;
            GameObject* pObject = manager.resolveGameObject( spawned._object );
            return pObject != nullptr ? pObject->getComponent<SkeletalMeshComponent>() : nullptr;
        }
        return nullptr;
    }

    uint32 CharacterAppearanceComponent::computeFitReloadStamp()
    {
        return SharedAssetCache<FitTables>::getReloadCount() + SharedAssetCache<FitPartData>::getReloadCount();
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
        {
            pUnit->unregisterAnimationPhaseTask( &_socketTask );
            if ( _bProportionTask == SW_TRUE )
                pUnit->unregisterAnimationPhaseTask( &_proportionTask );
        }
        _bProportionTask = SW_FALSE;
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
