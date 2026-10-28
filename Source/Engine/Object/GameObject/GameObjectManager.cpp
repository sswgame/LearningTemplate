/**
 * @file GameObjectManager.cpp
 * @brief GameObjectManager 의 소유 · 비우기와 이름으로 컴포넌트 만들기입니다. 저장소는 `GameObjectStore`, 프레임 경로(tick · 트랜스폼 배치)는 `GameObjectManagerTick.cpp` 에 있습니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Core/Foundation/StdHeaders.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
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
} // namespace sw
