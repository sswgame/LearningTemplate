/**
 * @file GameObjectManager.cpp
 * @brief GameObjectManager 의 소유 · 비우기, 이름으로 컴포넌트 만들기, 프레임 경로(tick 의 단계 · 물리 · 트랜스폼 전달)입니다. 저장소는 `GameObjectStore`, 디스패치는 `SceneTickScheduler` 입니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.inl"

#include "Core/Foundation/StdHeaders.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneTransformHierarchy.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/ObjectIDAllocator.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Object/GameObject/Registry/TagIndex.h"
#include "Engine/Object/GameObject/Service/ISceneService.h"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Object/GameObject/Tick/ISceneFrameSystem.h"
#include "Engine/Object/GameObject/Tick/SceneTickScheduler.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/TypeRegistry.h"

// FrameSystemLanePlan
namespace sw
{
    namespace
    {
        constexpr bool intersects( const SceneDataKindSet& left, const SceneDataKindSet& right )
        {
            return left.intersect( right ).isEmpty() == false;
        }
    } // namespace

    bool hasFrameAccessConflict( const SceneFrameAccess& earlier, const SceneFrameAccess& later )
    {
        // 쓰기 · 쓰기, 쓰기 · 읽기(이번 프레임 · 지난 프레임 모두), 읽기 · 쓰기가 겹침이다. 읽기 · 읽기는 겹치지 않는다.
        if ( intersects( earlier._writes, later._writes ) || intersects( earlier._writes, later._reads ) || intersects( earlier._writes, later._readsPrevious ) )
            return true;
        return intersects( earlier._reads, later._writes ) || intersects( earlier._readsPrevious, later._writes );
    }

    void buildFrameSystemLanePlan( span<const SceneFrameAccess> listAccess, FrameSystemLanePlan& outPlan )
    {
        // 파도 벡터는 비우되 안의 용량은 프레임 사이에 둔다(`_listSystem` 만 비운다).
        for ( FrameSystemWave& wave : outPlan._listWave )
        {
            wave._listSystem.clear();
        }
        uint32 waveCount = 0;
        // 앞 시스템의 파도 번호 — 계획은 입력 순서로 한 번 훑어 만든다(앞 시스템이 이미 정해져 있다).
        vector<uint32>& listWaveOf = outPlan._listWaveOf;
        listWaveOf.clear();
        listWaveOf.reserve( listAccess.size() );
        for ( size_t systemIndex = 0; systemIndex < listAccess.size(); ++systemIndex )
        {
            uint32 wave = 0;
            for ( size_t earlierIndex = 0; earlierIndex < systemIndex; ++earlierIndex )
            {
                if ( hasFrameAccessConflict( listAccess[earlierIndex], listAccess[systemIndex] ) )
                    wave = MathUtil::max( wave, listWaveOf[earlierIndex] + 1 );
            }
            listWaveOf.push_back( wave );
            if ( outPlan._listWave.size() <= wave )
                outPlan._listWave.resize( wave + 1 );
            outPlan._listWave[wave]._listSystem.push_back( static_cast<uint32>( systemIndex ) );
            waveCount = MathUtil::max( waveCount, wave + 1 );
        }
        outPlan._listWave.resize( waveCount );
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct GameObjectManagerInternal
        {
            /**
             * @brief 이름으로 컴포넌트를 만들 타입을 리플렉션 표에서 찾습니다. 없으면 nullptr 입니다.
             * @details 짧은 이름 · FQN · 옛 이름(`REFLECT( Alias = Old )`)을 `findType` 이 모두 받는다. 못 찾으면 `이름#번호` 꼬리(같은 타입 둘째 컴포넌트의
             *          저장 이름)를 떼고 한 번 더 찾는다.
             */
            static const TypeInfo* findComponentType( hashed_string typeName )
            {
                const TypeRegistry& registry = engine::getSubsystem<TypeRegistry>();
                const TypeInfo*     pType    = registry.findType( typeName );
                if ( pType != nullptr )
                    return pType;

                const utf8* pRawName = typeName.c_str();
                if ( pRawName == nullptr )
                    return nullptr;
                const utf8* pHash = nullptr;
                for ( const utf8* pCursor = pRawName; *pCursor != '\0'; ++pCursor )
                {
                    if ( *pCursor == '#' )
                        pHash = pCursor;
                }
                if ( pHash == nullptr || pHash == pRawName )
                    return nullptr;
                return registry.findType( hashed_string( pRawName, static_cast<uint32>( pHash - pRawName ) ) );
            }

            /**
             * @brief 스레드마다 둔 이름 → 타입 조회 캐시입니다. 타입 표의 **캐시**일 뿐 레지스트리가 아닙니다 — 표의 세대가 바뀌면 통째로 버립니다.
             * @details 씬 · 프리팹 로드는 컴포넌트마다 이름으로 찾는다. 표 조회는 공유 잠금 + 해시 두 번(짧은 이름 → FQN → 줄)이라
             *          컴포넌트 하나에 10 ns 남짓을 더했다(Release `GameObjectBenchTest.AddComponentByName`). `TypeInfo` 주소는 고정이고
             *          해제는 세대를 올리므로 포인터를 들고 있어도 된다. 생성 함수(`_addComponent`)는 부를 때마다 다시 읽는다.
             */
            struct ComponentTypeCache
            {
                static constexpr uint32 kSlotCount = 64;
                struct Slot
                {
                    uint32          _nameIndex; ///< `hashed_string::getIndex()`(대소문자 무시 비교 키)
                    const TypeInfo* _pType;     ///< 비었으면 nullptr
                };
                uint32 _generation; ///< 채울 때의 `TypeRegistry::getGeneration`
                Slot   _arrSlot[kSlotCount];
            };
            inline static thread_local ComponentTypeCache s_typeCache{};

            /** @brief `findComponentType` 의 캐시 앞단입니다. 못 찾은 이름은 적지 않습니다(다음 등록이 세대를 올리기 전이라도 매번 다시 찾는다). */
            static const TypeInfo* findComponentTypeCached( hashed_string typeName )
            {
                ComponentTypeCache& cache      = s_typeCache;
                const uint32        generation = engine::getSubsystem<TypeRegistry>().getGeneration();
                if ( cache._generation != generation )
                {
                    for ( ComponentTypeCache::Slot& slot : cache._arrSlot )
                    {
                        slot = ComponentTypeCache::Slot{ 0, nullptr };
                    }
                    cache._generation = generation;
                }
                const uint32              nameIndex = typeName.getIndex();
                ComponentTypeCache::Slot& slot      = cache._arrSlot[nameIndex % ComponentTypeCache::kSlotCount];
                if ( slot._pType != nullptr && slot._nameIndex == nameIndex )
                    return slot._pType;
                const TypeInfo* pType = findComponentType( typeName );
                if ( pType != nullptr )
                    slot = ComponentTypeCache::Slot{ nameIndex, pType };
                return pType;
            }
        };

        /** @brief 같은 파도의 워커 레인 묶음입니다 — 구간마다 시스템의 `runLane` 을 잡 컨텍스트(매니저 읽기 전용 + 틱 뒤 큐)로 부른다. */
        struct FrameSystemLaneJob
        {
            const GameObjectManager*    _pManager;
            StructuralChangeBuffer*     _pCommandBuffer;
            const FrameSystemLaneEntry* _arrLane;
            float32                     _deltaTime;

            void runRange( uint32 start, uint32 end ) const
            {
                SceneJobContext context{ *_pManager, *_pCommandBuffer, _deltaTime };
                for ( uint32 jobIndex = start; jobIndex < end; ++jobIndex )
                {
                    const ScopedFrameProfile systemScope{ _arrLane[jobIndex]._scopeID };
                    _arrLane[jobIndex]._pSystem->runLane( context );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void SceneJobContext::deferPostTick( Callback func )
    {
        // 늘 큐로 간다 — 레인은 워커에서 돌 수 있어, 얼지 않았다고 바로 실행하면(`executeOrDeferPostTick`) 워커가 씬을 바꾼다.
        _pCommandBuffer->deferPostTick( std::move( func ) );
    }

    TickContext::TickContext( GameObject& self, float32 deltaTime )
        : SceneJobContext{ *self.getManager(), self.getManager()->_structuralChangeBuffer, deltaTime }
        , _pSelf{ &self }
    {
    }

    void ISceneLaneFrameSystem::runMain( GameObjectManager& manager, float32 deltaTime )
    {
        SceneJobContext context{ manager, manager._structuralChangeBuffer, deltaTime };
        runLane( context );
    }

    struct GameObjectManager::SceneServiceEntry
    {
        const SceneServiceRegistration* _pRegistration;
        ISceneService*                  _pService;
        ProfileScopeID                  _updateScopeID;   ///< `update` 구간(`GT.Scene.service.<이름>`) — 서비스 이름으로 만들 때 한 번 받는다
        ProfileScopeID                  _completeScopeID; ///< `completeFrameJob` 구간(`GT.Scene.serviceJob.<이름>`)
    };

    /**
     * @brief 매니저가 드는 씬 부품입니다(`GameObjectManager::_pState`). 선언 순서가 생성 순서다 — 틱 스케줄러가 계층 · 프리미티브 레지스트리를,
     *        저장소가 틱 스케줄러를 참조하므로 그 뒤에 선다. 소멸은 역순이라 씬 범위(서비스)가 저장소보다 먼저 사라진다.
     */
    struct GameObjectManager::State
    {
        SceneTransformHierarchy _transformHierarchy;
        PrimitiveRegistry       _primitiveRegistry;
        CameraRegistry          _cameraRegistry;
        TagIndex                _tagIndex;
        SceneTickScheduler      _tickScheduler;
        /** @brief 타입별 컴포넌트 레지스트리입니다(빛 포함). */
        ComponentRegistry _componentRegistry;
        /** @brief 오브젝트 저장소(생성 · 이름 · id 표 · 조회 · 지연 파괴 · 컴포넌트 풀 · 시작 줄)입니다. 게임이 부르는 API 는 매니저의 전달 함수입니다. */
        GameObjectStore _store;
        /** @brief 씬 범위입니다 — 레지스트리(`SceneServiceRegistry`)에서 만든 기능 서비스(물리 · 오디오 · 내비게이션 …)를 소유한다. 부모는 엔진 범위. */
        SubsystemScope _sceneScope;
        /** @brief 씬 서비스를 범위의 초기화 순서로 든 목록입니다. 코어는 타입을 모르고 단계마다 `update` 만 부른다. */
        vector<SceneServiceEntry> _listSceneService;

        State( GameObjectManager& manager, StructuralChangeBuffer& structuralChangeBuffer, ObjectIDAllocator& objectIDAllocator, const SceneEngineServices& services )
            : _transformHierarchy{}
            , _primitiveRegistry{}
            , _cameraRegistry{}
            , _tagIndex{}
            , _tickScheduler{ manager, _transformHierarchy, _primitiveRegistry, structuralChangeBuffer }
            , _componentRegistry{}
            , _store{ manager, _tickScheduler, structuralChangeBuffer, objectIDAllocator }
            , _sceneScope{ services._pEngineSubsystems }
            , _listSceneService{}
        {
        }
    };

    GameObjectManager::GameObjectManager()
        : GameObjectManager( SceneEngineServices::makeFromBoundEngine() )
    {
    }

    GameObjectManager::GameObjectManager( const SceneEngineServices& services )
        : _pOwnedObjectIDAllocator{ services._pObjectIDAllocator == nullptr ? make_unique<ObjectIDAllocator>() : nullptr } // 엔진 범위가 없으면 이 매니저가 번호 공간의 주인이다
        , _pObjectIDAllocator{ services._pObjectIDAllocator != nullptr ? services._pObjectIDAllocator : _pOwnedObjectIDAllocator.get() }
        , _structuralChangeBuffer{}
        , _pState{ make_unique<State>( *this, _structuralChangeBuffer, *_pObjectIDAllocator, services ) }
        , _pFrameCamera{ nullptr }
        , _listFrameSystem{}
        , _frameSystemLanePlan{}
        , _listFrameAccess{}
        , _listLaneJob{}
        , _listRetiredFrameSystem{}
        , _services{ services }
        , _bRunningFrameSystems{ SW_FALSE }
        , _bPhysicsStepEnabled{ SW_TRUE }
        , _updateCount{ 0 }
        , _deferredRegistrationDepth{ 0 }
        , _listDeferredRegister{}
        , _listDeferredPostLoad{}
        , _pumpBatch{}
    {
        // 기능 서비스는 기능 폴더가 정적 초기화에서 올린 줄로 만든다 — 코어는 그 타입을 모른다(`ISceneService.h`). 씬 범위가 소유하고 의존 순서로 세운다.
        const vector<const SceneServiceRegistration*>& listRegistration = SceneServiceRegistry::getAll();
        vector<SceneServiceEntry>                      listCreated;
        listCreated.reserve( listRegistration.size() );
        for ( const SceneServiceRegistration* pRegistration : listRegistration )
        {
            ISceneService& service = pRegistration->_pCreate( *this, _pState->_sceneScope );
            // 구간은 서비스 이름으로 여기서 한 번 받는다 — 서비스마다 손으로 구간을 걸지 않아도 새 서비스가 계측에서 빠지지 않는다.
            const utf8* pName = service.getSubsystemName();
            // 프레임 일이 없는 서비스는 `completeFrameJob` 구간을 받지 않는다(불리지 않는다 — 슬롯 수 `FrameProfiler::kMaxScope` 를 아낀다).
            const ProfileScopeID updateScopeID   = FrameProfiler::registerEngineNamedScope( "GT.Scene.service.", pName, __func__, __FILE__, __LINE__ );
            const ProfileScopeID completeScopeID = pRegistration->hasFrameJob() ? FrameProfiler::registerEngineNamedScope( "GT.Scene.serviceJob.", pName, __func__, __FILE__, __LINE__ ) : ProfileScopeID{};
            listCreated.push_back( SceneServiceEntry{ pRegistration, &service, updateScopeID, completeScopeID } );
        }
        SubsystemCollection& collection = _pState->_sceneScope.getCollection();
        if ( collection.initializeAll( SubsystemHostMask::All ) == false )
            SW_LOG_ERROR_ID( "GameObjectManager.SceneServicesFailedStart", "Scene services failed to start: %#", collection.getError().c_str() );
        // 갱신도 초기화 순서다 — 같은 단계의 서비스가 서로 기대면 의존으로 적는다.
        _pState->_listSceneService.reserve( listCreated.size() );
        for ( const uint32 index : collection.getInitializeOrder() )
        {
            const ISubsystem& subsystem = collection.getSubsystem( index );
            for ( const SceneServiceEntry& entry : listCreated )
            {
                if ( entry._pService == &subsystem )
                    _pState->_listSceneService.push_back( entry );
            }
        }
        validateSceneServiceJobs();
    }

    void GameObjectManager::validateSceneServiceJobs() const
    {
        for ( size_t serviceIndex = 0; serviceIndex < _pState->_listSceneService.size(); ++serviceIndex )
        {
            const SceneServiceRegistration& registration = *_pState->_listSceneService[serviceIndex]._pRegistration;
            if ( registration.hasFrameJob() == false )
                continue;
            const utf8* pName = _pState->_listSceneService[serviceIndex]._pService->getSubsystemName();
            if ( registration._completeStep <= registration._frameStep )
            {
                SW_LOG_ERROR_ID( "GameObjectManager.FrameJobCompleteStep", "Scene service '%#' completes its frame job at step %# which is not after its update step %# - the job would be waited a frame late",
                                 pName, getSceneFrameStepName( registration._completeStep ), getSceneFrameStepName( registration._frameStep ) );
                continue;
            }
            for ( size_t otherIndex = serviceIndex + 1; otherIndex < _pState->_listSceneService.size(); ++otherIndex )
            {
                const SceneServiceRegistration& other = *_pState->_listSceneService[otherIndex]._pRegistration;
                // 두 일의 사이(넘긴 단계 끝 ~ 마치는 단계 앞)가 겹치면 같이 돈다 — 겹치는 접근이면 워커끼리 경합한다.
                const bool bWindowsOverlap = other.hasFrameJob() && other._frameStep < registration._completeStep && registration._frameStep < other._completeStep;
                if ( bWindowsOverlap == false || hasFrameAccessConflict( registration._jobAccess, other._jobAccess ) == false )
                    continue;
                SW_LOG_ERROR_ID( "GameObjectManager.FrameJobAccessConflict", "Scene services '%#' and '%#' run frame jobs at the same time with conflicting scene data access - move one completion step or split the data",
                                 pName, _pState->_listSceneService[otherIndex]._pService->getSubsystemName() );
            }
            // 일이 도는 사이(넘긴 단계의 뒤 서비스 ~ 마치는 단계 앞)에 다른 서비스의 갱신이 겹치는 데이터를 고치거나 일이 쓰는 데이터를 읽으면 경합이다 — 갱신의 접근 선언을 읽는 자리.
            for ( size_t otherIndex = 0; otherIndex < _pState->_listSceneService.size(); ++otherIndex )
            {
                const SceneServiceRegistration& other         = *_pState->_listSceneService[otherIndex]._pRegistration;
                const bool                      bAfterLaunch  = other._frameStep == registration._frameStep && otherIndex > serviceIndex;
                const bool                      bInsideWindow = other._frameStep > registration._frameStep && other._frameStep < registration._completeStep;
                if ( otherIndex == serviceIndex || ( bAfterLaunch == false && bInsideWindow == false ) || hasFrameAccessConflict( other._updateAccess, registration._jobAccess ) == false )
                    continue;
                SW_LOG_ERROR_ID( "GameObjectManager.ServiceUpdateDuringFrameJob", "Scene service '%#' updates at step %# while the frame job of '%#' runs with conflicting scene data access - move the update or the job's completion step",
                                 _pState->_listSceneService[otherIndex]._pService->getSubsystemName(), getSceneFrameStepName( other._frameStep ), pName );
            }
        }
    }

    GameObjectManager::~GameObjectManager()
    {
        clear();
    }

    GameObject* GameObjectManager::createGameObject()
    {
        return _pState->_store.createGameObject( hashed_string( GameObject::kDefaultName ) );
    }

    GameObject* GameObjectManager::createGameObject( hashed_string name )
    {
        return _pState->_store.createGameObject( name );
    }

    vector<GameObject*> GameObjectManager::getAllGameObjects() const
    {
        vector<GameObject*> listAllGameObject;
        _pState->_store.getAllGameObjects( listAllGameObject );
        return listAllGameObject;
    }

    uint32 GameObjectManager::destroyComponentsOfModule( string_view moduleName )
    {
        if ( _structuralChangeBuffer.isFrozen() == false )
            _structuralChangeBuffer.flushUntilEmpty( *this );
        return _pState->_store.destroyComponentsOfModule( moduleName );
    }

    const GameObjectStore& GameObjectManager::getObjectStore() const
    {
        return _pState->_store;
    }

    GameObject* GameObjectManager::createGameObjectWithID( hashed_string name, uint64 objectID )
    {
        return _pState->_store.createGameObjectWithID( name, objectID );
    }

    void GameObjectManager::notifyNameChanged( GameObject* pObj, hashed_string oldName, hashed_string newName )
    {
        _pState->_store.notifyNameChanged( pObj, oldName, newName );
    }

    GameObject* GameObjectManager::findGameObjectByName( hashed_string name ) const
    {
        return _pState->_store.findGameObjectByName( name );
    }

    GameObject* GameObjectManager::findGameObjectByID( uint64 objectID ) const
    {
        return _pState->_store.findGameObjectByID( objectID );
    }

    uint32 GameObjectManager::getOverflowObjectCount() const
    {
        return _pState->_store.getOverflowObjectCount();
    }

    void GameObjectManager::getAllGameObjects( vector<GameObject*>& outListGameObject ) const
    {
        _pState->_store.getAllGameObjects( outListGameObject );
    }

    uint32 GameObjectManager::getTypedComponentBucketCount() const
    {
        return _pState->_store.getTypedComponentBucketCount();
    }

    uint64 GameObjectManager::getStructureGeneration() const
    {
        return _pState->_store.getStructureGeneration();
    }

    void GameObjectManager::notifyStructureChanged()
    {
        _pState->_store.notifyStructureChanged();
    }

    void GameObjectManager::beginPlay()
    {
        _pState->_store.beginPlay();
    }

    void GameObjectManager::endPlay()
    {
        _pState->_store.endPlay();
    }

    bool GameObjectManager::hasBegunPlay() const
    {
        return _pState->_store.hasBegunPlay();
    }

    ComponentRegistry& GameObjectManager::getComponentRegistry()
    {
        return _pState->_componentRegistry;
    }

    const ComponentRegistry& GameObjectManager::getComponentRegistry() const
    {
        return _pState->_componentRegistry;
    }

    Component* GameObjectManager::resolveComponent( ComponentHandle handle ) const
    {
        return _pState->_store.resolveComponent( handle );
    }

    GameObject* GameObjectManager::resolveGameObject( GameObjectHandle handle ) const
    {
        return _pState->_store.resolveGameObject( handle );
    }

    const SubsystemScope& GameObjectManager::getSceneScope() const
    {
        return _pState->_sceneScope;
    }

    void GameObjectManager::destroyGameObject( GameObject* pObj, bool bDestroyChildren )
    {
        _pState->_store.destroyGameObject( pObj, bDestroyChildren );
    }

    void GameObjectManager::destroyComponent( Component* pComp )
    {
        _pState->_store.destroyComponent( pComp );
    }

    void GameObjectManager::destroyComponentInstance( Component* pComp )
    {
        _pState->_store.destroyComponentInstance( pComp );
    }

    void GameObjectManager::processDeferredDestruction()
    {
        _pState->_store.processDeferredDestruction();
    }

    void GameObjectManager::mergePendingAdds()
    {
        _pState->_store.mergePendingAdds();
    }

    void GameObjectManager::dispatchPendingBeginPlay()
    {
        _pState->_store.dispatchPendingBeginPlay();
    }

    void GameObjectManager::queueBeginPlay( ComponentHandle handle )
    {
        _pState->_store.queueBeginPlay( handle );
    }

    ComponentPoolEntry* GameObjectManager::getOrCreateComponentPoolEntry( const TypeInfo* pTypeInfo, size_t typeSize )
    {
        return _pState->_store.getOrCreateComponentPoolEntry( pTypeInfo, typeSize );
    }

    void GameObjectManager::registerTypedComponent( Component* pComp )
    {
        _pState->_store.registerTypedComponent( pComp );
    }

    TagIndex& GameObjectManager::getTagIndex()
    {
        return _pState->_tagIndex;
    }

    const TagIndex& GameObjectManager::getTagIndex() const
    {
        return _pState->_tagIndex;
    }

    SceneTransformHierarchy& GameObjectManager::getTransformHierarchy()
    {
        return _pState->_transformHierarchy;
    }

    const SceneTransformHierarchy& GameObjectManager::getTransformHierarchy() const
    {
        return _pState->_transformHierarchy;
    }

    void GameObjectManager::flushSceneTransforms()
    {
        _pState->_transformHierarchy.flush();
    }

    bool GameObjectManager::hasDirtySceneTransforms() const
    {
        return _pState->_transformHierarchy.hasDirty();
    }

    PrimitiveRegistry& GameObjectManager::getPrimitiveRegistry()
    {
        return _pState->_primitiveRegistry;
    }

    const PrimitiveRegistry& GameObjectManager::getPrimitiveRegistry() const
    {
        return _pState->_primitiveRegistry;
    }

    CameraRegistry& GameObjectManager::getCameraRegistry()
    {
        return _pState->_cameraRegistry;
    }

    const CameraRegistry& GameObjectManager::getCameraRegistry() const
    {
        return _pState->_cameraRegistry;
    }

    TickRegistry& GameObjectManager::getTickRegistry()
    {
        return _pState->_tickScheduler.getTickRegistry();
    }

    const TickRegistry& GameObjectManager::getTickRegistry() const
    {
        return _pState->_tickScheduler.getTickRegistry();
    }

    void GameObjectManager::markTickStagesDirty()
    {
        _pState->_tickScheduler.getTickRegistry().markAllDirty();
    }

    uint32 GameObjectManager::getStageTransformApplyCount() const
    {
        return _pState->_tickScheduler.getStageTransformApplyCount();
    }

    void GameObjectManager::notifyTransformDirty()
    {
        _pState->_transformHierarchy.notifyDirty();
    }

    uint64 GameObjectManager::getTransformGeneration() const
    {
        return _pState->_transformHierarchy.getGeneration();
    }

    void GameObjectManager::clear()
    {
        // 프레임 시스템이 먼저다 — 지울 컴포넌트를 가리킬 수 있다. 컴포넌트의 해제는 시스템이 없으면 할 일이 없다.
        _listFrameSystem.clear();
        _listRetiredFrameSystem.clear();
        // 저장소가 오브젝트를 지우기 전에 미룬 일 · 루트 목록을 버린다 — 지울 오브젝트를 가리킨다.
        _structuralChangeBuffer.clear();
        _pState->_transformHierarchy.clear();
        _pState->_store.clear();
        _pState->_tickScheduler.getTickRegistry().clear();
        markTickStagesDirty();
        // 컴포넌트가 바디 · 등록을 놓았다 — 서비스가 남은 것을 버린다(물리는 빈 씬과 쌓인 시간을 버리고 다음 씬은 처음 쓸 때 새로 만든다).
        for ( SceneServiceEntry& entry : _pState->_listSceneService )
        {
            entry._pService->onSceneCleared();
        }
    }

    void GameObjectManager::notifySceneLoading( const string_view sourcePath )
    {
        for ( SceneServiceEntry& entry : _pState->_listSceneService )
        {
            entry._pService->onSceneLoading( sourcePath );
        }
    }

    void GameObjectManager::applyPreviewPolicy( const ScenePreviewPolicy& policy )
    {
        for ( SceneServiceEntry& entry : _pState->_listSceneService )
        {
            entry._pService->applyPreviewPolicy( policy );
        }
    }

    void GameObjectManager::drawServiceDebug( DebugDrawQueue& queue )
    {
        for ( SceneServiceEntry& entry : _pState->_listSceneService )
        {
            entry._pService->drawDebug( queue );
        }
    }

    void GameObjectManager::collectServiceLights( const SceneLightPhase phase, vector<GPULight>& inoutList ) const
    {
        for ( const SceneServiceEntry& entry : _pState->_listSceneService )
        {
            entry._pService->contributeLights( phase, inoutList );
        }
    }

    void GameObjectManager::collectServiceParticles( ISceneParticleSink& sink ) const
    {
        for ( const SceneServiceEntry& entry : _pState->_listSceneService )
        {
            entry._pService->contributeParticles( sink );
        }
    }

    bool GameObjectManager::findServiceSky( SkyGradient& outGradient ) const
    {
        for ( const SceneServiceEntry& entry : _pState->_listSceneService )
        {
            if ( entry._pService->contributeSky( outGradient ) )
                return true;
        }
        return false;
    }

    bool GameObjectManager::findServiceFog( const float3& viewPosition, FogSettings& outSettings ) const
    {
        for ( const SceneServiceEntry& entry : _pState->_listSceneService )
        {
            if ( entry._pService->contributeFog( viewPosition, outSettings ) )
                return true;
        }
        return false;
    }

    bool GameObjectManager::findServiceSurfaceDeform( SurfaceDeformBinding& outBinding ) const
    {
        for ( const SceneServiceEntry& entry : _pState->_listSceneService )
        {
            if ( entry._pService->contributeSurfaceDeform( outBinding ) )
                return true;
        }
        return false;
    }

    ISceneFrameSystem* GameObjectManager::findFrameSystem( const hashed_string& key ) const
    {
        for ( const FrameSystemEntry& entry : _listFrameSystem )
        {
            if ( entry._pSystem != nullptr && entry._key == key )
                return entry._pSystem.get();
        }
        return nullptr;
    }

    bool GameObjectManager::addFrameSystem( const hashed_string& key, unique_ptr<ISceneFrameSystem> pSystem )
    {
        if ( pSystem == nullptr || findFrameSystem( key ) != nullptr )
            return false;
        // 붙인 순서가 실행 순서다 — 이미 붙은 시스템이 이번 프레임의 값을 보아야 하는 종류를 새 시스템이 쓰면, 쓰는 쪽이 읽는 쪽 뒤에 서게 되므로 붙이지 않는다.
        // 워커에서 돌 시스템은 오브젝트 · 컴포넌트를 만들거나 지울 수 없다(생성 · 파괴는 게임 스레드 전용이다) — 구조를 쓰는 시스템이 레인 안전을 선언하면 모순이라 붙이지 않는다.
        const SceneFrameAccess newAccess = pSystem->getFrameAccess();
        if ( pSystem->findLaneSystem() != nullptr && newAccess._writes.contains( SceneDataKind::Structure ) )
        {
            SW_LOG_ERROR_ID( "FrameSystem.LaneStructure", "Frame system '%#' is rejected: it declares itself lane-safe (worker thread) but writes Structure", key.c_str() );
            return false;
        }
        const SceneDataKindSet newWrites = pSystem->getFrameAccess()._writes;
        for ( const FrameSystemEntry& entry : _listFrameSystem )
        {
            if ( entry._pSystem == nullptr )
                continue;
            const SceneDataKind conflictKind = entry._pSystem->getFrameAccess()._reads.intersect( newWrites ).getFirstKind();
            if ( conflictKind == SceneDataKind::Count )
                continue;
            SW_LOG_ERROR_ID( "FrameSystem.AccessOrder", "Frame system '%#' is rejected: it writes %# but the attached system '%#' reads it and would run first (attach the writer before the reader)",
                             key.c_str(), getSceneDataKindName( conflictKind ), entry._key.c_str() );
            return false;
        }
        _listFrameSystem.push_back( FrameSystemEntry{ key, std::move( pSystem ), FrameProfiler::registerEngineNamedScope( "GT.Scene.frameSystem.", key.c_str(), __func__, __FILE__, __LINE__ ) } );
        return true;
    }

    void GameObjectManager::removeFrameSystem( const hashed_string& key )
    {
        for ( auto it = _listFrameSystem.begin(); it != _listFrameSystem.end(); ++it )
        {
            if ( it->_pSystem == nullptr || it->_key != key )
                continue;
            // 도는 중이면 객체는 단계가 끝날 때까지 살려 두고 빈 칸만 남긴다(지금 도는 시스템일 수 있고, 지우면 뒤 칸의 자리가 당겨진다).
            if ( _bRunningFrameSystems )
            {
                _listRetiredFrameSystem.push_back( std::move( it->_pSystem ) );
                it->_key = hashed_string{};
            }
            else
            {
                _listFrameSystem.erase( it );
            }
            return;
        }
    }

    Component* GameObjectManager::addComponentByName( GameObject* pGameObject, hashed_string typeName, bool bLogWarning )
    {
        if ( pGameObject == nullptr )
            return nullptr;
        if ( engine::areEngineServicesBound() == false )
        {
            if ( bLogWarning )
                SW_LOG_WARNING_ID( "GameObjectManager.CannotCreateComponentName", "Cannot create component '%#' by name - engine services (TypeRegistry) are not bound", typeName.c_str() );
            return nullptr;
        }

        const TypeInfo* pType = GameObjectManagerInternal::findComponentTypeCached( typeName );
        if ( pType == nullptr )
        {
            if ( bLogWarning )
                SW_LOG_WARNING_ID( "GameObjectManager.ComponentTypeNotRegistered", "Component type '%#' is not registered (no REFLECT, its module is not loaded, or a typo)", typeName.c_str() );
            return nullptr;
        }
        if ( pType->_addComponent == nullptr )
        {
            if ( bLogWarning )
                SW_LOG_WARNING_ID( "GameObjectManager.TypeCannotCreatedComponent", "Type '%#' cannot be created as a component (abstract, not a component, or its module was unloaded)", typeName.c_str() );
            return nullptr;
        }
        return static_cast<Component*>( pType->_addComponent( pGameObject ) );
    }

    vector<hashed_string> GameObjectManager::getRegisteredComponentTypeNames()
    {
        vector<hashed_string> listName;
        if ( engine::areEngineServicesBound() == false )
            return listName;
        engine::getSubsystem<TypeRegistry>().forEachType( [&listName]( const TypeInfo& info )
        {
            if ( info._addComponent != nullptr )
                listName.push_back( info._name );
        } );
        return listName;
    }

    bool GameObjectManager::isComponentOfType( const Component& component, const TypeInfo* pToType )
    {
        return isTypeDerivedFrom( component.findCachedTypeInfo(), pToType );
    }

    TypedHandleResult GameObjectManager::resolveTypedHandleResult( ComponentHandle handle, const TypeInfo* pToType ) const
    {
        if ( handle.isValid() == false )
            return TypedHandleResult::Success;
        const utf8*      pExpected  = pToType != nullptr ? pToType->_fullyQualifiedName.c_str() : "(no type)";
        const Component* pComponent = _pState->_store.resolveComponent( handle );
        if ( pComponent == nullptr )
        {
            SW_LOG_ERROR_ID( "GameObjectManager.TypedHandleCouldNot", "Typed handle to '%#' could not be made: component %#:%# does not exist (destroyed or pending destroy)", pExpected, handle.objectID(),
                             handle.componentID() );
            return TypedHandleResult::TargetMissing;
        }
        if ( isComponentOfType( *pComponent, pToType ) == false )
        {
            const TypeInfo* pActual = pComponent->findCachedTypeInfo();
            SW_LOG_ERROR_ID( "GameObjectManager.TypedHandleCouldNot2", "Typed handle to '%#' could not be made: component %#:%# is a '%#'", pExpected, handle.objectID(), handle.componentID(),
                             pActual != nullptr ? pActual->_fullyQualifiedName.c_str() : "(type unloaded)" );
            return TypedHandleResult::TypeMismatch;
        }
        return TypedHandleResult::Success;
    }

    void GameObjectManager::reportTypedResolveMismatch( ComponentHandle handle, const Component& found, const TypeInfo* pToType ) const
    {
        // 핸들이 가리키던 id 를 다른 타입이 받았다. 타입 핸들의 전제(한 핸들의 대상 타입은 바뀌지 않는다)가 깨졌으므로 결함이다.
        const TypeInfo* pActual = found.findCachedTypeInfo();
        SW_LOG_ERROR_ID( "GameObjectManager.TypedHandleExpectedBut", "Typed handle %#:%# expected '%#' but resolved to a '%#' - returning nothing", handle.objectID(), handle.componentID(),
                         pToType != nullptr ? pToType->_fullyQualifiedName.c_str() : "(no type)", pActual != nullptr ? pActual->_fullyQualifiedName.c_str() : "(type unloaded)" );
        SW_ASSERT( false && "typed component handle resolved to another type" );
    }

    void GameObjectManager::update( float32 deltaTime )
    {
        ++_updateCount;
        // 순서는 표 하나가 갖는다(`SceneFrameStepList.xxx`).
        // 단계마다 본문 뒤에 그 단계에 등록한 씬 서비스를 부른다 — 본문이 부르게 두면 부르지 않는 단계에 등록한 서비스가 조용히 돌지 않는다.
        // 본문 앞에는 이 단계에서 마치기로 한 서비스의 프레임 일을 마친다(워커 일 결과를 쓴다) — 본문(트랜스폼 펼치기 등)이 그 결과를 본다.
#define SW_SCENE_FRAME_STEP( Name )                   \
    completeSceneServiceJobs( SceneFrameStep::Name ); \
    runFrameStep##Name( deltaTime );                  \
    updateSceneServices( SceneFrameStep::Name, deltaTime );
#include "Engine/Object/GameObject/Tick/SceneFrameStepList.xxx"
#undef SW_SCENE_FRAME_STEP
    }

    void GameObjectManager::runFrameStepMainThreadTasks( float32 /*deltaTime*/ )
    {
        if ( engine::areEngineServicesBound() )
            engine::getSubsystem<TaskManager>().dispatchMainThreadTasks();
    }

    void GameObjectManager::runFrameStepDestroy( float32 /*deltaTime*/ )
    {
        SW_PROFILE_SCOPE( "GT.Scene.tick.destroy" );
        processDeferredDestruction();
        _pState->_tagIndex.releaseRetiredSnapshots(); // 틱이 없는 자리 — 옛 태그 스냅샷을 읽는 질의가 없다
    }

    void GameObjectManager::runFrameStepMerge( float32 /*deltaTime*/ )
    {
        SW_PROFILE_SCOPE( "GT.Scene.tick.merge" );
        mergePendingAdds();
    }

    void GameObjectManager::runFrameStepBeginPlay( float32 /*deltaTime*/ )
    {
        SW_PROFILE_SCOPE( "GT.Scene.tick.beginPlay" );
        dispatchPendingBeginPlay();
    }

    void GameObjectManager::runFrameStepAssetArrival( float32 deltaTime )
    {
        if ( _pState->_primitiveRegistry.countAssetWaiters() == 0 )
            return;
        SW_PROFILE_SCOPE( "GT.Scene.tick.assetArrival" );
        _pState->_primitiveRegistry.updateAssetWaiters( deltaTime );
    }

    void GameObjectManager::runFrameStepFrameSystems( float32 deltaTime )
    {
        if ( _listFrameSystem.empty() )
            return;
        SW_PROFILE_SCOPE( "GT.Scene.tick.frameSystems" );
        // 시스템이 시스템을 붙이거나 뗄 수 있다 — 붙인 것은 다음 프레임부터(개수를 먼저 센다), 뗀 것은 빈 칸으로 남았다가 끝에 걷힌다.
        _bRunningFrameSystems = SW_TRUE;
        const size_t count    = _listFrameSystem.size();
        // 파도 계획 — 붙인 순서를 지키되 접근이 겹치지 않는 시스템끼리 한 파도에 묶는다(`FrameSystemLanePlan`). 뗀 칸은 빈 접근이라 아무와도 겹치지 않는다.
        // 언리얼 Task Graph 의 선행 있는 태스크, 유니티 Job System 의 JobHandle 의존과 같다. 파도는 차례로 돌고, 같은 파도의 레인 시스템(`ISceneLaneFrameSystem`)은 `TaskManager` 가 병렬로 돌린다.
        _listFrameAccess.clear();
        for ( size_t systemIndex = 0; systemIndex < count; ++systemIndex )
        {
            const ISceneFrameSystem* pSystem = _listFrameSystem[systemIndex]._pSystem.get();
            _listFrameAccess.push_back( pSystem != nullptr ? pSystem->getFrameAccess() : SceneFrameAccess{} );
        }
        buildFrameSystemLanePlan( span<const SceneFrameAccess>( _listFrameAccess.data(), _listFrameAccess.size() ), _frameSystemLanePlan );
        for ( const FrameSystemWave& wave : _frameSystemLanePlan._listWave )
        {
            _listLaneJob.clear();
            for ( const uint32 systemIndex : wave._listSystem )
            {
                ISceneFrameSystem* pSystem = _listFrameSystem[systemIndex]._pSystem.get();
                if ( pSystem == nullptr )
                    continue;
                // 워커로 보내지 않는 것은 이 스레드가 먼저 돈다 — 같은 파도의 다른 시스템과 접근이 겹치지 않으므로 순서는 결과에 영향이 없다.
                const ProfileScopeID   scopeID     = _listFrameSystem[systemIndex]._scopeID;
                ISceneLaneFrameSystem* pLaneSystem = pSystem->findLaneSystem();
                if ( pLaneSystem != nullptr )
                {
                    _listLaneJob.push_back( FrameSystemLaneEntry{ pLaneSystem, scopeID } );
                }
                else
                {
                    const ScopedFrameProfile systemScope{ scopeID };
                    pSystem->runMain( *this, deltaTime );
                }
            }
            if ( _listLaneJob.empty() )
                continue;
            // 레인 하나뿐이면 나눌 일이 없다(`serialThreshold` 2). 본문은 시스템 포인터만 읽는다 — 이 목록은 파도가 끝날 때까지 바뀌지 않는다.
            // 워커는 트랜스폼 캐시를 읽기만 해야 한다 — 더티 노드를 읽으면 `SceneComponent::ensureWorldCache` 가 계층을 합성해 쓰므로, 트랜스폼을 읽는 레인 시스템이
            // 둘 이상 같이 돌기 전에 게임 스레드가 한 번 펼쳐 둔다(이 파도의 다른 시스템은 선언상 트랜스폼을 쓰지 않는다).
            if ( _listLaneJob.size() >= 2 && hasDirtySceneTransforms() )
            {
                for ( const uint32 systemIndex : wave._listSystem )
                {
                    const SceneFrameAccess& laneAccess = _listFrameAccess[systemIndex];
                    const bool              bLane      = _listFrameSystem[systemIndex]._pSystem != nullptr && _listFrameSystem[systemIndex]._pSystem->findLaneSystem() != nullptr;
                    if ( bLane && ( laneAccess._reads.contains( SceneDataKind::Transform ) || laneAccess._readsPrevious.contains( SceneDataKind::Transform ) ) )
                    {
                        flushSceneTransforms();
                        break;
                    }
                }
            }
            FrameSystemLaneJob job{ this, &_structuralChangeBuffer, _listLaneJob.data(), deltaTime };
            engine::runParallel( static_cast<uint32>( _listLaneJob.size() ), 2, SW_DELEGATE_METHOD( ParallelBlockDelegate, &FrameSystemLaneJob::runRange, &job ) );
        }
        _bRunningFrameSystems = SW_FALSE;
        if ( _listRetiredFrameSystem.empty() )
            return;
        size_t keptCount = 0;
        for ( size_t systemIndex = 0; systemIndex < _listFrameSystem.size(); ++systemIndex )
        {
            if ( _listFrameSystem[systemIndex]._pSystem == nullptr )
                continue;
            if ( keptCount != systemIndex )
                _listFrameSystem[keptCount] = std::move( _listFrameSystem[systemIndex] );
            ++keptCount;
        }
        _listFrameSystem.resize( keptCount );
        _listRetiredFrameSystem.clear();
    }

    void GameObjectManager::runFrameStepTickPrePhysics( float32 deltaTime )
    {
        // 오브젝트가 없으면 컴포넌트 틱만 건너뛴다(적용 · 병합 · 파괴는 늘 돈다).
        if ( _pState->_store.hasMergedObjects() )
            _pState->_tickScheduler.tickPhase( deltaTime, 0, static_cast<uint32>( TickGroup::PostPhysics ) );
    }

    void GameObjectManager::runFrameStepApplyPrePhysics( float32 /*deltaTime*/ )
    {
        _structuralChangeBuffer.flush( *this );
    }

    void GameObjectManager::runFrameStepNavigation( float32 /*deltaTime*/ )
    {
        // 내비게이션(`SceneNavigation`)은 이 단계에 등록된 씬 서비스다(`update` 가 단계 끝에 부른다). 끝난 타일 재베이크 · 비동기 경로 결과를 끼우고, 군중 진행을 워커에 넘긴다.
        // 군중 결과(자리 · 컨트롤러 속도)는 `FlushPostAnimation` 본문 앞(`completeFrameJob`)에서 쓴다 — 그 사이 애니메이션이 군중과 겹쳐 돈다.
    }

    void GameObjectManager::completeSceneServiceJobs( SceneFrameStep step )
    {
        for ( SceneServiceEntry& entry : _pState->_listSceneService )
        {
            if ( entry._pRegistration->_completeStep != step )
                continue;
            const ScopedFrameProfile serviceScope{ entry._completeScopeID };
            entry._pService->completeFrameJob();
        }
    }

    void GameObjectManager::updateSceneServices( SceneFrameStep step, float32 deltaTime )
    {
        for ( SceneServiceEntry& entry : _pState->_listSceneService )
        {
            if ( entry._pRegistration->_frameStep != step )
                continue;
            const ScopedFrameProfile serviceScope{ entry._updateScopeID };
            entry._pService->update( deltaTime );
        }
    }

    void GameObjectManager::runFrameStepAnimation( float32 /*deltaTime*/ )
    {
        // 애니메이션 평가(`AnimationSystem`)는 이 단계에 등록된 씬 서비스다. 래그돌의 바디 자세는 물리 뒤에 읽혀 다음 포즈에 섞인다.
    }

    void GameObjectManager::runFrameStepFlushPostAnimation( float32 /*deltaTime*/ )
    {
        if ( hasDirtySceneTransforms() )
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.flushTransformsPost" );
            flushSceneTransforms();
        }
    }

    void GameObjectManager::runFrameStepPhysics( float32 /*deltaTime*/ )
    {
        // 물리 서비스(`ScenePhysics`) — 강체와 2D 콜라이더(센서)가 이 프레임에 적용된 월드 자리로 겹침을 측정한다(고정 스텝 · 보간한 자리를 트랜스폼에 쓰고
        // 접촉 이벤트를 나눠 준다). 단계 끝에 부르고, 끈 씬(`setPhysicsStepEnabled( false )`)은 서비스가 건너뛴다. 이벤트 처리가 지운 것은 마지막 단계가 놓는다.
    }

    void GameObjectManager::runFrameStepFlushPostPhysics( float32 /*deltaTime*/ )
    {
        // 물리가 쓴 자리가 이번 프레임의 그림과 PostPhysics 틱에 들게 바로 펼친다.
        if ( hasDirtySceneTransforms() )
            flushSceneTransforms();
    }

    void GameObjectManager::runFrameStepSpatialIndex( float32 /*deltaTime*/ )
    {
        // 공간 색인(`SceneSpatialGrid`)은 이 단계에 등록된 씬 서비스다. 단계를 따로 둔 것은 같은 단계 안의 서비스 순서(의존)가 아니라 표가 접촉 처리(`ScenePhysicsEvents`) 뒤를 정하게 하려는 것이다.
    }

    void GameObjectManager::runFrameStepTickPostPhysics( float32 deltaTime )
    {
        if ( _pState->_store.hasMergedObjects() )
            _pState->_tickScheduler.tickPhase( deltaTime, static_cast<uint32>( TickGroup::PostPhysics ), TickRegistry::kGroupCount );
    }

    void GameObjectManager::runFrameStepApplyPostPhysics( float32 /*deltaTime*/ )
    {
        _structuralChangeBuffer.flush( *this );
    }

    void GameObjectManager::runFrameStepFlushEnd( float32 /*deltaTime*/ )
    {
        if ( hasDirtySceneTransforms() )
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.flushTransformsEnd" );
            flushSceneTransforms();
        }
    }

    void GameObjectManager::runFrameStepDestroyPost( float32 /*deltaTime*/ )
    {
        SW_PROFILE_SCOPE( "GT.Scene.tick.destroyPost" );
        processDeferredDestruction();
        // 놓은 오브젝트의 기록을 거두는 서비스(`PrefabInstanceRegistry`)가 이 본문 뒤에 돈다 — 파괴를 처리한 뒤라야 무엇이 없어졌는지 안다.
    }

    const utf8* getSceneFrameStepName( SceneFrameStep step )
    {
        static constexpr const utf8* kArrName[] = {
#define SW_SCENE_FRAME_STEP( Name ) #Name,
#include "Engine/Object/GameObject/Tick/SceneFrameStepList.xxx"
#undef SW_SCENE_FRAME_STEP
        };
        const uint32 index = static_cast<uint32>( step );
        return index < static_cast<uint32>( SceneFrameStep::Count ) ? kArrName[index] : "Unknown";
    }

    uint32 GameObjectManager::applyTransformBatch( const SceneTransformWrite* pWrite, uint32 count )
    {
        return _pState->_transformHierarchy.applyBatch( *this, pWrite, count );
    }

    void GameObjectManager::queueTransformWrite( const SceneTransformWrite& write )
    {
        // 순서 키는 이 쓰기를 낸 틱의 주인 오브젝트다 — 여러 오브젝트의 틱이 한 컴포넌트에 쓰면 id 가 큰 쪽이 이긴다. 같은 주인의 건끼리는 디스패치 순번이 선후다.
        const TickThreadState& threadState = StructuralChangeBuffer::getThreadState();
        const uint64           writerID    = ( threadState._pTickWriter != nullptr ) ? threadState._pTickWriter->getObjectID() : 0;
        if ( _pState->_transformHierarchy.queueWriteParallel( write, writerID, threadState._dispatchOrdinal ) )
            return;

        // 이 스레드가 스크래치 슬롯을 받지 못했다(도우미 칸이 다 찬 드문 경우). 계층 변경과 같은 지연 경로로 가서, 틱 뒤에 한 건짜리 배치로 적용한다.
        // 그 건이 단계 끝에 적용되므로, 뒤에 부른 쓰기가 스테이지 경계에서 먼저 적용되어 이 건에 지지 않게 계층 변경으로 센다.
        deferHierarchyChange( [this, write]()
        {
            _pState->_transformHierarchy.applyBatch( *this, &write, 1 );
        } );
    }

    void GameObjectManager::registerRootSceneComponent( SceneComponent* pComp )
    {
        _pState->_transformHierarchy.registerRoot( pComp );
    }

    void GameObjectManager::unregisterRootSceneComponent( SceneComponent* pComp )
    {
        _pState->_transformHierarchy.unregisterRoot( pComp );
    }

    void GameObjectManager::registerComponent( Component& comp )
    {
        if ( _deferredRegistrationDepth != 0 )
            _listDeferredRegister.push_back( comp.getHandle() );
        else
            comp.dispatchRegister( *this );
    }

    void GameObjectManager::postLoadComponent( Component& comp )
    {
        if ( _deferredRegistrationDepth != 0 )
        {
            comp.markPostLoadPending();
            _listDeferredPostLoad.push_back( comp.getHandle() );
        }
        else
            comp.dispatchPostLoad();
    }

    void GameObjectManager::endDeferredRegistration()
    {
        SW_ASSERT( _deferredRegistrationDepth != 0 );
        if ( --_deferredRegistrationDepth != 0 )
            return;
        (void)processQueuedRegistration( 0 );
    }

    bool GameObjectManager::processDeferredRegistration( uint32 maxCount )
    {
        SW_ASSERT( _deferredRegistrationDepth == 1 );
        --_deferredRegistrationDepth; // 훅이 만든 컴포넌트는 미루지 않고 바로 등록한다(한 번에 부를 때와 같다)
        const bool bDone = processQueuedRegistration( maxCount );
        if ( bDone == false )
            ++_deferredRegistrationDepth;
        return bDone;
    }

    bool GameObjectManager::processQueuedRegistration( uint32 maxCount )
    {
        // 묶음을 꺼내 들고 부른다 — 훅이 프리팹을 지으면 이 함수가 다시 불리는데, 그 안쪽 미루기는 제 묶음만 끝내야 한다.
        DeferredRegistrationBatch batch = std::move( _pumpBatch );
        _pumpBatch                      = DeferredRegistrationBatch{};
        batch._listRegister.insert( batch._listRegister.end(), _listDeferredRegister.begin(), _listDeferredRegister.end() );
        batch._listPostLoad.insert( batch._listPostLoad.end(), _listDeferredPostLoad.begin(), _listDeferredPostLoad.end() );
        _listDeferredRegister.clear();
        _listDeferredPostLoad.clear();
        if ( batch.pump( *this, maxCount ) )
            return true;
        _pumpBatch = std::move( batch );
        return false;
    }

    bool GameObjectManager::DeferredRegistrationBatch::pump( GameObjectManager& manager, uint32 maxCount )
    {
        // 등록을 모두 끝낸 뒤 로드 뒤 처리를 부른다 — 바로 붙이던 때와 같이 `onPostLoad` 가 같은 배치의 다른 컴포넌트가 등록된 뒤를 본다.
        uint32 calledCount = 0;
        while ( _registerCursor < _listRegister.size() )
        {
            if ( maxCount != 0 && calledCount == maxCount )
                return false;
            Component* pComp = manager.resolveComponent( _listRegister[_registerCursor++] );
            if ( pComp != nullptr && pComp->isPendingDestroy() == false ) // 받기 전에 파괴가 정해졌다 — 등록하지 않으므로 해체도 해제를 부르지 않는다
                pComp->dispatchRegister( manager );
            ++calledCount;
        }
        while ( _postLoadCursor < _listPostLoad.size() )
        {
            if ( maxCount != 0 && calledCount == maxCount )
                return false;
            Component* pComp = manager.resolveComponent( _listPostLoad[_postLoadCursor++] );
            if ( pComp != nullptr && pComp->isPendingDestroy() == false )
                pComp->dispatchPostLoad();
            ++calledCount;
        }
        return true;
    }
} // namespace sw
