/**
 * @file GameObjectManager.inl
 * @brief `GameObjectManager` 의 순회 템플릿 몸체입니다. 이 템플릿을 부르는 파일만 포함합니다(타입 핸들 찾기 · 틱 뒤 컴포넌트 작업은 `GameObjectManagerResolve.inl` — 둘 다 부르면 둘 다 포함한다).
 * @details 매니저 헤더를 포함하는 TU 는 400 여 개이고 그중 이 템플릿을 부르는 것은 일부다. 몸체를 헤더에 두면 몸체가 보는 형식(저장소 · 레지스트리 · 범위 · GameObject)의
 *          헤더를 모든 TU 가 다시 읽는다(W-S4 허브 해체, 언리얼 `UWorld` 가 `World.h` 에 템플릿 몸체 대신 앞 선언을 두는 자리).
 *          포함을 잊으면 clang 은 컴파일에서(`-Werror=undefined-func-template`, `sw_require_template_definitions`), MSVC 는 링크에서 정의 없는 템플릿 심볼로 멈춘다 — 그 이름(`GameObjectManager::forEach…`)이 이 파일을 가리킨다.
 */
#pragma once
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectStore.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"

namespace sw
{
    template <typename Func>
    void GameObjectManager::forEachGameObject( SceneIterationKey /*key*/, Func&& func )
    {
        getObjectStore().forEachGameObject( std::forward<Func>( func ) );
    }

    template <typename Func>
    void GameObjectManager::forEachGameObject( SceneIterationKey /*key*/, Func&& func ) const
    {
        getObjectStore().forEachGameObject( [&func]( const GameObject* pObj )
        {
            func( pObj );
        } );
    }

    template <typename Func>
    void GameObjectManager::forEachComponent( SceneIterationKey key, Func&& func )
    {
        forEachGameObject( key, [&func]( GameObject* pObj )
        {
            pObj->forEachComponent( func );
        } );
    }

    template <typename Func>
    void GameObjectManager::forEachComponent( SceneIterationKey key, Func&& func ) const
    {
        forEachGameObject( key, [&func]( const GameObject* pObj )
        {
            pObj->forEachComponent( [&func]( const Component* pComp )
            {
                func( pComp );
            } );
        } );
    }

    template <typename TComponent, typename Func>
    void GameObjectManager::forEachComponentOfType( SceneIterationKey /*key*/, Func&& func )
    {
        static_assert( std::is_base_of_v<Component, TComponent>, "TComponent must derive from sw::Component" );
        getObjectStore().forEachComponentOfType( findStaticType<TComponent>(), [&func]( Component* pComp )
        {
            func( static_cast<TComponent*>( pComp ) );
        } );
    }

    template <typename TComponent, typename Func>
    void GameObjectManager::forEachComponentOfType( SceneIterationKey /*key*/, Func&& func ) const
    {
        static_assert( std::is_base_of_v<Component, TComponent>, "TComponent must derive from sw::Component" );
        getObjectStore().forEachComponentOfType( findStaticType<TComponent>(), [&func]( const Component* pComp )
        {
            func( static_cast<const TComponent*>( pComp ) );
        } );
    }

    template <typename Func>
    void GameObjectManager::forEachComponentOfType( SceneIterationKey /*key*/, const TypeInfo* pToType, Func&& func )
    {
        getObjectStore().forEachComponentOfType( pToType, std::forward<Func>( func ) );
    }

    template <typename Func>
    void GameObjectManager::forEachComponentOfType( SceneIterationKey /*key*/, const TypeInfo* pToType, Func&& func ) const
    {
        getObjectStore().forEachComponentOfType( pToType, [&func]( const Component* pComp )
        {
            func( pComp );
        } );
    }

    template <typename T>
    const T* TickContext::readOther( GameObjectHandle object ) const
    {
        const GameObject* pObject = getManager().resolveGameObject( object );
        return pObject != nullptr ? pObject->getTickSafeComponent<T>() : nullptr;
    }

    template <typename T, typename TFunc>
    void GameObjectManager::forEachActiveRegistered( vector<ComponentHandle>& inoutScratch, TFunc&& func )
    {
        inoutScratch.clear();
        const ComponentRegistry::View<T> view = getComponentRegistry().getAll<T>();
        for ( size_t index = 0; index < view.size(); ++index )
        {
            inoutScratch.push_back( view.getHandle( index ) );
        }
        for ( const ComponentHandle handle : inoutScratch )
        {
            Component* pComponent = resolveComponent( handle );
            if ( pComponent == nullptr || pComponent->isPendingDestroy() || pComponent->isActive() == false )
                continue;
            func( *static_cast<T*>( pComponent ) ); // 레지스트리는 정확히 T 로 오른 것만 담는다(`ComponentRegistry::add<T>`)
        }
    }
} // namespace sw
