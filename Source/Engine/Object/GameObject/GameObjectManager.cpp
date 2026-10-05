/**
 * @file GameObjectManager.cpp
 * @brief GameObjectManager 의 소유 · 비우기와 이름으로 컴포넌트 만들기입니다. 저장소는 `GameObjectStore`, 프레임 경로(tick · 트랜스폼 배치)는 `GameObjectManagerTick.cpp` 에 있습니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Core/Common/StdHeaders.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionCore.h"
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
                const TypeRegistry& registry = engine::getTypeRegistry();
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
             * @brief 스레드마다 둔 이름 → 타입 조회 캐시입니다. 타입 표의 **캐시**일 뿐 등록부가 아닙니다 — 표의 세대가 바뀌면 통째로 버립니다.
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
                const uint32        generation = engine::getTypeRegistry().getGeneration();
                if ( cache._generation != generation )
                {
                    for ( ComponentTypeCache::Slot& slot : cache._arrSlot )
                        slot = ComponentTypeCache::Slot{ 0, nullptr };
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
        : _structuralChangeBuffer{}
        , _transformHierarchy{}
        , _primitiveRegistry{}
        , _lightRegistry{}
        , _cameraRegistry{}
        , _animationSystem{}
        , _tickScheduler{ *this, _transformHierarchy, _primitiveRegistry, _structuralChangeBuffer }
        , _store{ *this, _tickScheduler, _structuralChangeBuffer }
        , _overlapWorld2D{}
        , _scenePhysics{}
        , _sceneAudio{}
        , _sceneNavigation{}
        , _frameStepObserver{}
    {
        _animationSystem.setObjectManager( this );
        _sceneNavigation.setObjectManager( this );
    }

    GameObjectManager::~GameObjectManager()
    {
        clear();
    }

    void GameObjectManager::clear()
    {
        // 저장소가 오브젝트를 지우기 전에 미룬 일 · 루트 목록을 버린다 — 지울 오브젝트를 가리킨다.
        _structuralChangeBuffer.clear();
        _transformHierarchy.clear();
        _store.clear();
        _tickScheduler.getTickRegistry().clear();
        markTickStagesDirty();
        // 컴포넌트가 바디를 놓았다 — 빈 물리 씬과 쌓인 시간을 버린다(다음 씬은 처음 쓸 때 새로 만든다).
        _scenePhysics.shutdown();
        _sceneNavigation.shutdown();
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
        engine::getTypeRegistry().forEachType( [&listName]( const TypeInfo& info )
        {
            if ( info._addComponent != nullptr )
                listName.push_back( info._name );
        } );
        return listName;
    }
} // namespace sw
