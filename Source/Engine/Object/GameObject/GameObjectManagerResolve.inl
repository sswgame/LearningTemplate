/**
 * @file GameObjectManagerResolve.inl
 * @brief `GameObjectManager` 의 타입 핸들 찾기(`resolveComponent<T>` · `makeTypedHandle`)와 틱 뒤 컴포넌트 작업(`executeOrDeferPostTickOnComponent` · `deferPostTickOnComponent`)의 템플릿 몸체입니다.
 * @details 이 몸체는 컴포넌트를 앞 선언으로만 보고(`T` 가 실체화될 때 `T` 의 헤더가 `Component` 를 이미 정의한다) 리플렉션 형식 검사(`ReflectionCast.h`)만 쓴다.
 *          순회 몸체(`GameObjectManager.inl`)가 보는 저장소 · 레지스트리 · GameObject 를 읽지 않게 따로 둔다 — 이 템플릿만 부르는 TU 가 .inl 을 포함하던 TU 의 3 분의 1 이다(헤더 팬인 점수로 정했다).
 *          포함을 잊으면 clang 은 컴파일에서(`-Werror=undefined-func-template`), MSVC 는 링크에서 정의 없는 템플릿 심볼로 멈춘다. `GameObjectManager.inl` 은 이것을 포함하지 않는다 —
 *          포함하면 순회만 쓰는 TU 도 이 몸체의 의존을 읽는다(점수로 견줬다).
 */
#pragma once
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    template <typename TComponent, typename TFunc>
    void GameObjectManager::executeOrDeferPostTickOnComponent( const TComponent& self, TFunc func ) const
    {
        _structuralChangeBuffer.executeOrDeferPostTick( makeComponentPostTickTask( self, func ) );
    }

    template <typename TComponent, typename TFunc>
    void GameObjectManager::deferPostTickOnComponent( const TComponent& self, TFunc func ) const
    {
        _structuralChangeBuffer.deferPostTick( makeComponentPostTickTask( self, func ) );
    }

    template <typename TComponent, typename TFunc>
    void TickContext::deferPostTickOnComponent( const TComponent& self, TFunc func )
    {
        _pSceneManager->deferPostTickOnComponent( self, func );
    }

    template <typename TComponent, typename TFunc>
    GameObjectManager::PostTickDelegate GameObjectManager::makeComponentPostTickTask( const TComponent& self, TFunc func ) const
    {
        static_assert( std::is_empty_v<TFunc>, "post-tick component callable must not capture" );
        const TComponentHandle<TComponent> handle{ &self };
        // 명령 버퍼: 쓰기 가능한 매니저는 이 일이 도는 때(틱 밖 · 틱 뒤)에만 @p func 로 나간다 — 거는 쪽(const 매니저 · 틱)은 받지 않는다.
        GameObjectManager* pManager = const_cast<GameObjectManager*>( this );
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

    template <typename T>
    bool GameObjectManager::isResolvedAs( ComponentHandle handle, const Component* pComponent ) const
    {
        static_assert( std::is_base_of_v<Component, T>, "T must derive from sw::Component" );
        static_assert( HasOwnReflectBody_v<T>, "T must declare its own REFLECT_BODY() (cannot slice to a base class StaticType)" );
#if !defined( SW_SHIPPING )
        if ( pComponent != nullptr && isComponentOfType( *pComponent, findStaticType<T>() ) == false )
        {
            reportTypedResolveMismatch( handle, *pComponent, findStaticType<T>() );
            return false;
        }
#else
        (void)handle;
        (void)pComponent;
#endif
        return true;
    }

    template <typename T>
    T* GameObjectManager::resolveComponent( TComponentHandle<T> handle )
    {
        Component* pComponent = resolveComponent( handle.untyped() );
        return isResolvedAs<T>( handle.untyped(), pComponent ) ? static_cast<T*>( pComponent ) : nullptr;
    }

    template <typename T>
    const T* GameObjectManager::resolveComponent( TComponentHandle<T> handle ) const
    {
        const Component* pComponent = resolveComponent( handle.untyped() );
        return isResolvedAs<T>( handle.untyped(), pComponent ) ? static_cast<const T*>( pComponent ) : nullptr;
    }

    template <typename T>
    TypedHandleResult GameObjectManager::makeTypedHandle( ComponentHandle handle, TComponentHandle<T>& outHandle ) const
    {
        static_assert( std::is_base_of_v<Component, T>, "T must derive from sw::Component" );
        static_assert( HasOwnReflectBody_v<T>, "T must declare its own REFLECT_BODY() (cannot slice to a base class StaticType)" );
        const TypedHandleResult result = resolveTypedHandleResult( handle, findStaticType<T>() );
        if ( result == TypedHandleResult::Success )
            outHandle = TComponentHandle<T>( ComponentHandleCastKey{}, handle );
        return result;
    }
} // namespace sw
