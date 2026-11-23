/**
 * @file StatefulComponentRegistrar.h
 * @brief 컴포넌트 타입 하나를 모든 게임의 상태 스냅샷에 올리는 정적 등록입니다. 컴포넌트를 정의한 .cpp 만 포함합니다(`GameInstance` 쪽은 `ComponentStateStore.h` 만 본다).
 */
#pragma once
#include "Core/Registry/RegistrationList.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/ComponentStateStore.h"

namespace sw
{
    /**
     * @class StatefulComponentRegistration
     * @brief 게임 프레임워크의 컴포넌트 타입 하나를 **모든 게임**의 상태 스냅샷에 등록하는 정적 등록입니다. 컴포넌트를 정의한 .cpp 에 하나 둡니다.
     * @details `GameStateSnapshot` 생성자가 등록된 타입을 모두 스냅샷에 넣는다(`registerStatefulComponent` 와 같은 일). 게임마다 한 줄씩 넣으면 빠뜨린 게임만
     *          세이브 · 핫 리로드에서 그 상태를 조용히 잃으므로, 타입을 정의한 쪽이 한 번 등록한다. 층이 위인 컴포넌트(Actor 의 카메라 디렉터)도
     *          기반 층(Foundation)의 게임 인스턴스가 include 하지 않고 올라온다.
     *          등록은 프레임워크 모듈의 정적 초기화에서 돌아 어떤 게임 인스턴스보다 먼저 끝난다. 같은 이름은 두 번 오르지 않는다.
     *          게임 모듈의 타입은 이것을 쓰지 않고 게임 인스턴스 생성자에서 `getStateSnapshot().registerStatefulComponent<T>()` 로 등록한다(모듈 리로드와 수명이 같다).
     */
    class SW_GF_API StatefulComponentRegistration
    {
    public:
        using CaptureFn = void ( * )( SceneIterationKey key, ComponentStateStore& store, GameObjectManager& manager );
        using RestoreFn = void ( * )( SceneIterationKey key, const ComponentStateStore& store, GameObjectManager& manager );
        using DespawnFn = void ( * )( SceneIterationKey key, GameObjectManager& manager );

        /** @param pDespawn 상태 저장 전에 세운 런타임 오브젝트를 걷는 일입니다(`GameStateSnapshot::registerViewOwner` 와 같은 일). 세운 것이 없는 타입은 nullptr 입니다. */
        StatefulComponentRegistration( const utf8* pTypeName, CaptureFn pCapture, RestoreFn pRestore, DespawnFn pDespawn, SourceLocation site );
        ~StatefulComponentRegistration()                                                 = default;
        StatefulComponentRegistration( const StatefulComponentRegistration& )            = delete;
        StatefulComponentRegistration& operator=( const StatefulComponentRegistration& ) = delete;

        /** @brief 지금 올라 있는 등록입니다(등록 순서). */
        static const vector<StatefulComponentRegistration*>& getRegistrations();

        const utf8* getTypeName() const { return _pTypeName; }
        CaptureFn   getCapture() const { return _pCapture; }
        RestoreFn   getRestore() const { return _pRestore; }
        DespawnFn   getDespawn() const { return _pDespawn; }

    private:
        [[nodiscard]] static bool add( StatefulComponentRegistration* pRegistration );
        static void               remove( StatefulComponentRegistration* pRegistration );

        const utf8*                                     _pTypeName;
        CaptureFn                                       _pCapture;
        RestoreFn                                       _pRestore;
        DespawnFn                                       _pDespawn;
        StaticRegistrar<StatefulComponentRegistration*> _registrar; ///< 마지막에 만들어 먼저 사라진다 — 칸이 선 뒤에 오르고 칸보다 먼저 내린다
    };
} // namespace sw

namespace sw
{
    namespace ComponentStateStoreTraits
    {
        /** @brief @p T 에 세운 오브젝트를 걷는 `despawnViews()` 가 있는지 봅니다(C++17 이라 `requires` 를 쓰지 않는다). */
        template <typename T, typename = void>
        struct HasDespawnViews : std::false_type
        {
        };
        template <typename T>
        struct HasDespawnViews<T, std::void_t<decltype( std::declval<T&>().despawnViews() )>> : std::true_type
        {
        };
        /** @brief @p T 가 PROPERTY 밖의 상태를 싣는 `restoreState( vector<uint8>&& )` 를 가졌는지 봅니다(쓰기 `serializeState` 와 짝으로 선언한다). */
        template <typename T, typename = void>
        struct HasRestoreState : std::false_type
        {
        };
        template <typename T>
        struct HasRestoreState<T, std::void_t<decltype( std::declval<T&>().restoreState( std::declval<vector<uint8>&&>() ) )>> : std::true_type
        {
        };
    } // namespace ComponentStateStoreTraits

    /**
     * @class StatefulComponentRegistrar
     * @brief @p TComponent 의 `serializeState` / `restoreState` 를 `ComponentStateStore` 에 연결하는 정적 등록입니다.
     * @details 오브젝트 매니저를 include 한 .cpp 에서 만든다(순회 템플릿이 그때 인스턴스화된다).
     *          `despawnViews()` 가 있는 타입(`GameModeComponent` 파생 — 키트의 부지 · 디렉터)은 상태를 실은 뒤 세운 것도 걷는다. 걷지 않으면 세운 오브젝트가 씬 섹션에 실려
     *          복원한 뒤 다시 세운 것과 겹친다 — 그래서 등록 한 줄이 둘을 함께 정한다(게임마다 `registerGameMode` 를 적지 않는다).
     *          상태 없이 세운 것만 가진 타입(`CharacterAppearanceComponent` — 스폰한 부품 · 합친 구간 컴포넌트)도 같은 한 줄이다 — 있는 쪽만 연결한다.
     */
    template <typename TComponent>
    class StatefulComponentRegistrar final : public StatefulComponentRegistration
    {
        static_assert( ComponentStateStoreTraits::HasRestoreState<TComponent>::value || ComponentStateStoreTraits::HasDespawnViews<TComponent>::value,
                       "StatefulComponentRegistrar needs serializeState/restoreState or despawnViews" );

    public:
        explicit StatefulComponentRegistrar( const utf8* pTypeName, SourceLocation site = SourceLocation::current() )
            : StatefulComponentRegistration( pTypeName, selectCapture(), selectRestore(), selectDespawn(), site )
        {
        }

    private:
        static CaptureFn selectCapture()
        {
            if constexpr ( ComponentStateStoreTraits::HasRestoreState<TComponent>::value )
                return &StatefulComponentRegistrar::captureAllComponentStates;
            else
                return nullptr;
        }
        static RestoreFn selectRestore()
        {
            if constexpr ( ComponentStateStoreTraits::HasRestoreState<TComponent>::value )
                return &StatefulComponentRegistrar::restoreAllComponentStates;
            else
                return nullptr;
        }
        static DespawnFn selectDespawn()
        {
            if constexpr ( ComponentStateStoreTraits::HasDespawnViews<TComponent>::value )
                return &StatefulComponentRegistrar::despawnAllViews;
            else
                return nullptr;
        }
        /** @brief 컴포넌트를 먼저 모으고 순회 밖에서 걷는다 — 순회 콜백 안(매니저 잠금)에서는 오브젝트를 지울 수 없다. */
        static void despawnAllViews( SceneIterationKey key, GameObjectManager& manager )
        {
            if constexpr ( ComponentStateStoreTraits::HasDespawnViews<TComponent>::value )
            {
                vector<TComponent*> listComponent;
                manager.template forEachComponentOfType<TComponent>( key, [&listComponent]( TComponent* pComponent )
                { listComponent.push_back( pComponent ); } );
                for ( TComponent* pComponent : listComponent )
                    pComponent->despawnViews();
            }
            else
            {
                (void)key, (void)manager;
            }
        }
        static void captureAllComponentStates( SceneIterationKey key, ComponentStateStore& store, GameObjectManager& manager )
        {
            if constexpr ( ComponentStateStoreTraits::HasRestoreState<TComponent>::value )
                (void)store.capture<TComponent>( key, manager );
            else
                (void)key, (void)store, (void)manager;
        }
        static void restoreAllComponentStates( SceneIterationKey key, const ComponentStateStore& store, GameObjectManager& manager )
        {
            if constexpr ( ComponentStateStoreTraits::HasRestoreState<TComponent>::value )
                (void)store.restore<TComponent>( key, manager ); // 짝이 없는 컴포넌트는 그대로 둔다
            else
                (void)key, (void)store, (void)manager;
        }
    };
} // namespace sw
