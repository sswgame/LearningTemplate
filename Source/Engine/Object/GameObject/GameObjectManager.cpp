/**
 * @file GameObjectManager.cpp
 * @brief GameObjectManager 의 소유 · 비우기, 이름으로 컴포넌트 만들기, 프레임 경로(tick 의 단계 · 물리 · 트랜스폼 전달)입니다. 저장소는 `GameObjectStore`, 디스패치는 `SceneTickScheduler` 입니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Core/Foundation/StdHeaders.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Object/GameObject/Tick/ISceneFrameSystem.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/TypeRegistry.h"

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

        /** @brief 같은 파도의 워커 레인 묶음입니다 — 구간마다 시스템의 `runBeforeTick` 을 부른다. */
        struct FrameSystemLaneJob
        {
            GameObjectManager*  _pManager;
            ISceneFrameSystem** _arrSystem;
            float32             _deltaTime;

            void runRange( uint32 start, uint32 end ) const
            {
                for ( uint32 jobIndex = start; jobIndex < end; ++jobIndex )
                {
                    _arrSystem[jobIndex]->runBeforeTick( *_pManager, _deltaTime );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "GameObjectManager" );

    GameObjectManager::GameObjectManager()
        : GameObjectManager( SceneEngineServices::makeFromBoundEngine() )
    {
    }

    GameObjectManager::GameObjectManager( const SceneEngineServices& services )
        : _structuralChangeBuffer{}
        , _transformHierarchy{}
        , _primitiveRegistry{}
        , _componentRegistry{}
        , _cameraRegistry{}
        , _tagIndex{}
        , _tickScheduler{ *this, _transformHierarchy, _primitiveRegistry, _structuralChangeBuffer }
        , _store{ *this, _tickScheduler, _structuralChangeBuffer }
        , _sceneScope{ services._pEngineSubsystems }
        , _listSceneService{}
        , _pFrameCamera{ nullptr }
        , _frameStepObserver{}
        , _listFrameSystem{}
        , _frameSystemLanePlan{}
        , _listFrameAccess{}
        , _listLaneJob{}
        , _listRetiredFrameSystem{}
        , _services{ services }
        , _bRunningFrameSystems{ false }
        , _updateSerial{ 0 }
        , _bPhysicsStepEnabled{ true }
    {
        // 기능 서비스는 기능 폴더가 정적 초기화에서 올린 줄로 만든다 — 코어는 그 타입을 모른다(`ISceneService.h`). 씬 범위가 소유하고 의존 순서로 세운다.
        const vector<const SceneServiceRegistration*>& listRegistration = SceneServiceRegistry::getAll();
        vector<SceneServiceEntry>                      listCreated;
        listCreated.reserve( listRegistration.size() );
        for ( const SceneServiceRegistration* pRegistration : listRegistration )
        {
            listCreated.push_back( SceneServiceEntry{ pRegistration, &pRegistration->_pCreate( *this, _sceneScope ) } );
        }
        SubsystemCollection& collection = _sceneScope.getCollection();
        if ( collection.initializeAll( SubsystemHostMask::All ) == false )
            SW_LOG_ERROR( "Scene services failed to start: %#", collection.getError().c_str() );
        // 갱신도 초기화 순서다 — 같은 단계의 서비스가 서로 기대면 의존으로 적는다.
        _listSceneService.reserve( listCreated.size() );
        for ( const uint32 index : collection.getInitializeOrder() )
        {
            const ISubsystem& subsystem = collection.getSubsystem( index );
            for ( const SceneServiceEntry& entry : listCreated )
            {
                if ( entry._pService == &subsystem )
                    _listSceneService.push_back( entry );
            }
        }
    }

    GameObjectManager::~GameObjectManager()
    {
        clear();
    }

    void GameObjectManager::clear()
    {
        // 프레임 시스템이 먼저다 — 지울 컴포넌트를 가리킬 수 있다. 컴포넌트의 해제는 시스템이 없으면 할 일이 없다.
        _listFrameSystem.clear();
        _listRetiredFrameSystem.clear();
        // 저장소가 오브젝트를 지우기 전에 미룬 일 · 루트 목록을 버린다 — 지울 오브젝트를 가리킨다.
        _structuralChangeBuffer.clear();
        _transformHierarchy.clear();
        _store.clear();
        _tickScheduler.getTickRegistry().clear();
        markTickStagesDirty();
        // 컴포넌트가 바디 · 등록을 놓았다 — 서비스가 남은 것을 버린다(물리는 빈 씬과 쌓인 시간을 버리고 다음 씬은 처음 쓸 때 새로 만든다).
        for ( SceneServiceEntry& entry : _listSceneService )
        {
            entry._pService->onSceneCleared();
        }
    }

    void GameObjectManager::notifySceneLoading( const string_view sourcePath )
    {
        for ( SceneServiceEntry& entry : _listSceneService )
        {
            entry._pService->onSceneLoading( sourcePath );
        }
    }

    void GameObjectManager::applyPreviewPolicy( const ScenePreviewPolicy& policy )
    {
        for ( SceneServiceEntry& entry : _listSceneService )
        {
            entry._pService->applyPreviewPolicy( policy );
        }
    }

    void GameObjectManager::drawServiceDebug( DebugDrawQueue& queue )
    {
        for ( SceneServiceEntry& entry : _listSceneService )
        {
            entry._pService->drawDebug( queue );
        }
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
        if ( newAccess._bLaneSafe && newAccess._writes.contains( SceneDataKind::Structure ) )
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
        _listFrameSystem.push_back( FrameSystemEntry{ key, std::move( pSystem ) } );
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
                SW_LOG_WARNING( "Cannot create component '%#' by name - engine services (TypeRegistry) are not bound", typeName.c_str() );
            return nullptr;
        }

        const TypeInfo* pType = GameObjectManagerInternal::findComponentTypeCached( typeName );
        if ( pType == nullptr )
        {
            if ( bLogWarning )
                SW_LOG_WARNING( "Component type '%#' is not registered (no REFLECT, its module is not loaded, or a typo)", typeName.c_str() );
            return nullptr;
        }
        if ( pType->_addComponent == nullptr )
        {
            if ( bLogWarning )
                SW_LOG_WARNING( "Type '%#' cannot be created as a component (abstract, not a component, or its module was unloaded)", typeName.c_str() );
            return nullptr;
        }
        return pType->_addComponent( pGameObject );
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

    TypedHandleResult GameObjectManager::resolveTypedHandleResult( ComponentHandle handle, const TypeInfo* pToType ) const
    {
        if ( handle.isValid() == false )
            return TypedHandleResult::Success;
        const utf8*      pExpected  = pToType != nullptr ? pToType->_fullyQualifiedName.c_str() : "(no type)";
        const Component* pComponent = _store.resolveComponent( handle );
        if ( pComponent == nullptr )
        {
            SW_LOG_ERROR( "Typed handle to '%#' could not be made: component %#:%# does not exist (destroyed or pending destroy)", pExpected, handle.objectID(),
                          handle.componentID() );
            return TypedHandleResult::TargetMissing;
        }
        if ( isComponentOfType( *pComponent, pToType ) == false )
        {
            const TypeInfo* pActual = pComponent->findCachedTypeInfo();
            SW_LOG_ERROR( "Typed handle to '%#' could not be made: component %#:%# is a '%#'", pExpected, handle.objectID(), handle.componentID(),
                          pActual != nullptr ? pActual->_fullyQualifiedName.c_str() : "(type unloaded)" );
            return TypedHandleResult::TypeMismatch;
        }
        return TypedHandleResult::Success;
    }

    void GameObjectManager::reportTypedResolveMismatch( ComponentHandle handle, const Component& found, const TypeInfo* pToType ) const
    {
        // 핸들이 가리키던 id 를 다른 타입이 받았다. 타입 핸들의 전제(한 핸들의 대상 타입은 바뀌지 않는다)가 깨졌으므로 결함이다.
        const TypeInfo* pActual = found.findCachedTypeInfo();
        SW_LOG_ERROR( "Typed handle %#:%# expected '%#' but resolved to a '%#' - returning nothing", handle.objectID(), handle.componentID(),
                      pToType != nullptr ? pToType->_fullyQualifiedName.c_str() : "(no type)", pActual != nullptr ? pActual->_fullyQualifiedName.c_str() : "(type unloaded)" );
        SW_ASSERT( false && "typed component handle resolved to another type" );
    }

    void GameObjectManager::update( float32 deltaTime )
    {
        ++_updateSerial;
        // 순서는 표 하나가 갖는다(`SceneFrameStepList.xxx`). 관찰자는 보통 비어 있어 단계마다 분기 하나다.
        // 단계마다 본문 뒤에 그 단계에 등록한 씬 서비스를 부른다 — 본문이 부르게 두면 부르지 않는 단계에 등록한 서비스가 조용히 돌지 않는다.
#define SW_SCENE_FRAME_STEP( Name )                       \
    if ( _frameStepObserver.isBound() )                   \
        _frameStepObserver.get()( SceneFrameStep::Name ); \
    runFrameStep##Name( deltaTime );                      \
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
        if ( _primitiveRegistry.countAssetWaiters() == 0 )
            return;
        SW_PROFILE_SCOPE( "GT.Scene.tick.assetArrival" );
        _primitiveRegistry.updateAssetWaiters( deltaTime );
    }

    void GameObjectManager::runFrameStepFrameSystems( float32 deltaTime )
    {
        if ( _listFrameSystem.empty() )
            return;
        SW_PROFILE_SCOPE( "GT.Scene.tick.frameSystems" );
        // 시스템이 시스템을 붙이거나 뗄 수 있다 — 붙인 것은 다음 프레임부터(개수를 먼저 센다), 뗀 것은 빈 칸으로 남았다가 끝에 걷힌다.
        _bRunningFrameSystems = true;
        const size_t count    = _listFrameSystem.size();
        // 파도 계획 — 붙인 순서를 지키되 접근이 겹치지 않는 시스템끼리 한 파도에 묶는다(`FrameSystemLanePlan`). 뗀 칸은 빈 접근이라 아무와도 겹치지 않는다.
        // 언리얼 Task Graph 의 선행 있는 태스크, 유니티 Job System 의 JobHandle 의존과 같다. 파도는 차례로 돌고, 같은 파도의 `_bLaneSafe` 시스템은 `TaskManager` 가 병렬로 돌린다.
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
                if ( _listFrameAccess[systemIndex]._bLaneSafe )
                    _listLaneJob.push_back( pSystem );
                else
                    pSystem->runBeforeTick( *this, deltaTime );
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
                    if ( laneAccess._bLaneSafe && ( laneAccess._reads.contains( SceneDataKind::Transform ) || laneAccess._readsPrevious.contains( SceneDataKind::Transform ) ) )
                    {
                        flushSceneTransforms();
                        break;
                    }
                }
            }
            FrameSystemLaneJob job{ this, _listLaneJob.data(), deltaTime };
            engine::runParallel( static_cast<uint32>( _listLaneJob.size() ), 2, SW_DELEGATE_METHOD( ParallelBlockDelegate, &FrameSystemLaneJob::runRange, &job ) );
        }
        _bRunningFrameSystems = false;
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
        if ( _store.hasMergedObjects() )
            _tickScheduler.tickPhase( deltaTime, 0, static_cast<uint32>( TickGroup::PostPhysics ) );
    }

    void GameObjectManager::runFrameStepApplyPrePhysics( float32 /*deltaTime*/ )
    {
        _structuralChangeBuffer.drain( *this );
    }

    void GameObjectManager::runFrameStepNavigation( float32 /*deltaTime*/ )
    {
        // 군중 진행(`SceneNavigation`)은 이 단계에 등록된 씬 서비스다(`update` 가 단계 끝에 부른다). 끝난 타일 재베이크를 끼우는 것도 거기다 — 컴포넌트 틱이 질의하지 않는 구간이다.
    }

    void GameObjectManager::updateSceneServices( SceneFrameStep step, float32 deltaTime )
    {
        for ( SceneServiceEntry& entry : _listSceneService )
        {
            if ( entry._pRegistration->_frameStep == step )
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

    void GameObjectManager::runFrameStepTickPostPhysics( float32 deltaTime )
    {
        if ( _store.hasMergedObjects() )
            _tickScheduler.tickPhase( deltaTime, static_cast<uint32>( TickGroup::PostPhysics ), TickRegistry::kGroupCount );
    }

    void GameObjectManager::runFrameStepApplyPostPhysics( float32 /*deltaTime*/ )
    {
        _structuralChangeBuffer.drain( *this );
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
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Profiling/FrameProfiler.h"
#undef SW_SCENE_FRAME_STEP
        };
        const uint32 index = static_cast<uint32>( step );
        return index < static_cast<uint32>( SceneFrameStep::Count ) ? kArrName[index] : "Unknown";
    }

    uint32 GameObjectManager::applyTransformBatch( const SceneTransformWrite* pWrite, uint32 count )
    {
        return _transformHierarchy.applyBatch( *this, pWrite, count );
    }

    void GameObjectManager::queueTransformWrite( const SceneTransformWrite& write )
    {
        // 순서 키는 이 쓰기를 낸 틱의 주인 오브젝트다 — 여러 오브젝트의 틱이 한 컴포넌트에 쓰면 id 가 큰 쪽이 이긴다.
        const GameObject* pTickWriter = StructuralChangeBuffer::getTickWriter();
        const uint64      writerID    = ( pTickWriter != nullptr ) ? pTickWriter->getObjectID() : 0;
        if ( _transformHierarchy.queueWriteParallel( write, writerID ) )
            return;

        // 이 스레드가 스크래치 슬롯을 받지 못했다(도우미 칸이 다 찬 드문 경우). 계층 변경과 같은 지연 경로로 가서, 틱 뒤에 한 건짜리 배치로 적용한다.
        // 그 건이 단계 끝에 적용되므로, 뒤에 부른 쓰기가 스테이지 경계에서 먼저 적용되어 이 건에 지지 않게 계층 변경으로 센다.
        deferHierarchyChange( [this, write]()
        {
            _transformHierarchy.applyBatch( *this, &write, 1 );
        } );
    }

    void GameObjectManager::registerRootSceneComponent( SceneComponent* pComp )
    {
        _transformHierarchy.registerRoot( pComp );
    }

    void GameObjectManager::unregisterRootSceneComponent( SceneComponent* pComp )
    {
        _transformHierarchy.unregisterRoot( pComp );
    }
} // namespace sw
