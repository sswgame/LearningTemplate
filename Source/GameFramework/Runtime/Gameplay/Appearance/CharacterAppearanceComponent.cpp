#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Foundation/Constants.h"
#include "Core/Hash/HashUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Animation/Component/PoseModifierComponent.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/Fit/CharacterLODSettings.h"
#include "Engine/Character/Fit/CharacterMeshUtil.h"
#include "Engine/Character/Fit/FitPartDef.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Fit/FitTables.h"
#include "Engine/Character/Fit/SurfaceState.h"
#include "Engine/Character/PoseModifier/CharacterPoseUtil.h"
#include "Engine/Character/Socket/ResolvedSocketTable.h"
#include "Engine/Character/Socket/SocketBindingComponent.h"
#include "Engine/Character/Socket/SocketPartPool.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Visual/2D/SpriteComponent.h"
#include "Engine/Visual/Component/MeshComponent.h"
#include "Engine/Visual/Component/MeshSectionComponent.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceMeshPart.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceXMLUtil.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearance.h"

namespace sw
{
    /**
     * @struct CharacterAppearanceInFlightTable
     * @brief 짓는 중인 조립(짓기 키 → 짓기)입니다. 같은 프레임에 스폰한 같은 차림이 캐시를 다 놓치고(캐시는 짓기가 끝나야 찬다) 따로 짓지 않게 나눈다 —
     *        Mutable · DDC 가 진행 중인 같은 요청을 합치는 자리. 약한 참조라 걸기를 마친 짓기는 사라진다. 게임 스레드에서만 쓴다.
     * @details GameFramework 의 모듈 부분이라 엔진 범위와 함께 생기고 사라진다(구운 메시 표 `AppearanceBakedMeshTable` 과 같은 자리). 게임 서비스가 없는 실행(테스트)은 나누지 않는다.
     */
    struct CharacterAppearanceInFlightTable
    {
        static constexpr const utf8* kModulePartName = "CharacterAppearanceInFlightTable";

        unordered_map<uint64, weak_ptr<CharacterAppearanceMeshBuild>> _mapBuild;
    };

    SW_MODULE_PART( Engine, CharacterAppearanceInFlightTable, CharacterAppearanceInFlightTable );

    namespace
    {
        struct CharacterAppearanceComponentInternal
        {
            /**
             * @brief 유닛 공간(뼈가 사는 공간)의 컴포넌트입니다 — 스켈레탈 메시가 있으면 그것이고, 없으면 주 컴포넌트입니다.
             * @details 몸 메시는 캡슐(주 컴포넌트) 아래에 오프셋을 두고 붙기도 한다(모래에 빠짐 · 앉은 자리 보정). 소켓을 주 컴포넌트 공간으로 읽으면 그 오프셋을 잃어
             *          모자 같은 소켓 부품이 몸을 따라가지 않았다. 언리얼에서 부착물이 메시 컴포넌트에 붙는 것과 같은 자리다.
             */
            static const SceneComponent* findUnitScene( const GameObject& object )
            {
                const SkeletalMeshComponent* pMesh = object.getComponent<SkeletalMeshComponent>();
                return pMesh != nullptr ? static_cast<const SceneComponent*>( pMesh ) : object.getPrimarySceneComponent();
            }

            /** @brief 유닛 공간의 변환 @p inUnit 을 @p holder 의 주 컴포넌트 공간으로 옮깁니다(소켓 바인딩은 주 컴포넌트를 따른다). */
            static float4x4 toHolderSpace( const GameObject& holder, const float4x4& inUnit )
            {
                const SceneComponent* pPrimary = holder.getPrimarySceneComponent();
                const SceneComponent* pUnit    = findUnitScene( holder );
                if ( pPrimary == nullptr || pUnit == nullptr || pUnit == pPrimary )
                    return inUnit;
                return inUnit * pUnit->getWorldMatrix() * pPrimary->getWorldMatrix().invert();
            }

            static bool isSamePart( const ResolvedPart& part, const hashed_string& owner, const hashed_string& partName, const hashed_string& asset, const hashed_string& material )
            {
                return part._owner == owner && part._partName == partName && part._asset == asset && part._material == material;
            }

            /** @brief 쉬는 소켓 부품 풀의 열쇠입니다. 머티리얼을 입힌 부품은 머티리얼마다 따로 쉰다 - 프리팹 그대로인 부품을 꺼낸 쪽이 남의 머티리얼을 받지 않게 한다. */
            static hashed_string makePoolKey( const hashed_string& asset, const hashed_string& material )
            {
                if ( material.empty() )
                    return asset;
                string key{ asset.view() };
                key += '#';
                key += material.view();
                return hashed_string( string_view( key ) );
            }

            /** @brief 합친 메시에서 부품이 든 구간의 이름입니다 — 주인(슬롯)과 부품 이름을 이어 차림 안에서 겹치지 않게 한다(몸은 주인이 비어 부품 이름 그대로). */
            /**
             * @brief 부품의 `<Section>` 을 그립니다 — 주 메시 @p main 이 첫 구간을, 오브젝트 @p owner 의 구간 컴포넌트가 나머지를 자기 머티리얼로 그린다.
             * @details @p inoutListSection 은 이 부품이 전에 만든 구간 컴포넌트다 — 다 지우고 새로 만든다(구간 수 · 이름이 외형마다 다르다). 구간을 적지 않은 부품은 지우기만 한다.
             */
            static void applyPartSections( GameObjectManager& manager, GameObject& owner, MeshComponent& main, const ResolvedPart& part,
                                           vector<TComponentHandle<MeshSectionComponent>>& inoutListSection, const string& presetID )
            {
                // 이미 같은 구간 · 머티리얼이면 그대로 둔다 — 조립은 자주(로드를 기다리는 동안 프레임마다) 다시 불리고, 새로 만들면 머티리얼을 매번 다시 잡는다.
                bool bSame = part._listSection.empty() == false && inoutListSection.size() + 1 == part._listSection.size() && main.getSectionName() == part._listSection.front()._name &&
                             main.getMaterialPath() == part._listSection.front()._material;
                for ( size_t index = 0; bSame && index < inoutListSection.size(); ++index )
                {
                    const MeshSectionComponent* pSection = manager.resolveComponent( inoutListSection[index] );
                    bSame                                = pSection != nullptr && pSection->getSectionName() == part._listSection[index + 1]._name && pSection->getMaterialPath() == part._listSection[index + 1]._material;
                }
                if ( bSame )
                    return;
                for ( const TComponentHandle<MeshSectionComponent>& handle : inoutListSection )
                {
                    MeshSectionComponent* pSection = manager.resolveComponent( handle );
                    if ( pSection != nullptr && owner.removeComponent( pSection ) == false )
                        SW_LOG_WARNING_ID( "CharacterAppearance.AppearanceCouldNotRemove", "Appearance '%#' could not remove a section component of part '%#'", presetID.c_str(), part._partName.c_str() );
                }
                inoutListSection.clear();
                if ( part._listSection.empty() )
                    return;
                main.setSectionName( part._listSection.front()._name );
                main.setMaterialPath( part._listSection.front()._material.c_str() );
                for ( size_t index = 1; index < part._listSection.size(); ++index )
                {
                    MeshSectionComponent* pSection = owner.addComponent<MeshSectionComponent>();
                    if ( pSection == nullptr )
                    {
                        SW_LOG_ERROR_ID( "CharacterAppearance.AppearancePartCouldNot2", "Appearance '%#' part '%#' could not get a section component", presetID.c_str(), part._partName.c_str() );
                        continue;
                    }
                    pSection->setSectionName( part._listSection[index]._name );
                    pSection->setMaterialPath( part._listSection[index]._material.c_str() );
                    pSection->followSourceMesh();
                    inoutListSection.push_back( TComponentHandle<MeshSectionComponent>{ pSection } );
                }
            }

            static hashed_string makeSectionName( const ResolvedPart& part )
            {
                if ( part._owner.empty() )
                    return part._partName;
                return hashed_string( ( string( part._owner.c_str() ) + "." + part._partName.c_str() ).c_str() );
            }

            /**
             * @brief 부품을 병합에 넣는 모양으로 만듭니다 — `<Section>` 이 없으면 부품의 구간 이름(`makeSectionName`) 조각 하나, 있으면 적은 구간마다 조각 하나다(적지 않은 구간은 빠진다).
             * @details 몸(@p bBody)의 조각은 원본 구간 이름 그대로다 — 몸의 주 메시와 `_listBodySection` 이 이미 그 이름으로 그려, 병합을 걸고 풀 때 이름을 다시 매기지 않는다.
             *          다른 부품의 조각은 부품의 구간 이름 뒤에 `/구간` 을 붙여 차림 안에서 겹치지 않게 한다.
             */
            static MeshMergePart makeMergePart( const ResolvedPart& part, const Mesh& mesh, bool bBody )
            {
                MeshMergePart merge{ makeSectionName( part ), part._material, &mesh };
                for ( const AppearanceSectionDef& section : part._listSection ) // 변형 · 규칙을 반영한 구간 머티리얼 — 병합해도 구간마다 머티리얼이 남는다
                {
                    const hashed_string name = bBody ? section._name : hashed_string( ( string( merge._name.c_str() ) + "/" + section._name.c_str() ).c_str() );
                    merge._listSection.push_back( MeshMergeSectionPick{ section._name, name, section._material } );
                }
                return merge;
            }

            /**
             * @brief 병합 조각(@p merge — `makeMergePart` 의 결과)마다 합친 메시 LOD 규칙을 덧붙입니다. 조각 이름이 합친 메시의 구간 이름이다(훅 없는 병합).
             *        `<Section>` 이 적은 값이 먼저이고, 적지 않으면 부품의 값입니다.
             */
            static void appendLODRules( const ResolvedPart& part, const MeshMergePart& merge, vector<CharacterLODSectionRule>& inoutListRule )
            {
                if ( merge._listSection.empty() )
                {
                    inoutListRule.push_back( CharacterLODSectionRule{ merge._name, part._lodDetail, part._lodDropLevel } );
                    return;
                }
                for ( size_t index = 0; index < merge._listSection.size() && index < part._listSection.size(); ++index )
                {
                    const AppearanceSectionDef& section = part._listSection[index]; // `makeMergePart` 가 같은 순서로 조각을 만들었다
                    const float32               detail  = section._lodDetail != 1.0f ? section._lodDetail : part._lodDetail;
                    const uint32                drop    = section._lodDropLevel != 0 ? section._lodDropLevel : part._lodDropLevel;
                    inoutListRule.push_back( CharacterLODSectionRule{ merge._listSection[index]._name, detail, drop } );
                }
            }

            /** @brief 합친 부품의 조각마다 몸 오브젝트 @p owner 의 구간 컴포넌트를 맞춥니다 — 모자라면 더하고 남으면 지우고, 이름 · 머티리얼을 조각의 것으로 한다. */
            static void applyMergedSections( GameObjectManager& manager, GameObject& owner, const MeshMergePart& merge, const ResolvedPart& part,
                                             vector<TComponentHandle<MeshSectionComponent>>& inoutListSection, const string& presetID )
            {
                vector<MeshMergeSectionPick> listPiece = merge._listSection;
                if ( listPiece.empty() )
                    listPiece.push_back( MeshMergeSectionPick{ hashed_string{}, merge._name, merge._material } );
                while ( inoutListSection.size() > listPiece.size() )
                {
                    MeshSectionComponent* pExtra = manager.resolveComponent( inoutListSection.back() );
                    if ( pExtra != nullptr && owner.removeComponent( pExtra ) == false )
                        SW_LOG_WARNING_ID( "CharacterAppearance.AppearanceCouldNotRemove", "Appearance '%#' could not remove a section component of part '%#'", presetID.c_str(), part._partName.c_str() );
                    inoutListSection.pop_back();
                }
                inoutListSection.resize( listPiece.size() );
                for ( size_t index = 0; index < listPiece.size(); ++index )
                {
                    MeshSectionComponent* pSection = manager.resolveComponent( inoutListSection[index] );
                    if ( pSection == nullptr )
                    {
                        pSection = owner.addComponent<MeshSectionComponent>();
                        if ( pSection == nullptr )
                        {
                            SW_LOG_ERROR_ID( "CharacterAppearance.AppearancePartCouldNot2", "Appearance '%#' part '%#' could not get a section component", presetID.c_str(), part._partName.c_str() );
                            continue;
                        }
                        inoutListSection[index] = TComponentHandle<MeshSectionComponent>{ pSection };
                    }
                    pSection->setSectionName( listPiece[index]._name );
                    if ( listPiece[index]._material.empty() == false )
                        pSection->setMaterialPath( listPiece[index]._material.c_str() );
                    pSection->followSourceMesh();
                }
            }

            /** @brief 병합한 메시를 같은 차림끼리 나누는 캐시 키에 섞는 값입니다(구운 메시와 키가 겹치지 않게). */
            static constexpr uint64 kMergedMeshKeySalt = 0x4D45524745ull; // "MERGE"

            /** @brief 보고에 한 줄을 덧붙입니다. */
            template <typename... Args>
            static void appendReport( string& inoutReport, string_view format, Args&&... args )
            {
                utf8 arrMessage[constant::buffer::kMax512]{};
                formatstring( arrMessage, constant::buffer::kMax512, format, std::forward<Args>( args )... );
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

            /** @brief 메시에 머티리얼 값을 적용합니다. 메시 전용 인스턴스에 넣고, 없으면 메시의 머티리얼에서 새로 만듭니다. */
            static void applyParameter( MeshComponent& mesh, const ResolvedMaterialValue& value )
            {
                MaterialInstance* pInstance = CharacterSurfaceBinding::acquireInstance( mesh ); // 표면 상태와 같은 인스턴스를 나눈다
                if ( pInstance == nullptr )
                    return;
                if ( value._shape == ResolvedMaterialShape::Scalar )
                    pInstance->setScalarParameter( value._name, value._value._x ); // 스칼라 매개변수에 float4 글을 쓰지 않는다
                else
                    pInstance->setVectorParameter( value._name, value._value );
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

    void CharacterAppearanceSocketTask::onAnimationMeshDetached( SkeletalMeshComponent& unit )
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

    AnimationPoseAccess CharacterAppearanceProportionTask::getPoseAccess() const
    {
        return _owner.getBodyProportion().getEntries().empty() ? AnimationPoseAccess::ReadsOnly : AnimationPoseAccess::Writes;
    }

    void CharacterAppearanceProportionTask::runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context )
    {
        (void)context;
        if ( phase != AnimationPhase::Attachment || getPoseAccess() == AnimationPoseAccess::ReadsOnly )
            return;
        _owner.getBodyProportion().applyToPose( unit.getSkeleton(), unit.getLocalPose() );
    }

    void CharacterAppearanceProportionTask::onAnimationMeshDetached( SkeletalMeshComponent& unit )
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
        , _surfaceChannelsPath{}
        , _bMergeParts{ SW_FALSE }
        , _reservedProperty{ 0 }
        , _socketTask{ *this }
        , _proportionTask{ *this }
        , _bodyProportion{}
        , _listShapeMorph{}
        , _bodyShapeSet{}
        , _footLift{ 0.0f }
        , _shapeReload{}
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
        , _reloadSubscription{}
        , _listMeshAssemblyLoad{}
        , _fitTablesLoad{}
        , _meshBuild{}
        , _lastFitResult{}
        , _fitResultRevision{ 0 }
        , _assembleCount{ 0 }
        , _seenFitReloadStamp{ 0 }
        , _bodyPart{ constant::invalid_index::kInt32 }
        , _bPartsVisible{ true }
        , _bAssembleScheduled{ SW_FALSE }
        , _bStarted{ SW_FALSE }
        , _bMeshAssemblyPending{ SW_FALSE }
        , _bMeshAssembled{ SW_FALSE }
        , _bProportionTask{ SW_FALSE }
        , _bMeshMerged{ SW_FALSE }
        , _bMeshApplyQueued{ SW_FALSE }
        , _reserved{ 0 }
        , _surface{}
        , _surfaceBinding{}
        , _loadedSurfaceChannels{}
        , _surfaceAppliedRevision{ constant::invalid_index::kUint64 }
        , _surfaceRegionCount{ 0 }
    {
    }

    CharacterAppearanceComponent::~CharacterAppearanceComponent() = default;

    void CharacterAppearanceComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _bStarted = SW_TRUE;
        // 핫 리로드: 외형 데이터를 다시 읽으면(`AppearanceDatabase::reloadFromFolder`) 다시 조립한다 — 상태가 데이터 버전으로 다시 해석한다.
        AppearanceDatabase* pDatabase = game::findSubsystem<AppearanceDatabase>();
        if ( pDatabase != nullptr )
            _reloadSubscription = pDatabase->registerReloaded( Delegate<void()>::create<&CharacterAppearanceComponent::onAppearanceDataReloaded>( this ) );
        // 미리보기 입히기(`previewSelection`)도 시작한 뒤 같은 틱 길로 조립한다 — 미리보기 씬도 플레이하고 틱한다(언리얼 FPreviewScene).
        if ( _presetID.empty() == false || _pPreviewDatabase != nullptr )
            requestAssemble();
    }

    void CharacterAppearanceComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObjectManager* pManager = findManager();
        if ( pManager == nullptr || _assembleCount == 0 )
            return;
        if ( hasSurfaceState() )
        {
            _surface.update( deltaTime ); // 감쇠한다. 바뀐 프레임에만 머티리얼로 올라간다
            applySurface( *pManager, false );
        }
        if ( _bMeshAssemblyPending == SW_TRUE || _bMeshApplyQueued == SW_TRUE )
            return; // 베이크한 메시를 기다리거나(`waitMeshAssembly`) 걸기가 틱 뒤로 예약됐다 — 걸기 전의 원본 내용 id 는 0 이라 낡음으로 보이면 조립을 끝없이 다시 한다
        // 소켓 파일을 고치면 공유 캐시가 제자리에서 다시 읽는다 — 몸 소켓은 조립 때 덮어쓰기와 합쳐 사본으로 들었으므로 다시 조립해야 바뀐다.
        // 베이크한 메시도 원본 메시 · 피팅 표 · 피팅 데이터의 사본이라 같다.
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
        // 데이터베이스는 게임 서비스다 — 모듈을 내릴 때 서비스가 컴포넌트보다 먼저 사라질 수 있다. 그때는 구독이 이미 무효라 푸는 일이 없다.
        _reloadSubscription.reset();
        unregisterSocketTask();
        _surfaceBinding.releaseTextures(); // 마스크 텍스처를 놓는다(상태 값은 다음 시작에 다시 만든다)
        _surface = CharacterSurfaceState{};
        _loadedSurfaceChannels.clear();
        _surfaceRegionCount     = 0;
        _surfaceAppliedRevision = constant::invalid_index::kUint64;
        // 끝날 때 붙은 부품은 이미 이 오브젝트의 자식으로 함께 지울 예정이다 — 풀에 넣을 수 없다(넣으려면 부르는 쪽이 지우기 전에 `stowParts`).
        despawnParts();
        GameObjectManager* pManager = findManager();
        if ( pManager != nullptr )
            pManager->getPrimitiveRegistry().removeAssetWaiter( this );
        _listMeshAssemblyLoad.clear();
        _fitTablesLoad.reset();
        _meshBuild.reset(); // 도는 태스크는 자기 참조로 끝까지 돌고 결과를 버린다
        _bMeshAssemblyPending = SW_FALSE;
        _bStarted             = SW_FALSE;
        Component::onEndPlay();
    }

    void CharacterAppearanceComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getPrimitiveRegistry().removeAssetWaiter( this );
        _bMeshAssemblyPending = SW_FALSE;
        Component::onUnregister( manager );
    }

    void CharacterAppearanceComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        const bool bAssembleProperty = propertyName == hashed_string( "_presetID" ) || propertyName == hashed_string( "_seed" ) || propertyName == hashed_string( "_bMergeParts" );
        if ( _bStarted == SW_TRUE && bAssembleProperty )
            requestAssemble();
        GameObjectManager* pManager      = findManager();
        const bool         bSurfaceReady = _bStarted == SW_TRUE && pManager != nullptr;
        if ( bSurfaceReady && propertyName == hashed_string( "_surfaceChannelsPath" ) )
        {
            rebuildSurfaceState( _surfaceRegionCount );
            applySurface( *pManager, true );
        }
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
        _previewSelection                 = selection;
        _pPreviewDatabase                 = &database;
        const GameObjectManager* pManager = findManager();
        // 베이크한 메시는 틱이 받아 건다 — 플레이하지 않는 씬에 입히면 영영 걸리지 않으므로 오류로 알린다(그 자리에서 기다리지 않는다).
        if ( pManager == nullptr || pManager->hasBegunPlay() == false )
        {
            SW_LOG_ERROR_ID( "CharacterAppearance.AppearancePreviewNeedsPlaying", "Appearance preview needs a playing scene (ScenePreviewPolicy::_bPlay) - '%#' is not dressed", getOwner() != nullptr ? getOwner()->getName().c_str() : "?" );
            return;
        }
        if ( _bStarted == SW_TRUE )
            requestAssemble(); // 아직 시작 전이면(방금 붙인 컴포넌트 — 다음 틱에 시작) `onBeginPlay` 가 조립한다
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
            manager.destroyGameObject( appearance.getOwner() );
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
                if ( part._bMerged == SW_TRUE )
                    retireMergedPart( *pManager, part );
                else if ( pObject != nullptr )
                    retirePart( *pManager, *pObject, part, bPool );
            }
        }
        _listSpawnedPart.clear();
    }

    void CharacterAppearanceComponent::retirePart( GameObjectManager& manager, GameObject& object, const SpawnedPart& spawned, bool bPool ) const
    {
        // 프리팹에서 스폰한 소켓 부품만 풀에 넣는다 — 스킨드 · 단단한 조각 · 스프라이트는 이 컴포넌트가 만든 작은 오브젝트라 다시 세우는 비용이 작다
        // (풀 열쇠가 에셋 · 머티리얼뿐이라 구간 머티리얼을 담지 못한다). 풀이 거절하면(물리로 떨어짐) 지운다.
        using Internal       = CharacterAppearanceComponentInternal;
        const bool bPoolable = bPool && spawned._bPooled == SW_TRUE;
        if ( bPoolable )
        {
            // 염색은 이 외형이 만든 런타임 인스턴스다(`applyParameter`) — 버려서 프리팹 그대로 쉬게 한다. 꺼낸 쪽의 외형이 자기 값을 다시 입힌다.
            Internal::forEachMesh( object, []( MeshComponent& mesh )
            { mesh.setMaterialInstance( nullptr ); } );
            if ( manager.getSubsystem<SocketPartPool>().storePart( object, Internal::makePoolKey( spawned._asset, spawned._material ) ) )
                return;
        }
        manager.destroyGameObject( &object );
    }

    void CharacterAppearanceComponent::retireMergedPart( GameObjectManager& manager, const SpawnedPart& spawned ) const
    {
        GameObject* pOwner = getOwner();
        for ( const TComponentHandle<MeshSectionComponent>& handle : spawned._listSection )
        {
            MeshSectionComponent* pSection   = manager.resolveComponent( handle );
            const bool            bRemovable = pSection != nullptr && pOwner != nullptr;
            if ( bRemovable && pOwner->removeComponent( pSection ) == false )
                SW_LOG_WARNING_ID( "CharacterAppearance.AppearanceCouldNotRemove", "Appearance '%#' could not remove the section component of a merged part", _presetID.c_str() );
        }
    }

    template <typename Func>
    void CharacterAppearanceComponent::forEachPartMesh( GameObjectManager& manager, const SpawnedPart& spawned, Func&& func ) const
    {
        if ( spawned._bMerged == SW_TRUE )
        {
            // 합친 조각 i 는 부품의 i 번째 `<Section>` 이다(`makeMergePart` 순서). 합친 구간 이름은 조립이 만든 것이라 원본 구간 이름을 따로 넘긴다.
            const ResolvedPart& part = _state.getResolved()._listPart[spawned._partIndex];
            for ( size_t index = 0; index < spawned._listSection.size(); ++index )
            {
                MeshSectionComponent* pSection = manager.resolveComponent( spawned._listSection[index] );
                const hashed_string   source   = index < part._listSection.size() ? part._listSection[index]._name : hashed_string{};
                if ( pSection != nullptr )
                    func( *pSection, source ); // 아직 합친 메시를 걸기 전이면 구간 컴포넌트가 없다
            }
            return;
        }
        GameObject* pObject = manager.resolveGameObject( spawned._object );
        if ( pObject != nullptr )
            CharacterAppearanceComponentInternal::forEachMesh( *pObject, [&func]( MeshComponent& mesh )
            { func( mesh, mesh.getSectionName() ); } ); // 합치지 않은 부품은 주 메시 · 구간 컴포넌트가 원본 구간 이름을 그대로 가진다(`applyPartSections`)
    }

    bool CharacterAppearanceComponent::isMergedPart( const ResolvedPart& part, int32 partIndex ) const
    {
        const bool bBodyPartKnown = _bodyPart >= 0 && static_cast<size_t>( _bodyPart ) < _state.getResolved()._listPart.size();
        const bool bOwnsRig       = part._rig.empty() == false;
        const bool bFollowsBody   = part.isSkinnedFollower() && bOwnsRig == false && partIndex != _bodyPart;
        const bool bMergeable     = _bMergeParts == SW_TRUE && bBodyPartKnown;
        if ( bMergeable == false || bFollowsBody == false )
            return false;
        // 몸과 같은 스켈레톤(적지 않으면 몸의 것)만 한 유닛이 스키닝한다.
        const ResolvedPart& body = _state.getResolved()._listPart[static_cast<size_t>( _bodyPart )];
        return part._skeleton.empty() || part._skeleton == body._skeleton;
    }

    bool CharacterAppearanceComponent::hasMergedParts() const
    {
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            if ( spawned._bMerged == SW_TRUE )
                return true;
        }
        return false;
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
            const SceneComponent* pScene  = pObject != nullptr ? CharacterAppearanceComponentInternal::findUnitScene( *pObject ) : nullptr;
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
            if ( part._bMerged == SW_FALSE ) // 합친 부품은 오브젝트가 없다(몸 오브젝트의 구간으로 그린다)
                outListObject.push_back( part._object );
        }
    }

    GameObjectHandle CharacterAppearanceComponent::findSlotPartObject( const hashed_string& slot ) const
    {
        for ( const SpawnedPart& part : _listSpawnedPart )
        {
            if ( part._owner == slot && part._bSocketPart == SW_TRUE && part._bMerged == SW_FALSE )
                return part._object;
        }
        return GameObjectHandle{};
    }

    // ------------------------------------------------------------------------------
    // 조립(게임 스레드, 틱 밖)
    // ------------------------------------------------------------------------------
    void CharacterAppearanceComponent::requestAssemble()
    {
        GameObjectManager* pManager = findManager();
        if ( pManager == nullptr || _bAssembleScheduled == SW_TRUE )
            return;
        // 조립은 사건(시작 · 선택 · 장비 · 데이터 다시 읽기)마다 한 번이고 깃발로 합친다 — 매 프레임 일(표면 감쇠 · 올리기)은 자기 오브젝트 쓰기라 틱에 남는다.
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
            SW_LOG_WARNING_ID( "CharacterAppearance.NoSkeletalMeshWear", "'%#' has no skeletal mesh to wear appearance '%#'", pOwner != nullptr ? pOwner->getName().c_str() : "?", _presetID.c_str() );
            return;
        }
        if ( pDatabase == nullptr )
        {
            SW_LOG_ERROR_ID( "CharacterAppearance.AppearanceCannotAssembledNo", "Appearance '%#' cannot be assembled - no AppearanceDatabase game service", _presetID.c_str() );
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
        if ( _state.usesDatabase( *pDatabase ) == false )
            _state.initialize( *pDatabase, spec );
        else
            _state.setSpec( spec );
        (void)_state.update( *pDatabase, _pEquipment ); // 바뀐 것이 없으면 지난 해석을 그대로 쓴다
        vector<AppearanceDetachEvent> listDetachEvent;
        _state.takeDetachEvents( listDetachEvent );

        _bodyPart = applyBodyPart( *pBody );
        applyBodyShapes( *pBody );
        CharacterPoseUtil::makeBindBones( pBody->getSkeleton(), _bodyBindBones );
        _bodyBones = _bodyBindBones;
        (void)CharacterPoseUtil::copyUnitPose( *pBody, _bodyBones ); // 포즈가 아직 없으면 바인드 본 그대로 소켓을 계산한다
        string socketError;
        if ( _rig.rebuild( _state.getResolved(), _bodyBindBones, _socketCache, &socketError ) == false )
            SW_LOG_ERROR_ID( "CharacterAppearance.AppearanceSockets", "Appearance '%#' sockets: %#", _presetID.c_str(), socketError.c_str() );
        // 장비 소켓은 차림을 풀어야 안다(임포트 때 알 수 없다) — 소켓 표를 다시 지을 때마다 몸 소켓의 부모 본을 몸 유닛 본 LOD 의 핵심 본으로 다시 낸다.
        const ResolvedSocketTable& socketTable = _rig.getTable();
        vector<hashed_string>      listSocketBone;
        for ( SocketID socketID = 0; socketID < socketTable.getSocketCount(); ++socketID )
        {
            const SocketDef* pDef        = socketTable.isSocketActive( socketID ) ? socketTable.findSocketDef( socketID ) : nullptr;
            const bool       bBodySocket = pDef != nullptr && socketTable.getSocketUnit( socketID ) == AppearanceSocketRig::kBodyUnit && pDef->_parent.empty() == false;
            if ( bBodySocket )
                listSocketBone.push_back( pDef->_parent );
        }
        _socketBoneRequirement = pBody->requireBones( std::move( listSocketBone ) );

        syncParts( *pManager, *pBody, _bodyPart, listDetachEvent );
        bindSocketParts( *pManager );
        bindRigUnits( *pManager );
        applyMaterialValues( *pManager, *pBody );
        applyVisibility( *pManager );
        rebuildSurfaceState( _surfaceRegionCount ); // 영역 수는 피팅 표를 읽은 뒤 `applyMeshAssembly` 가 맞춘다
        applySurface( *pManager, true );
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
        if ( database.getPresets().expand( presetID, seed, database.getSlotCatalog(), database.getSets(), database.getSchemas(), base ) == false )
        {
            SW_LOG_ERROR_ID( "CharacterAppearance.AppearancePresetCouldNot", "Appearance preset '%#' could not be expanded", presetID.c_str() );
            return false;
        }
        if ( bPreview == false )
        {
            outSpec = std::move( base );
            return true;
        }
        AppearanceSelectionReport report;
        AppearanceSelection::applySelection( database, _previewSelection, base, nullptr, outSpec, report );
        if ( report.isClean() == false )
            SW_LOG_WARNING_ID( "CharacterAppearance.PreviewedAppearanceBaseDropped", "Previewed appearance on base '%#' dropped %# values, clamped %# and reset %# slots to their defaults", presetID.c_str(),
                               static_cast<uint32>( report._listDroppedParameter.size() ), static_cast<uint32>( report._listClampedParameter.size() ), static_cast<uint32>( report._listSlotFallback.size() ) );
        return true;
    }

    int32 CharacterAppearanceComponent::applyBodyPart( SkeletalMeshComponent& body )
    {
        for ( size_t partIndex = 0; partIndex < _state.getResolved()._listPart.size(); ++partIndex )
        {
            const ResolvedPart& part = _state.getResolved()._listPart[partIndex];
            if ( part._owner.empty() == false || part.isSkinnedFollower() == false )
                continue; // 몸은 휘는 스킨드다(`AppearanceSocketRig` 의 몸 부품과 같은 판정)
            // 스켈레톤을 먼저 적용한다. 메시의 스킨 사본이 그 본 수를 참조하기 때문이다.
            // 적지 않은 스켈레톤은 빈 경로(암묵 스켈레톤)다 — 빈 이름의 `c_str()` 은 "None" 이라 그대로 넘기면 경로 "None" 이 된다(C-59).
            const string_view skeletonPath = part._skeleton.empty() ? string_view{} : string_view( part._skeleton.c_str() );
            if ( string_view( body.getSkeletonPath() ) != skeletonPath )
                body.setSkeletonPath( skeletonPath );
            if ( string_view( body.getMeshID() ) != string_view( part._asset.c_str() ) )
                body.setMeshID( part._asset.c_str() );
            if ( part._material.empty() == false && body.getMaterialPath() != part._material )
                body.setMaterialPath( part._material.c_str() );
            GameObjectManager* pManager = findManager();
            if ( pManager != nullptr && body.getOwner() != nullptr )
                CharacterAppearanceComponentInternal::applyPartSections( *pManager, *body.getOwner(), body, part, _listBodySection, _presetID );
            return static_cast<int32>( partIndex );
        }
        SW_LOG_WARNING_ID( "CharacterAppearance.AppearanceNoBodyPart", "Appearance '%#' has no body part (a Skinned part without an owner)", _presetID.c_str() );
        return -1;
    }

    const AppearanceDatabase* CharacterAppearanceComponent::findDatabase() const
    {
        return _pPreviewDatabase != nullptr ? _pPreviewDatabase : game::findSubsystem<AppearanceDatabase>();
    }

    void CharacterAppearanceComponent::applyBodyShapes( SkeletalMeshComponent& body )
    {
        const ResolvedAppearance& resolved = _state.getResolved();
        _bodyProportion.clear();
        _listShapeMorph.clear();
        _bodyShapeSet                  = nullptr;
        const ResolvedPart* pBodyPart  = _bodyPart >= 0 ? &resolved._listPart[static_cast<size_t>( _bodyPart )] : nullptr;
        const bool          bHasShapes = pBodyPart != nullptr && pBodyPart->_bodyShapes.empty() == false;
        if ( bHasShapes )
        {
            _bodyShapeSet = SharedAssetCache<BodyShapeSet>::acquire( pBodyPart->_bodyShapes.c_str() );
            _shapeReload.markSeen();
            if ( _bodyShapeSet == nullptr )
                SW_LOG_ERROR_ID( "CharacterAppearance.AppearanceBodyShapesCould", "Appearance '%#' body shapes '%#' could not be read - the body keeps its proportions", _presetID.c_str(), pBodyPart->_bodyShapes.c_str() );
        }
        vector<BodyShapeValue> listValue;
        for ( const ResolvedBoneProportion& proportion : resolved._listBoneProportion )
        {
            if ( proportion._owner.empty() == false )
                continue; // 다른 주인(장비)의 본 비율은 그 주인의 부품이 다룬다
            // 부품 머티리얼이 받은 채널(`<ChannelMaterial>` — 머리카락 길이)은 이 스켈레톤이 축으로 적지 않았으면 몸 축이 아니다 — 체형 축 데이터 없이도 닿는다.
            const bool bMaterialOnly = VectorUtil::contains( resolved._listMaterialChannel, proportion._name ) && ( _bodyShapeSet == nullptr || _bodyShapeSet->findAxis( proportion._name ) == nullptr );
            if ( bMaterialOnly == false )
                listValue.push_back( BodyShapeValue{ proportion._name, proportion._value } );
        }
        if ( bHasShapes == false && listValue.empty() == false )
        {
            SW_LOG_ERROR_ID( "CharacterAppearance.AppearanceDrivesBodyShape", "Appearance '%#' drives %# body shape axes but its body part has no 'bodyShapes' - the sliders do nothing", _presetID.c_str(), static_cast<uint32>( listValue.size() ) );
        }
        _footLift = 0.0f;
        if ( _bodyShapeSet != nullptr )
        {
            string error;
            if ( _bodyShapeSet->evaluate( vector_reference<const BodyShapeValue>( listValue.data(), listValue.size() ), _listShapeMorph, _bodyProportion, &error ) == false )
                SW_LOG_ERROR_ID( "CharacterAppearance.AppearanceBodyShapeAxes", "Appearance '%#' body shape axes: %#", _presetID.c_str(), error.c_str() );
            _footLift = _bodyShapeSet->computeFootLift( vector_reference<const BodyShapeValue>( listValue.data(), listValue.size() ) );
        }
        // 암묵 스켈레톤은 몸을 입기 전이다 — 몸 스켈레톤이 정해진 뒤에만 견준다.
        if ( body.hasDataSkeleton() )
            (void)_bodyProportion.checkBones( body.getSkeleton(), _presetID ); // 없는 본은 오류로 남았다. 있는 본의 비율은 그대로 건다
        // 본 비율이 있을 때만 단계 작업을 등록한다 — 포즈를 쓰는 작업이 둘이면 몸의 군중 공유가 꺼지므로, 비율이 없는 몸은 공유를 지킨다.
        const bool bWantsTask = _bodyProportion.getEntries().empty() == false;
        if ( bWantsTask && _bProportionTask == SW_FALSE )
        {
            _proportionTaskRegistration = body.registerAnimationPhaseTask( &_proportionTask );
            _bProportionTask            = SW_TRUE;
        }
        else if ( bWantsTask == false && _bProportionTask == SW_TRUE )
        {
            _proportionTaskRegistration.reset();
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
            SW_LOG_WARNING_ID( "CharacterAppearance.AppearancePartFellOff", "Appearance '%#' part '%#' fell off but has no physics body to release onto - removed", _presetID.c_str(), spawned._partName.c_str() );
            return false;
        }
        return false;
    }

    GameObject* CharacterAppearanceComponent::spawnSkinnedPart( GameObjectManager& manager, SkeletalMeshComponent& body, SceneComponent* pBodyScene, const ResolvedPart& part,
                                                                vector<TComponentHandle<MeshSectionComponent>>& outListSection )
    {
        // 스킨드 부품이다. 몸의 자식이 되고 위치는 몸과 같다. 자기 리그가 없으면 몸을 리더로 따르고(옷, 머리카락), 있으면 자기 스켈레톤을
        // 그 리그가 움직인다(스프링 망토 — 리그의 `Body` 유닛이 이 몸이다).
        const TComponentHandle<SkeletalMeshComponent> bodyHandle{ &body };
        GameObject*                                   pObject  = manager.createGameObject( part._partName );
        SkeletalMeshComponent*                        pSkinned = pObject->addComponent<SkeletalMeshComponent>();
        pSkinned->setSkeletonPath( part._skeleton.empty() ? string_view( body.getSkeletonPath() ) : string_view( part._skeleton.c_str() ) );
        pSkinned->setMeshID( part._asset.c_str() );
        if ( part._material.empty() == false )
            pSkinned->setMaterialPath( part._material.c_str() );
        // 새 오브젝트라 지울 것이 없다. 구간 컴포넌트는 부품 오브젝트와 함께 사라지고, 다시 조립할 때 구간 머티리얼만 바뀌면 이 목록으로 고친다(`syncParts`).
        outListSection.clear();
        CharacterAppearanceComponentInternal::applyPartSections( manager, *pObject, *pSkinned, part, outListSection, _presetID );
        if ( part._rig.empty() )
        {
            // 군중 공유는 몸을 따른다 — 몸이 묶음을 나누면 부품도 같은 클립 · 위상의 자기 묶음을 나눈다(`AnimationCrowd::joinFollowerBucket`). 값의 기준은 몸 프리팹이다.
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
                SW_LOG_WARNING_ID( "CharacterAppearance.AppearanceRiggedPartNames", "Appearance '%#' rigged part '%#' names no active body socket - it rests at the body origin", _presetID.c_str(), part._partName.c_str() );
        }
        if ( pBodyScene != nullptr )
            (void)pSkinned->attachToComponent( pBodyScene, AttachRule::KeepRelative ); // 붙일 수 없는 부모이면 false 이고, 그 부위는 제자리에 남는다
        return pObject;
    }

    GameObject* CharacterAppearanceComponent::spawnRigidPart( GameObjectManager& manager, const ResolvedPart& part, vector<TComponentHandle<MeshSectionComponent>>& outListSection )
    {
        // 스키닝하지 않는 메시 조각이다(견갑판 · 투구 장식). 몸 뼈 대신 소켓 하나를 따르므로 소켓 부품과 같은 길로 붙인다(`bindSocketParts`).
        GameObject*    pObject = manager.createGameObject( part._partName );
        MeshComponent* pMesh   = pObject != nullptr ? pObject->addComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
        {
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
            return nullptr; // 부르는 쪽이 스폰하지 못했다고 알린다
        }
        pMesh->setMeshID( part._asset.c_str() );
        if ( part._material.empty() == false )
            pMesh->setMaterialPath( part._material.c_str() );
        outListSection.clear();
        CharacterAppearanceComponentInternal::applyPartSections( manager, *pObject, *pMesh, part, outListSection, _presetID );
        (void)pObject->addComponent<SocketBindingComponent>();
        return pObject;
    }

    GameObject* CharacterAppearanceComponent::spawnSpritePart( GameObjectManager& manager, const ResolvedPart& part )
    {
        // 2D 종이 인형 조각이다. 클립(`.sprite.xml`)의 첫 프레임을 그리고, 순서는 부품의 `layer` 다. 클립을 넘기는 애니메이터는 붙이지 않는다 — 조각은 몸 소켓을 따라 움직인다.
        GameObject*      pObject = manager.createGameObject( part._partName );
        SpriteComponent* pSprite = pObject != nullptr ? pObject->addComponent<SpriteComponent>() : nullptr;
        if ( pSprite == nullptr )
        {
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
            return nullptr;
        }
        pSprite->setClipPath( part._asset.c_str() );
        pSprite->setOrderInLayer( part._layer );
        if ( part._material.empty() == false )
            pSprite->setMaterialPath( part._material.c_str() );
        (void)pObject->addComponent<SocketBindingComponent>();
        return pObject;
    }

    void CharacterAppearanceComponent::refreshKeptPart( GameObjectManager& manager, SpawnedPart& inoutKept )
    {
        GameObject* pObject = manager.resolveGameObject( inoutKept._object );
        if ( inoutKept._bMerged == SW_TRUE || pObject == nullptr )
            return; // 합친 부품은 구간 머티리얼이 메시 해시에 들어가 다시 짓기가 고친다(`applyMergedSections`)
        const ResolvedPart& part = _state.getResolved()._listPart[inoutKept._partIndex];
        // 구간 머티리얼만 바뀐 메시 부품(변형 · 규칙의 구간 교체)은 같은 부품이다 — 다시 만들지 않고 구간만 고친다(같으면 그대로 둔다).
        MeshComponent* pMain = nullptr;
        if ( part.isSkinnedFollower() )
            pMain = pObject->getComponent<SkeletalMeshComponent>();
        else if ( part._kind == AppearancePartKind::Skinned )
            pMain = pObject->getComponent<MeshComponent>(); // 단단한 조각은 주 메시를 먼저 붙였다(`spawnRigidPart`)
        if ( pMain != nullptr )
            CharacterAppearanceComponentInternal::applyPartSections( manager, *pObject, *pMain, part, inoutKept._listSection, _presetID );
        // 그리기 순서만 바뀐 스프라이트도 같은 부품이다.
        SpriteComponent* pSprite = part._kind == AppearancePartKind::Sprite ? pObject->getComponent<SpriteComponent>() : nullptr;
        if ( pSprite != nullptr && pSprite->getOrderInLayer() != part._layer )
            pSprite->setOrderInLayer( part._layer );
    }

    void CharacterAppearanceComponent::syncParts( GameObjectManager& manager, SkeletalMeshComponent& body, int32 bodyPart,
                                                  const vector<AppearanceDetachEvent>& listDetachEvent )
    {
        using Internal = CharacterAppearanceComponentInternal;
        // 부품을 스폰하는 비용(풀이 내주지 못한 부품, 계획 ShooterCharacterIntegration S6)이 이 구간에 든다.
        SW_PROFILE_SCOPE( "GT.Appearance.syncParts" );
        // 남길 것을 고르고 없어진 것은 지운다. 같은 부품(주인, 이름, 에셋)은 그 오브젝트를 그대로 쓴다. 그래서 무기를 바꿔도 투구는 그대로 남는다.
        vector<SpawnedPart> listKeep;
        vector<uint8>       listPartCovered( _state.getResolved()._listPart.size(), SW_FALSE );
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            int32 matched = constant::invalid_index::kInt32;
            for ( size_t partIndex = 0; partIndex < _state.getResolved()._listPart.size(); ++partIndex )
            {
                const ResolvedPart& candidate  = _state.getResolved()._listPart[partIndex];
                const bool          bCandidate = listPartCovered[partIndex] == SW_FALSE && static_cast<int32>( partIndex ) != bodyPart;
                // 합친 부품이었는지도 같아야 같은 부품이다 — 병합을 켜거나 끄면 오브젝트로 두던 부품과 구간으로 두던 부품이 바뀐다.
                const bool bSameMode = ( spawned._bMerged == SW_TRUE ) == isMergedPart( candidate, static_cast<int32>( partIndex ) );
                const bool bSamePart = bSameMode && Internal::isSamePart( candidate, spawned._owner, spawned._partName, spawned._asset, spawned._material );
                if ( bCandidate && bSamePart )
                {
                    matched = static_cast<int32>( partIndex );
                    break;
                }
            }
            GameObject* pObject = manager.resolveGameObject( spawned._object );
            const bool  bAlive  = spawned._bMerged == SW_TRUE || pObject != nullptr;
            if ( matched < 0 || bAlive == false )
            {
                if ( spawned._bMerged == SW_TRUE )
                    retireMergedPart( manager, spawned );
                else if ( pObject != nullptr && releaseDetachedPart( *pObject, spawned, listDetachEvent ) == false )
                    retirePart( manager, *pObject, spawned, true ); // 바꾼 무기는 쉬게 해 두고 다른 캐릭터가 꺼내 쓴다
                continue;
            }
            listPartCovered[static_cast<size_t>( matched )] = SW_TRUE;
            SpawnedPart kept                                = spawned;
            kept._partIndex                                 = static_cast<uint32>( matched );
            refreshKeptPart( manager, kept );
            listKeep.push_back( kept );
        }
        _listSpawnedPart = std::move( listKeep );

        AssetManager*   pAssetManager = game::findSubsystem<AssetManager>();
        SceneComponent* pBodyScene    = body.getOwner() != nullptr ? body.getOwner()->getPrimarySceneComponent() : nullptr;
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
            spawned._material  = part._material;
            spawned._partIndex = static_cast<uint32>( partIndex );
            if ( isMergedPart( part, static_cast<int32>( partIndex ) ) )
            {
                // 몸 메시에 합칠 부품은 오브젝트를 만들지 않는다 — 구간 컴포넌트는 합친 메시를 건 뒤에 생긴다(`applyMergedMeshes`).
                spawned._bMerged     = SW_TRUE;
                spawned._bSocketPart = SW_FALSE;
                _listSpawnedPart.push_back( spawned );
                continue;
            }
            if ( part._kind == AppearancePartKind::SocketPrefab )
            {
                pObject = manager.getSubsystem<SocketPartPool>().takePart( Internal::makePoolKey( part._asset, part._material ) );
                if ( pObject == nullptr && pAssetManager != nullptr )
                {
                    pObject = pAssetManager->getAssetCache<PrefabCache>().spawn( &manager, part._asset.c_str(), part._partName.c_str() );
                    // 같은 메시를 머티리얼만 달리한 변형(`material` · `<MaterialVariant>`)이다. 풀에서 꺼낸 부품은 열쇠가 머티리얼을 담아 이미 입었다.
                    if ( pObject != nullptr && part._material.empty() == false )
                        Internal::forEachMesh( *pObject, [&part]( MeshComponent& mesh )
                        { mesh.setMaterialPath( part._material.c_str() ); } );
                }
                if ( pObject != nullptr && pObject->getComponent<SocketBindingComponent>() == nullptr )
                    (void)pObject->addComponent<SocketBindingComponent>();
                spawned._bPooled = SW_TRUE;
            }
            else if ( part.isSkinnedFollower() )
            {
                pObject              = spawnSkinnedPart( manager, body, pBodyScene, part, spawned._listSection );
                spawned._bSocketPart = SW_FALSE;
            }
            else if ( part._kind == AppearancePartKind::Skinned )
            {
                pObject = spawnRigidPart( manager, part, spawned._listSection ); // 휘지 않는 스킨드 — 소켓 하나를 따르는 단단한 메시
            }
            else if ( part._kind == AppearancePartKind::Sprite )
            {
                pObject = spawnSpritePart( manager, part );
            }
            if ( pObject == nullptr )
            {
                SW_LOG_WARNING_ID( "CharacterAppearance.AppearancePartCouldNot", "Appearance '%#' part '%#' (%#) could not be spawned", _presetID.c_str(), part._partName.c_str(), part._asset.c_str() );
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
                SW_LOG_WARNING_ID( "CharacterAppearance.AppearancePartNoActive", "Appearance '%#' part '%#' has no active socket among its candidates", _presetID.c_str(), part._partName.c_str() );
                continue;
            }
            GameObject* pHolder = findUnitObject( manager, unitIndex );
            if ( pHolder == nullptr || pHolder == pObject )
                continue;
            spawned._holderUnit            = unitIndex;
            const hashed_string socketName = part._placement._listSocket.empty() ? hashed_string{} : part._placement._listSocket[0];
            // 같은 holder 와 소켓이면 위치만 고친다. 다시 붙이면 되돌아가기 블렌드가 끊기기 때문이다.
            const bool     bSameSocketBinding = pBinding->getState() == SocketBindingState::Bound && pBinding->getHolder() == pHolder->getHandle() && pBinding->getSocketName() == socketName;
            const float4x4 inHolder           = CharacterAppearanceComponentInternal::toHolderSpace( *pHolder, inUnit );
            if ( bSameSocketBinding )
                pBinding->updateSocketTransform( inHolder );
            else
                (void)pBinding->bindToSocket( pHolder, socketName, inHolder ); // 못 붙이면 바인딩이 그대로이고 다음 갱신이 다시 붙인다
        }
    }

    void CharacterAppearanceComponent::bindRigUnits( GameObjectManager& manager )
    {
        PoseModifierComponent* pModifier = getOwner() != nullptr ? getOwner()->getComponent<PoseModifierComponent>() : nullptr;
        if ( pModifier == nullptr )
            return;
        vector<hashed_string>        listBound;
        unordered_set<hashed_string> uniqueSetBound; // 묶은 칸 — 목록은 묶은 순서를 들고 집합은 이미 묶었는지 답한다
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            const ResolvedPart& part     = _state.getResolved()._listPart[spawned._partIndex];
            const bool          bRigUnit = spawned._bSocketPart == SW_TRUE && part._owner.empty() == false && part._socketSet.empty() == false;
            if ( bRigUnit == false || uniqueSetBound.contains( part._owner ) )
                continue; // 한 칸에 소켓 에셋을 가진 부품이 둘이면 소켓 테이블처럼 첫 부품이 그 칸의 이름을 갖는다
            GameObject*                       pObject = manager.resolveGameObject( spawned._object );
            const shared_ptr<const SocketSet> sockets = _socketCache.findSharedSocketSet( part._socketSet );
            if ( pObject == nullptr || sockets == nullptr )
                continue; // 소켓 에셋을 읽지 못한 것은 소켓 테이블을 다시 만들 때 이미 알렸다
            pModifier->bindRigidUnit( part._owner, pObject, sockets );
            uniqueSetBound.insert( part._owner );
            listBound.push_back( part._owner );
        }
        for ( const hashed_string& slot : _listRigUnit )
        {
            if ( uniqueSetBound.contains( slot ) == false )
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
        using Internal                     = CharacterAppearanceComponentInternal;
        const ResolvedAppearance& resolved = _state.getResolved();
        const ResolvedPart*       pBody    = _bodyPart >= 0 ? &resolved._listPart[static_cast<size_t>( _bodyPart )] : nullptr;
        for ( const ResolvedMaterialValue& value : resolved._listMaterialValue )
        {
            if ( value._target != ResolvedMaterialTarget::Parameter )
                continue; // 염색 마스크 채널과 팔레트 슬롯은 그것을 읽는 머티리얼이 생기면 적용한다
            // 구간을 적은 값은 그 구간을 그리는 메시에만 건다(@p source 는 그 메시가 그리는 원본 구간 이름) — 부품마다, 구간마다 셰이더 효과를 달리한다.
            const auto applyToMesh = [&value]( MeshComponent& mesh, const hashed_string& source )
            {
                if ( value._section.empty() || value._section == source )
                    Internal::applyParameter( mesh, value );
            };
            const bool bBodyValue = value._owner.empty() && pBody != nullptr && ( value._part.empty() || pBody->_partName == value._part );
            if ( bBodyValue )
            {
                // 몸은 주 메시가 첫 구간을, `_listBodySection` 이 나머지를 원본 구간 이름으로 그린다(합쳐도 몸 구간 이름은 원본이다 — `makeMergePart`).
                applyToMesh( body, pBody->_listSection.empty() ? hashed_string{} : body.getSectionName() );
                for ( const TComponentHandle<MeshSectionComponent>& handle : _listBodySection )
                {
                    MeshSectionComponent* pSection = manager.resolveComponent( handle );
                    if ( pSection != nullptr )
                        applyToMesh( *pSection, pSection->getSectionName() );
                }
            }
            for ( const SpawnedPart& spawned : _listSpawnedPart )
            {
                const bool bOwned = spawned._owner == value._owner && ( value._part.empty() || spawned._partName == value._part );
                if ( bOwned )
                    forEachPartMesh( manager, spawned, applyToMesh );
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
            // 숨기는 칸의 부품은 몸이 보여도 숨긴다(1인칭의 머리 · 손 무기).
            const hashed_string& owner    = _state.getResolved()._listPart[spawned._partIndex]._owner;
            const bool           bVisible = _bPartsVisible && ( owner.empty() || VectorUtil::contains( _listHiddenSlot, owner ) == false );
            forEachPartMesh( manager, spawned, [bVisible]( MeshComponent& mesh, const hashed_string& /*source*/ )
            { mesh.setVisible( bVisible ); } );
        }
    }

    void CharacterAppearanceComponent::rebuildSurfaceState( uint32 regionCount )
    {
        if ( _surfaceChannelsPath.empty() )
        {
            if ( hasSurfaceState() )
            {
                _surfaceBinding.releaseTextures();
                _surface = CharacterSurfaceState{};
                _loadedSurfaceChannels.clear();
                _surfaceRegionCount = 0;
            }
            return;
        }
        // 부품 칸이 늘면 다시 만든다(마스크가 부품 번호별이라 늘리는 길이 없다). 같은 파일 · 같은 영역 수 · 충분한 부품 칸이면 값을 지킨다 — 장비를 바꿔도 젖음 · 피가 사라지지 않게.
        const uint32 partCount = static_cast<uint32>( _state.getResolved()._listPart.size() );
        const bool   bSameData = _loadedSurfaceChannels == _surfaceChannelsPath && _surfaceRegionCount == regionCount;
        const bool   bFits     = hasSurfaceState() && _surface.getPartCount() >= partCount;
        if ( bSameData && bFits )
            return;
        // 채널 표는 공유 캐시의 한 벌이다 — 캐릭터마다 다시 읽지 않는다(표면 상태는 만들 때 표를 복사해 든다).
        const shared_ptr<const SurfaceChannelTable> pChannels = SharedAssetCache<SurfaceChannelTable>::acquire( _surfaceChannelsPath );
        if ( pChannels == nullptr )
        {
            SW_LOG_ERROR_ID( "CharacterAppearance.AppearanceSurfaceChannelsCould", "Appearance '%#' surface channels '%#' could not be read - the surface state stays off", _presetID.c_str(), _surfaceChannelsPath.c_str() );
            _surfaceBinding.releaseTextures();
            _surface = CharacterSurfaceState{};
            _loadedSurfaceChannels.clear();
            _surfaceRegionCount = 0;
            return;
        }
        _surfaceBinding.releaseTextures();
        _surface.initialize( *pChannels, regionCount, partCount );
        _loadedSurfaceChannels  = _surfaceChannelsPath;
        _surfaceRegionCount     = regionCount;
        _surfaceAppliedRevision = constant::invalid_index::kUint64;
    }

    void CharacterAppearanceComponent::applySurface( GameObjectManager& manager, bool bForce )
    {
        const bool bUpToDate = bForce == false && _surfaceAppliedRevision == _surface.getRevision();
        if ( hasSurfaceState() == false || bUpToDate )
            return;
        const GameObject* pOwner    = getOwner();
        const uint64      ownerKey  = pOwner != nullptr ? pOwner->getHandle().objectID() : 0;
        bool              bComplete = true; // 머티리얼이 아직 없는 메시가 있으면 번호를 남기지 않아 다음 틱에 다시 시도한다
        for ( uint32 partIndex = 0; partIndex < _surface.getPartCount() && partIndex < _state.getResolved()._listPart.size(); ++partIndex )
        {
            const auto applyToMesh = [this, ownerKey, partIndex, &bComplete]( MeshComponent& mesh, const hashed_string& /*source*/ )
            {
                MaterialInstance* pInstance = CharacterSurfaceBinding::acquireInstance( mesh );
                if ( pInstance == nullptr )
                {
                    bComplete = false;
                    return;
                }
                CharacterSurfaceBinding::applyValues( _surface, *pInstance );
                _surfaceBinding.applyMasks( ownerKey, _surface, partIndex, *pInstance );
            };
            if ( static_cast<int32>( partIndex ) == _bodyPart )
            {
                SkeletalMeshComponent* pBody = pOwner != nullptr ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
                if ( pBody != nullptr )
                    applyToMesh( *pBody, hashed_string{} );
                continue;
            }
            for ( const SpawnedPart& spawned : _listSpawnedPart )
            {
                if ( spawned._partIndex == partIndex && spawned._bSocketPart == SW_FALSE )
                    forEachPartMesh( manager, spawned, applyToMesh );
            }
        }
        if ( bComplete )
            _surfaceAppliedRevision = _surface.getRevision();
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
                pBinding->updateSocketTransform( CharacterAppearanceComponentInternal::toHolderSpace( *getOwner(), inUnit ) );
        }
    }

    // ------------------------------------------------------------------------------
    // 베이크한 메시(체형 · 피팅)
    // ------------------------------------------------------------------------------
    /**
     * @struct CharacterAppearanceMeshBuild
     * @brief 워커에서 짓는 조립 하나입니다 — 게임 스레드가 입력을 채워 내고(`startMeshBuild`), 워커가 굽고 · 합치고 · LOD 를 만들며(`run`), 끝나면 게임 스레드가 건다(`applyMeshAssembly`).
     * @details 워커는 컴포넌트 · 서브시스템을 보지 않는다 — 캐시 읽기 · 쓰기와 LOD 표 찾기는 게임 스레드가 한다. 메시 · 피팅 데이터는 쥔 참조로 살아 있다.
     *          새 조립이 이것을 버려도 태스크가 자기 참조를 쥐어 끝까지 돌고 결과는 버려진다.
     */
    struct CharacterAppearanceMeshBuild
    {
        // 입력(게임 스레드가 채운다)
        string                               _presetID{};
        ResolvedAppearance                   _resolved{};          ///< 체형 모프를 더한 해석
        vector<AppearanceMeshPart>           _listPart{};          ///< 로드 순서(몸이 먼저). 포인터는 아래 쥔 메시 · 피팅 데이터를 가리킨다
        vector<shared_ptr<Mesh>>             _listSource{};        ///< 로드마다 원본 메시
        vector<IdentityToken>                _listSourceContent{}; ///< 키(`_bakeKey`)에 섞은 원본 내용 토큰 — 캐시 항목이 들어 키의 주소가 다른 원본에 다시 쓰이지 않는다
        vector<shared_ptr<const FitPartDef>> _listFit{};           ///< 로드마다 피팅 데이터(없으면 nullptr)
        vector<uint32>                       _listLoadPartIndex{}; ///< 로드마다 해석의 부품 번호
        vector<uint32>                       _listMergeLoad{};     ///< 합칠 로드 번호(몸이 먼저). 비면 합치지 않는다
        shared_ptr<const FitTables>          _tables{};
        CharacterBoneArray                   _bindBones{};
        const CharacterLODSettings*          _pLODSettings{ nullptr };
        uint64                               _bakeKey{ 0 };
        uint64                               _mergedKey{ 0 };
        int32                                _bodyPart{ constant::invalid_index::kInt32 };
        bool                                 _bPreview{ false };    ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool                                 _bBakeCached{ false }; ///< 구운 메시를 캐시에서 받았다(굽지 않는다) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        // 입력이자 결과
        string                   _report{};    ///< 읽기 · 굽기 실패의 까닭
        vector<shared_ptr<Mesh>> _listBaked{}; ///< 로드마다 베이크한 사본(원본을 그리면 nullptr)
        shared_ptr<Mesh>         _merged{};    ///< 캐시에서 받았거나 합친 메시. 합치지 않거나 못 했으면 nullptr
        // 결과(워커가 채운다)
        FitResult             _fitResult{};          ///< 미리보기가 푼 피팅 결과
        vector<MeshMergePart> _listMergePart{};      ///< `_listMergeLoad` 마다 병합 조각 — 몸 유닛 · 구간 컴포넌트가 이름을 읽는다
        bool                  _bBakeBuilt{ false };  ///< 이번에 구웠다(캐시에 남긴다) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool                  _bMergeBuilt{ false }; ///< 이번에 합쳤다(캐시에 남긴다) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        atomic<bool>          _bDone{ false };

        /** @brief 워커에서 굽고 합치고 LOD 를 만듭니다(끝나면 태스크가 `_bDone` 을 건다). */
        void run();
    };

    void CharacterAppearanceComponent::requestMeshAssembly( GameObjectManager& manager )
    {
        _listMeshAssemblyLoad.clear();
        _fitTablesLoad.reset();
        _meshBuild.reset();
        manager.getPrimitiveRegistry().removeAssetWaiter( this );
        _bMeshAssemblyPending                    = SW_FALSE;
        const ResolvedAppearance& resolved       = _state.getResolved();
        const ResolvedPart*       pBodyPart      = _bodyPart >= 0 ? &resolved._listPart[static_cast<size_t>( _bodyPart )] : nullptr;
        const bool                bHasTables     = pBodyPart != nullptr && pBodyPart->_fitTables.empty() == false;
        const bool                bBakes         = bHasTables || resolved._listMorph.empty() == false || _listShapeMorph.empty() == false || hasMergedParts() || _bMeshMerged == SW_TRUE;
        const bool                bNothingToBake = bBakes == false && _bMeshAssembled == SW_FALSE; // 베이크하지도 않고 되돌릴 것도 없다(대부분의 캐릭터)
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
                SW_LOG_ERROR_ID( "CharacterAppearance.AppearancePartFitData", "Appearance '%#' part '%#' has fit data but its own skeleton '%#' - it is drawn unfitted", _presetID.c_str(), part._partName.c_str(), part._skeleton.c_str() );
            else if ( part._fit.empty() == false )
                load._fit = SharedAssetCache<FitPartDef>::request( part._fit.c_str() );
            _listMeshAssemblyLoad.push_back( std::move( load ) );
        }
        _bMeshAssemblyPending = SW_TRUE;
        // 씬의 에셋 대기 목록에 건다 — 도착 단계가 받아 걸고, 자동화의 "에셋 일 0"(`Assets.Pending`)이 그동안 사진을 미룬다(옷이 숨은 몸이 찍히지 않게).
        manager.getPrimitiveRegistry().addAssetWaiter( this, &CharacterAppearanceComponent::waitMeshAssembly );
        // 새로 입은 피팅 부품은 베이크한 메시를 걸 때까지 숨긴다 — 피팅 전의 옷은 몸을 뚫고 나온다. 이미 베이크한 것을 그리는 부품은 다시 조립하는 동안 그대로 보인다.
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            GameObject* pObject    = manager.resolveGameObject( spawned._object );
            const bool  bUnfitted  = spawned._bSocketPart == SW_FALSE && spawned._bBaked == SW_FALSE;
            const bool  bHidesPart = bUnfitted && resolved._listPart[spawned._partIndex]._fit.empty() == false;
            if ( pObject != nullptr && bHidesPart )
                CharacterAppearanceComponentInternal::forEachMesh( *pObject, []( MeshComponent& mesh )
                { mesh.setVisible( false ); } );
        }
    }

    bool CharacterAppearanceComponent::waitMeshAssembly( Component& component, float32 /*deltaSeconds*/ )
    {
        // 이 함수를 거는 것은 외형 컴포넌트 자신뿐이다(`requestMeshAssembly`).
        CharacterAppearanceComponent& self     = static_cast<CharacterAppearanceComponent&>( component );
        GameObjectManager*            pManager = self.findManager();
        if ( self._bMeshAssemblyPending == SW_FALSE || pManager == nullptr )
            return false;
        if ( self.isMeshAssemblyLoading() )
            return true;
        // 다 읽었으면 워커에서 짓고, 지은 것을 기다린다 — 그동안도 대기 목록에 남아 "에셋 일 0" 이 이 조립을 센다.
        if ( self._meshBuild == nullptr )
        {
            self.startMeshBuild();
            return true;
        }
        if ( self._meshBuild->_bDone.load( std::memory_order_acquire ) == false )
            return true;
        self._bMeshAssemblyPending = SW_FALSE;
        self._bMeshApplyQueued     = SW_TRUE;
        // 유닛의 메시 · 구간 컴포넌트를 바꾸는 일은 대기 목록을 도는 중에 하지 않는다 — 같은 프레임의 틱 뒤 큐(`ApplyPrePhysics`)가 건다.
        pManager->deferPostTickOnComponent( self, []( CharacterAppearanceComponent& appearance )
        { appearance.applyMeshAssembly(); } );
        return false;
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

    bool CharacterAppearanceComponent::applyUnitMeshes( GameObjectManager& manager, SkeletalMeshComponent* pBodyUnit, bool bMerged, const vector<shared_ptr<Mesh>>& listSource,
                                                        const vector<shared_ptr<Mesh>>& listBaked )
    {
        bool bAnyBaked = false;
        for ( size_t index = 0; index < _listMeshAssemblyLoad.size(); ++index )
        {
            const uint32 partIndex = _listMeshAssemblyLoad[index]._partIndex;
            // 합쳐진 부품(몸 포함)은 호출 앞에서 걸었다.
            bool bInMerge = bMerged && static_cast<int32>( partIndex ) == _bodyPart;
            for ( const SpawnedPart& spawned : _listSpawnedPart )
            {
                bInMerge = bInMerge || ( bMerged && spawned._bMerged == SW_TRUE && spawned._partIndex == partIndex );
            }
            if ( bInMerge )
                continue;
            SkeletalMeshComponent* pUnit = findSkinnedUnit( manager, partIndex );
            shared_ptr<Mesh>       mesh  = listBaked[index] != nullptr ? listBaked[index] : listSource[index];
            bAnyBaked                    = bAnyBaked || listBaked[index] != nullptr;
            // 합쳤던 몸이 합치지 않게 되면 구간 이름을 몸의 첫 `<Section>`(없으면 메시 전체)으로 되돌리고 원본(또는 베이크한 사본)으로 되돌린다.
            // 몸의 구간은 합친 메시에서도 원본 이름이라(`makeMergePart`) 메시를 바꾸기 전에 걸어도 된다.
            const bool bUnmerge = pUnit != nullptr && pUnit == pBodyUnit && _bMeshMerged == SW_TRUE;
            if ( bUnmerge )
            {
                const ResolvedPart& body = _state.getResolved()._listPart[partIndex];
                pUnit->setSectionName( body._listSection.empty() ? hashed_string{} : body._listSection.front()._name );
                _bMeshMerged = SW_FALSE;
            }
            // 지난번에도 원본이었고 이번에도 원본이면 건드리지 않는다(사본 · 군중 묶음을 다시 만들지 않게).
            const bool bUnchanged = listBaked[index] == nullptr && _bMeshAssembled == SW_FALSE && bUnmerge == false;
            const bool bApplies   = pUnit != nullptr && mesh != nullptr;
            if ( bApplies && bUnchanged == false )
                pUnit->setSkinSourceMesh( std::move( mesh ) );
            for ( SpawnedPart& spawned : _listSpawnedPart )
            {
                if ( spawned._partIndex == partIndex && spawned._bSocketPart == SW_FALSE )
                    spawned._bBaked = listBaked[index] != nullptr ? SW_TRUE : SW_FALSE;
            }
        }
        return bAnyBaked;
    }

    void CharacterAppearanceComponent::startMeshBuild()
    {
        using Internal                                       = CharacterAppearanceComponentInternal;
        const shared_ptr<CharacterAppearanceMeshBuild> build = make_shared<CharacterAppearanceMeshBuild>();
        CharacterAppearanceMeshBuild&                  job   = *build;
        job._presetID                                        = _presetID;
        job._resolved                                        = _state.getResolved();
        // 체형 축이 정한 몸 모프(주인이 빈 모프)를 해석의 모프에 더해 굽는다. 같은 이름이 해석에 있으면 해석의 것이 이긴다(데이터가 직접 정한 값).
        for ( const BodyMorphWeight& shapeMorph : _listShapeMorph )
        {
            bool bDeclared = false;
            for ( const ResolvedMorph& morph : job._resolved._listMorph )
            {
                bDeclared = bDeclared || ( morph._owner.empty() && morph._name == shapeMorph._morph );
            }
            if ( bDeclared == false )
                job._resolved._listMorph.push_back( ResolvedMorph{ hashed_string{}, shapeMorph._morph, shapeMorph._weight } );
        }
        // 읽기 실패는 데이터 오류다 — 어느 것인지 알리고 원본을 그린다(베이크한 것과 원본이 섞인 차림을 만들지 않는다).
        if ( _fitTablesLoad.hasFailed() )
            Internal::appendReport( job._report, "fit tables: %#", _fitTablesLoad.getError().c_str() );
        for ( MeshAssemblyLoad& load : _listMeshAssemblyLoad )
        {
            const ResolvedPart&                part = job._resolved._listPart[load._partIndex];
            shared_ptr<Mesh>                   mesh = load._mesh.get();
            const shared_ptr<const FitPartDef> fit  = load._fit.get();
            if ( load._fit.hasFailed() || mesh == nullptr )
                Internal::appendReport( job._report, "part '%#' mesh or fit data could not be read", part._partName.c_str() );
            load._sourceContentID = mesh != nullptr ? mesh->getContentID() : IdentityToken{};
            job._listSourceContent.push_back( load._sourceContentID );
            job._listPart.push_back( AppearanceMeshPart{ part._partName, part._owner, mesh.get(), fit.get() } );
            job._listSource.push_back( std::move( mesh ) );
            job._listFit.push_back( fit );
            job._listLoadPartIndex.push_back( load._partIndex );
        }
        _seenFitReloadStamp = computeFitReloadStamp();
        // 같은 차림이면 이미 베이크한 것을 나눈다 — 키는 메시에 들어가는 해석(메시 해시) · 부품 · 원본 내용 · 데이터 판이다.
        job._bakeKey = HashUtil::combine( job._resolved._meshHash, _seenFitReloadStamp );
        for ( const MeshAssemblyLoad& load : _listMeshAssemblyLoad )
        {
            job._bakeKey = HashUtil::combine( HashUtil::combine( job._bakeKey, load._partIndex ), load._sourceContentID.getKey() ); // 키는 토큰 주소 — 캐시 항목이 토큰을 들어 겹치지 않는다
        }
        job._tables    = _fitTablesLoad.isValid() ? _fitTablesLoad.get() : nullptr;
        job._bindBones = _bodyBindBones;
        // 미리보기 입히기는 피팅 결과를 창에 보이므로 구운 메시 캐시를 건너뛰고 늘 푼다(같은 차림이 캐시된 메시를 나누면 푼 결과가 없다).
        job._bPreview    = _pPreviewDatabase != nullptr;
        job._bBakeCached = job._report.empty() && job._bPreview == false && AppearanceBakedMeshCache::tryGet( job._bakeKey, job._listBaked );
        // 합칠 것: 몸이 먼저(로드 0), 그 뒤 합치는 부품(로드 순서). 같은 차림이면 합친 메시도 나눈다(구운 메시와 같은 키에 값을 섞은 키).
        for ( size_t index = 0; index < _listMeshAssemblyLoad.size() && hasMergedParts(); ++index )
        {
            const uint32 partIndex = _listMeshAssemblyLoad[index]._partIndex;
            bool         bMember   = static_cast<int32>( partIndex ) == _bodyPart;
            for ( const SpawnedPart& spawned : _listSpawnedPart )
            {
                bMember = bMember || ( spawned._bMerged == SW_TRUE && spawned._partIndex == partIndex );
            }
            if ( bMember )
                job._listMergeLoad.push_back( static_cast<uint32>( index ) );
        }
        job._mergedKey = HashUtil::combine( job._bakeKey, Internal::kMergedMeshKeySalt );
        vector<shared_ptr<Mesh>> listCached;
        if ( job._listMergeLoad.empty() == false && AppearanceBakedMeshCache::tryGet( job._mergedKey, listCached ) && listCached.size() == 1 )
            job._merged = listCached[0];
        // 같은 차림을 짓는 중이면 그것을 나눈다(결과가 같다). 미리보기는 나누지 않는다 — 걸 때 피팅 결과를 옮겨 간다. 읽기 오류도 나누지 않는다(차림마다 알린다).
        CharacterAppearanceInFlightTable* const  pInFlight = game::findSubsystem<CharacterAppearanceInFlightTable>();
        const bool                               bShares   = pInFlight != nullptr && job._bPreview == false && job._report.empty();
        const uint64                             buildKey  = job._listMergeLoad.empty() ? job._bakeKey : job._mergedKey;
        shared_ptr<CharacterAppearanceMeshBuild> inFlight;
        if ( bShares )
        {
            const auto found = pInFlight->_mapBuild.find( buildKey );
            inFlight         = found != pInFlight->_mapBuild.end() ? found->second.lock() : nullptr;
        }
        if ( inFlight != nullptr )
        {
            _meshBuild = std::move( inFlight );
            return;
        }
        job._bodyPart     = _bodyPart;
        job._pLODSettings = &CharacterLODSettings::getDefault(); // 엔진 서브시스템을 읽으므로 게임 스레드에서 한 번
        _meshBuild        = build;
        if ( bShares )
        {
            unordered_map<uint64, weak_ptr<CharacterAppearanceMeshBuild>>& mapInFlight = pInFlight->_mapBuild;
            for ( auto it = mapInFlight.begin(); it != mapInFlight.end(); )
            {
                it = it->second.expired() ? mapInFlight.erase( it ) : std::next( it );
            }
            mapInFlight[buildKey] = build;
        }
        // 언리얼 Mutable 처럼 메시 짓기는 워커에서 한다(마을 사람 하나에 수십 ms). 로드 단계에 넣어 결정적 실행의 프레임 경계가 기다린다(`AssetManager::finishPendingAssetWork`).
        game::getSubsystem<AssetManager>().launchLoadWork( "AppearanceMeshBuild", SW_DELEGATE_LAMBDA( TaskDelegate, [build]()
        {
            build->run();
            build->_bDone.store( true, std::memory_order_release );
        } ) );
    }

    void CharacterAppearanceMeshBuild::run()
    {
        using Internal = CharacterAppearanceComponentInternal;
        SW_PROFILE_SCOPE( "Appearance.MeshBuild" );
        if ( _report.empty() && _bBakeCached == false )
            _bBakeBuilt = AppearanceMeshPart::assemble( _resolved, _listPart, _tables.get(), _bindBones, _listBaked, _report, _bPreview ? &_fitResult : nullptr );
        if ( _listBaked.size() != _listPart.size() )
        {
            SW_LOG_ERROR_ID( "CharacterAppearance.AppearanceMeshesDrawnUnbaked", "Appearance '%#' meshes are drawn unbaked: %#", _presetID.c_str(), _report.c_str() );
            _listBaked.assign( _listPart.size(), nullptr );
        }
        // 합칠 부품의 메시는 베이크한 사본(없으면 원본)이다. 읽지 못한 메시가 있으면 합치지 않는다 — 읽기 오류는 이미 알렸다(몸 원본만 그려진다).
        vector<CharacterLODSectionRule> listLODRule;
        for ( const uint32 loadIndex : _listMergeLoad )
        {
            const shared_ptr<Mesh>& mesh      = _listBaked[loadIndex] != nullptr ? _listBaked[loadIndex] : _listSource[loadIndex];
            const uint32            partIndex = _listLoadPartIndex[loadIndex];
            const ResolvedPart&     part      = _resolved._listPart[partIndex];
            if ( mesh == nullptr )
            {
                SW_LOG_ERROR_ID( "CharacterAppearance.AppearanceCannotMergeParts", "Appearance '%#' cannot merge parts - part '%#' has no mesh", _presetID.c_str(), part._partName.c_str() );
                _listMergePart.clear();
                _merged.reset();
                return;
            }
            _listMergePart.push_back( Internal::makeMergePart( part, *mesh, static_cast<int32>( partIndex ) == _bodyPart ) );
            Internal::appendLODRules( part, _listMergePart.back(), listLODRule );
        }
        if ( _merged != nullptr || _listMergePart.empty() )
            return;
        const expected<MeshMergeResult> result = CharacterMeshUtil::mergeMeshes( _listMergePart, nullptr );
        if ( result.has_value() == false )
        {
            SW_LOG_ERROR_ID( "CharacterAppearance.AppearancePartsDrawnUnmerged", "Appearance '%#' parts are drawn unmerged: %# '%#'", _presetID.c_str(), result.error().message(), result.error().detail() );
            _listMergePart.clear();
            return;
        }
        _merged = result->_mesh;
        // 합친 메시의 LOD 는 여기서 한 번 만든다(조립한 메시라 임포트 LOD 가 없다) — 같은 차림은 캐시로 나눈다. 만들지 못하면 오류를 남기고 LOD 0 만 그린다.
        (void)CharacterLODSettings::buildLODs( *_merged, *_pLODSettings, listLODRule );
        _bMergeBuilt = true;
    }

    void CharacterAppearanceComponent::applyMeshAssembly()
    {
        // 틱이 이 걸기를 예약한 뒤 같은 틱 뒤에 다시 조립했으면(새 요청) 새 것이 아직 읽거나 짓는 중이다 — 다음 틱이 건다.
        _bMeshApplyQueued              = SW_FALSE;
        GameObjectManager* pManager    = findManager();
        const bool         bSuperseded = _bMeshAssemblyPending == SW_TRUE || _meshBuild == nullptr;
        if ( pManager == nullptr || bSuperseded )
            return;
        const shared_ptr<CharacterAppearanceMeshBuild> build = std::move( _meshBuild );
        CharacterAppearanceMeshBuild&                  job   = *build;
        // 캐시는 게임 스레드에서 채운다(워커는 서브시스템을 보지 않는다).
        if ( job._bBakeBuilt && job._bPreview )
        {
            _lastFitResult = make_shared<FitResult>( std::move( job._fitResult ) );
            ++_fitResultRevision;
        }
        else if ( job._bBakeBuilt )
            AppearanceBakedMeshCache::store( job._bakeKey, job._listBaked, job._listSourceContent );
        if ( job._bMergeBuilt )
            AppearanceBakedMeshCache::store( job._mergedKey, { job._merged }, job._listSourceContent );
        // 합친 메시가 있으면 몸 유닛에 건다. 합치지 못했으면(오류는 워커가 남겼다) 몸 원본만 그려지고 아래 부품별 길로 간다.
        SkeletalMeshComponent* pBodyUnit = _bodyPart >= 0 ? findSkinnedUnit( *pManager, static_cast<uint32>( _bodyPart ) ) : nullptr;
        const bool             bMerged   = job._merged != nullptr && job._listMergePart.empty() == false && pBodyUnit != nullptr && applyMergedMeshes( *pManager, *pBodyUnit, job );
        const bool             bAnyBaked = applyUnitMeshes( *pManager, pBodyUnit, bMerged, job._listSource, job._listBaked ) || bMerged;
        _bMeshAssembled                  = bAnyBaked ? SW_TRUE : SW_FALSE;
        applyVisibility( *pManager );
        // 영역 수는 피팅 표의 영역 수다(표가 없으면 영역이 없다). 합친 구간 컴포넌트가 이제 생겼으니 표면도 다시 올린다.
        rebuildSurfaceState( job._tables != nullptr ? static_cast<uint32>( job._tables->getRegions().size() ) : 0 );
        applySurface( *pManager, true );
    }

    bool CharacterAppearanceComponent::applyMergedMeshes( GameObjectManager& manager, SkeletalMeshComponent& body, const CharacterAppearanceMeshBuild& build )
    {
        using Internal     = CharacterAppearanceComponentInternal;
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return false;
        // 몸 유닛에 합친 메시를 걸고, 몸은 자기 첫 조각을 그린다(나머지 몸 구간은 `_listBodySection` 이 원본 이름 그대로 그린다).
        // 옛 구간 이름은 새 메시에 없을 수 있어 먼저 푼다(없는 이름 오류를 남기지 않게).
        const MeshMergePart& bodyMerge = build._listMergePart.front(); // 몸이 로드 0 이라 늘 먼저다
        body.setSectionName( hashed_string{} );
        body.setSkinSourceMesh( build._merged );
        body.setSectionName( bodyMerge._listSection.empty() ? bodyMerge._name : bodyMerge._listSection.front()._name );
        _bMeshMerged = SW_TRUE;

        // 합친 부품의 조각마다 몸 오브젝트에 구간 컴포넌트를 둔다(있으면 그대로 두고 이름 · 머티리얼만 맞춘다).
        for ( SpawnedPart& spawned : _listSpawnedPart )
        {
            optional<size_t> mergeIndex;
            for ( size_t index = 0; index < build._listMergeLoad.size() && mergeIndex.has_value() == false; ++index )
            {
                if ( build._listLoadPartIndex[build._listMergeLoad[index]] == spawned._partIndex )
                    mergeIndex = index;
            }
            if ( spawned._bMerged == SW_FALSE || mergeIndex.has_value() == false )
                continue;
            Internal::applyMergedSections( manager, *pOwner, build._listMergePart[*mergeIndex], build._resolved._listPart[spawned._partIndex], spawned._listSection, _presetID );
        }
        // 구간 컴포넌트가 이제 생겼으니 염색을 부품마다 다시 입힌다.
        applyMaterialValues( manager, body );
        return true;
    }

    bool CharacterAppearanceComponent::isMeshAssemblyStale() const
    {
        // 체형 축 데이터를 고치면 본 비율 · 모프를 다시 풀어야 한다(굽지 않는 몸도 해당).
        if ( _bodyShapeSet != nullptr && _shapeReload.isStale() )
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
        return SharedAssetCache<FitTables>::getReloadCount() + SharedAssetCache<FitPartDef>::getReloadCount();
    }

    void CharacterAppearanceComponent::registerSocketTask( SkeletalMeshComponent& body )
    {
        const TComponentHandle<SkeletalMeshComponent> bodyHandle{ &body };
        if ( _taskUnit == bodyHandle )
            return;
        unregisterSocketTask();
        _socketTaskRegistration = body.registerAnimationPhaseTask( &_socketTask );
        _taskUnit               = bodyHandle;
    }

    void CharacterAppearanceComponent::unregisterSocketTask()
    {
        // 유닛이 먼저 사라졌으면 구독은 이미 무효다 — 푸는 것이 할 일 없이 끝난다.
        _taskUnit = TComponentHandle<SkeletalMeshComponent>{};
        _socketTaskRegistration.reset();
        _proportionTaskRegistration.reset();
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
                return spawned._bMerged == SW_TRUE ? getOwner() : manager.resolveGameObject( spawned._object ); // 합친 부품은 몸 유닛이 그린다
        }
        return nullptr;
    }

    uint32 CharacterAppearanceComponent::getMergedSectionCount() const
    {
        if ( _bMeshMerged == SW_FALSE || _bodyPart < 0 )
            return 0;
        const size_t bodySectionCount = _state.getResolved()._listPart[static_cast<size_t>( _bodyPart )]._listSection.size();
        size_t       count            = bodySectionCount > 0 ? bodySectionCount : 1u; // 몸의 조각
        for ( const SpawnedPart& spawned : _listSpawnedPart )
        {
            if ( spawned._bMerged == SW_TRUE )
                count += spawned._listSection.size();
        }
        return static_cast<uint32>( count );
    }

    GameObjectManager* CharacterAppearanceComponent::findManager() const
    {
        GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getManager() : nullptr;
    }
} // namespace sw
