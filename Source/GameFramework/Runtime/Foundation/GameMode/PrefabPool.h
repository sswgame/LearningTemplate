/**
 * @file PrefabPool.h
 * @brief 프리팹 하나의 인스턴스 풀입니다 — 돌려받은 인스턴스를 꺼 두었다가 다시 켜서 내줍니다(탄 · 손님 · 유닛처럼 자주 생기고 사라지는 것).
 */
#pragma once
#include "Core/Concurrency/DevCounter.h"
#include "Core/Container/LRUMap.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Prefab/SpawnLifetime.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Component;
    class GameObject;
    class GameObjectManager;

    /**
     * @brief 프리팹 하나를 풀로 돌릴지 정하는 데이터입니다(`GameModeComponent::spawnPrefab` 이 받는다). Lyra · Boss Room 의 풀 설정(프리팹 + 쉬게 둘 수) 자리입니다.
     * @details `_maxIdleCount` 가 0 이면 풀 없이 매번 스폰하고 지운다. 0 보다 크면 그 프리팹의 컴포넌트가 모두 되돌리기(`Component::resetToSpawnState`)를 구현해야 한다 —
     *          아니면 풀이 처음 만들 때 오류로 막는다(`PrefabPool`).
     */
    REFLECT()
    struct SW_GF_API PrefabPoolPolicy
    {
        REFLECT_BODY();

        PROPERTY( Min = 0, Tooltip = "Released instances kept switched off for reuse; 0 spawns and destroys every time (pooling needs every component to implement resetToSpawnState)" )
        uint32 _maxIdleCount{ 0 };
    };

    /**
     * @class PrefabPool
     * @brief 프리팹 인스턴스를 만들고 지우는 대신 끄고 켭니다(유니티 `ObjectPool<T>` · 언리얼 액터 풀링의 자리).
     * @details `acquire` 는 꺼 둔 인스턴스가 있으면 켜서, 없으면 `PrefabCache::spawn` 으로 새로 만들어 돌려준다. `release` 는 계층의 컴포넌트를 모두
     *          스폰 상태로 되돌리고(`Component::resetToSpawnState` — 죽음 · 래그돌 · 절단 · 빙의) 끈 뒤 쉬는 목록에 넣으며, 쉬는 수가 `_maxIdleCount` 를 넘으면 지운다.
     *          돌려받거나 다시 내줄 때 인스턴스의 핸들을 끊는다(`GameObject::invalidateHandles`) — 앞 생을 겨누던 쪽은 파괴된 대상처럼 nullptr 을 받고, 꺼낸 쪽은 꺼낸 뒤 핸들을 받는다.
     *          꺼낸 쪽이 매번 다시 정하는 값(자리 · `launch` 같은 시작 호출)은 꺼낸 뒤 정한다. 쉬는 인스턴스는 핸들로 들고, 지워졌으면 건너뛴다.
     *          **풀 계약**: 처음 만든 인스턴스의 계층 전부에 되돌리기를 불러 보고, 구현하지 않은 컴포넌트(`SpawnStateReset::Unsupported`)가 있으면 오류를 남기고
     *          그 인스턴스를 지운 뒤 이 풀을 막는다 — 다음 꺼내기도 nullptr 이다(매번 스폰으로 물러서지 않는다, 유니티 `IPoolable` · 언리얼 풀 인터페이스의 강제를 형식 대신 등록에서).
     *          `prewarm` 은 컷 · 레벨 시작 전에 미리 만들어 꺼 둔다(첫 사용 프레임의 스폰 비용을 앞당긴다).
     *          수가 정해진 효과(탄착 · 탄도선)는 `acquireRecycling` 을 쓴다 — 꺼낸 수가 상한이면 가장 오래 꺼낸 것을 다시 내주어(유니티 파티클 `Stop Action Recycle` 의 자리, 기억에 의존한 비교) 연사가 풀보다 빨라도 새 효과가 보인다.
     *          상태(`SpawnLifetime::Persistent`)로 만든 인스턴스는 씬의 평범한 오브젝트라 상태 스냅샷 앞에 걷어야 한다 — 쓰는 쪽이 `destroyAll` 을 부른다. 모습(`Derived`)은 스냅샷이 건너뛴다.
     */
    class SW_GF_API PrefabPool
    {
    public:
        /** @brief 새 인스턴스를 만드는 길입니다. 코드로 세우는 쪽과 테스트가 넘기고, 넘기지 않으면 프리팹을 스폰합니다. */
        using SpawnDelegate = Delegate<GameObject*( GameObjectManager& manager, const utf8* pInstanceName )>;

        /**
         * @param prefabPath 리소스 상대 경로(`.prefab.xml`)입니다. 비면 `acquire` 가 오류를 남기고 nullptr 를 돌려준다.
         * @param lifetime 꺼낸 것이 상태인지 모습인지입니다(`SpawnLifetime`).
         */
        PrefabPool( string prefabPath, uint32 maxIdleCount, SpawnLifetime lifetime );

        /** @brief 새 인스턴스를 프리팹 대신 @p spawner 로 만듭니다. */
        void setSpawner( SpawnDelegate spawner ) { _spawner = std::move( spawner ); }

        /** @brief 쉬는 인스턴스를 켜서, 없으면 새로 만들어 돌려줍니다. 프리팹을 읽지 못하면 nullptr 입니다(`PrefabCache` 가 이유를 남긴다). */
        GameObject* acquire( GameObjectManager& manager, const utf8* pInstanceName );
        /** @brief `acquire` 와 같되 꺼낸 수가 @p maxBusyCount 면 가장 오래 꺼낸 인스턴스를 그대로 다시 내줍니다(끄고 켜지 않는다 — 쓰는 쪽이 자리 · 상태를 다시 정한다). */
        GameObject* acquireRecycling( GameObjectManager& manager, uint32 maxBusyCount, const utf8* pInstanceName );
        /** @brief 인스턴스를 스폰 상태로 되돌려 끄고 쉬는 목록에 넣습니다. 쉬는 수가 상한이면 되돌린 뒤 지웁니다(외형 부품은 부품 풀로 간다). 이미 지워진 핸들이면 아무것도 하지 않습니다. */
        void release( GameObjectManager& manager, GameObjectHandle handle );
        /** @brief 꺼낸 인스턴스 가운데 @p predicate( 오브젝트 ) 가 참인 것을 모두 돌려받습니다. 지워진 핸들은 걷습니다. */
        template <typename TPredicate>
        void releaseBusyIf( GameObjectManager& manager, TPredicate&& predicate )
        {
            // 돌려주기가 꺼낸 목록을 바꾼다 — 베껴서 늦게 꺼낸 것부터 본다(쉬는 목록이 쌓이는 순서).
            _listScratchHandle.clear();
            for ( const auto& entry : _busyOrder )
            {
                _listScratchHandle.push_back( entry._key );
            }
            for ( size_t busyIndex = _listScratchHandle.size(); busyIndex > 0; --busyIndex )
            {
                const GameObjectHandle handle  = _listScratchHandle[busyIndex - 1];
                const GameObject*      pObject = findLive( manager, handle );
                if ( pObject == nullptr )
                    (void)_busyOrder.erase( handle );
                else if ( predicate( *pObject ) )
                    release( manager, handle );
            }
        }
        /** @brief 쉬는 인스턴스가 @p count 개가 되도록 미리 만들어 꺼 둡니다(상한을 넘지 않는다). 만든 수를 반환합니다. */
        uint32 prewarm( GameObjectManager& manager, uint32 count, const utf8* pInstanceName );
        /** @brief 쉬는 인스턴스를 모두 지웁니다(꺼낸 것은 그대로). */
        void clear( GameObjectManager& manager );
        /** @brief 쉬는 인스턴스와 꺼낸 인스턴스를 모두 지웁니다(상태 스냅샷 앞 · 판 끝). */
        void destroyAll( GameObjectManager& manager );

        const string& getPrefabPath() const { return _prefabPath; }
        /** @brief 지금 쉬는(꺼 둔) 인스턴스 수입니다. 지워진 핸들도 셉니다 — 다음 `acquire` 가 걷는다. */
        uint32 getIdleCount() const { return static_cast<uint32>( _listIdle.size() ); }
        /** @brief 지금 꺼낸 인스턴스 수입니다. 지워진 핸들도 셉니다 — 다음 `releaseBusyIf` 가 걷는다. */
        uint32 getBusyCount() const { return static_cast<uint32>( _busyOrder.size() ); }
        /** @brief 이 풀이 새로 만든 인스턴스 수입니다(재사용은 세지 않는다 — 감시 값 · 테스트만 읽는 개발 전용 칸, 배포는 0). */
        const DevCounter& getCreatedCount() const { return _createdCount; }

    private:
        /** @brief 새로 만듭니다(`_spawner` 또는 프리팹). 처음 만든 것으로 풀 계약을 확인하고, 어기면 지우고 풀을 막아 nullptr 입니다. */
        GameObject* spawnInstance( GameObjectManager& manager, const utf8* pInstanceName );
        /** @brief 계층의 컴포넌트를 모두 스폰 상태로 되돌립니다. 되돌리기를 모르는 컴포넌트가 있으면 그것(멈춘 자리)을, 아니면 nullptr 를 반환합니다. */
        [[nodiscard]] static const Component* resetHierarchy( GameObject& object );
        /** @brief 살아 있는 오브젝트입니다. 지워졌거나 지워지는 중이면 nullptr 입니다. */
        static GameObject* findLive( GameObjectManager& manager, GameObjectHandle handle );

    private:
        string                          _prefabPath;
        SpawnDelegate                   _spawner;
        vector<GameObjectHandle>        _listIdle;          ///< 꺼 둔 인스턴스(나중에 넣은 것을 먼저 꺼낸다)
        LRUMap<GameObjectHandle, uint8> _busyOrder;         ///< 꺼낸 인스턴스(오래 꺼낸 것이 앞 — 값은 쓰지 않는다). 돌려주기 · 다시 내주기가 목록을 훑지 않는다
        vector<GameObjectHandle>        _listScratchHandle; ///< `releaseBusyIf` 가 걷는 사본(프레임마다 할당하지 않는다)
        uint32                          _maxIdleCount;
        SpawnLifetime                   _lifetime;
        DevCounter                      _createdCount;
        uint8                           _bContractChecked : 1; ///< 처음 만든 인스턴스로 풀 계약을 확인했다
        uint8                           _bRefused         : 1; ///< 풀 계약을 어긴 프리팹이다 — 더 만들지 않는다
    };
} // namespace sw
