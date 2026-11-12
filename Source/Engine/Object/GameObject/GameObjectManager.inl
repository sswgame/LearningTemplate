/**
 * @file GameObjectManager.inl
 * @brief `GameObjectManager` 의 템플릿 몸체(순회 · 타입 핸들 찾기 · 씬 서비스 얻기 · 틱 뒤 컴포넌트 작업)입니다. 이 템플릿을 부르는 파일만 포함합니다.
 * @details 매니저 헤더를 포함하는 TU 는 400 여 개이고 그중 이 템플릿을 부르는 것은 일부다. 몸체를 헤더에 두면 몸체가 보는 형식(저장소 · 레지스트리 · 범위 · GameObject)의
 *          헤더를 모든 TU 가 다시 읽는다(W-S4 허브 해체, 언리얼 `UWorld` 가 `World.h` 에 템플릿 몸체 대신 앞 선언을 두는 자리).
 *          포함을 잊으면 컴파일이 아니라 링크에서 정의 없는 템플릿 심볼로 멈춘다 — 그 이름(`GameObjectManager::forEach…`)이 이 파일을 가리킨다.
 */
#pragma once
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectStore.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

namespace sw
{
    template <typename Func>
    void GameObjectManager::forEachGameObject( SceneIterationKey /*key*/, Func&& func ) const
    {
        getObjectStore().forEachGameObject( std::forward<Func>( func ) );
    }

    template <typename Func>
    void GameObjectManager::forEachComponent( SceneIterationKey key, Func&& func ) const
    {
        forEachGameObject( key, [&func]( GameObject* pObj )
        {
            pObj->forEachComponent( func );
        } );
    }

    template <typename TComponent, typename Func>
    void GameObjectManager::forEachComponentOfType( SceneIterationKey /*key*/, Func&& func ) const
    {
        static_assert( std::is_base_of_v<Component, TComponent>, "TComponent must derive from sw::Component" );
        getObjectStore().forEachComponentOfType( findStaticType<TComponent>(), [&func]( Component* pComp )
        {
            func( static_cast<TComponent*>( pComp ) );
        } );
    }

    template <typename Func>
    void GameObjectManager::forEachComponentOfType( SceneIterationKey /*key*/, const TypeInfo* pToType, Func&& func ) const
    {
        getObjectStore().forEachComponentOfType( pToType, std::forward<Func>( func ) );
    }

    template <typename TComponent, typename TFunc>
    void GameObjectManager::executeOrDeferPostTickOnComponent( const TComponent& self, TFunc func )
    {
        executeOrDeferPostTick( makeComponentPostTickTask( self, func ) );
    }

    template <typename TComponent, typename TFunc>
    void GameObjectManager::deferPostTickOnComponent( const TComponent& self, TFunc func )
    {
        deferPostTick( makeComponentPostTickTask( self, func ) );
    }

    template <typename TComponent, typename TFunc>
    GameObjectManager::PostTickDelegate GameObjectManager::makeComponentPostTickTask( const TComponent& self, TFunc func )
    {
        static_assert( std::is_empty_v<TFunc>, "post-tick component callable must not capture" );
        const TComponentHandle<TComponent> handle{ &self };
        GameObjectManager*                 pManager = this;
        return PostTickDelegate( [pManager, handle, func]()
        {
            TComponent* pComponent = pManager->resolveComponent( handle );
            // 삭제 대기 컴포넌트는 핸들로 아직 찾히지만 일을 받지 않는다(틱 · 이벤트와 같은 규칙).
            if ( pComponent == nullptr || pComponent->isPendingDestroy() )
                return;
            if constexpr ( std::is_invocable_v<const TFunc&, TComponent&, GameObjectManager&> )
                func( *pComponent, *pManager );
            else
                func( *pComponent );
        } );
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

    template <typename T>
    T* GameObjectManager::resolveComponent( TComponentHandle<T> handle ) const
    {
        static_assert( std::is_base_of_v<Component, T>, "T must derive from sw::Component" );
        static_assert( HasOwnReflectBody_v<T> || HasReflectStaticType_v<T>, "T must declare its own REFLECT_BODY() (cannot slice to a base class StaticType)" );
        Component* pComponent = resolveComponent( handle.untyped() );
#if !defined( SW_SHIPPING )
        if ( pComponent != nullptr && isComponentOfType( *pComponent, findStaticType<T>() ) == false )
        {
            reportTypedResolveMismatch( handle.untyped(), *pComponent, findStaticType<T>() );
            return nullptr;
        }
#endif
        return static_cast<T*>( pComponent );
    }

    template <typename T>
    TypedHandleResult GameObjectManager::makeTypedHandle( ComponentHandle handle, TComponentHandle<T>& outHandle ) const
    {
        static_assert( std::is_base_of_v<Component, T>, "T must derive from sw::Component" );
        static_assert( HasOwnReflectBody_v<T> || HasReflectStaticType_v<T>, "T must declare its own REFLECT_BODY() (cannot slice to a base class StaticType)" );
        const TypedHandleResult result = resolveTypedHandleResult( handle, findStaticType<T>() );
        if ( result == TypedHandleResult::Success )
            outHandle = TComponentHandle<T>( ComponentHandleCastKey{}, handle );
        return result;
    }

    template <typename TService>
    TService& GameObjectManager::getSubsystem()
    {
        return getSceneScope().get<TService>();
    }

    template <typename TService>
    const TService& GameObjectManager::getSubsystem() const
    {
        return getSceneScope().get<TService>();
    }
} // namespace sw
