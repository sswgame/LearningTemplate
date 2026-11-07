/**
 * @file PrefabPool.h
 * @brief 프리팹 하나의 인스턴스 풀입니다 — 돌려받은 인스턴스를 꺼 두었다가 다시 켜서 내줍니다(탄 · 손님 · 유닛처럼 자주 생기고 사라지는 것).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;

    /**
     * @class PrefabPool
     * @brief 프리팹 인스턴스를 만들고 지우는 대신 끄고 켭니다(유니티 `ObjectPool<T>` · 언리얼 액터 풀링의 자리).
     * @details `acquire` 는 꺼 둔 인스턴스가 있으면 켜서, 없으면 `PrefabCache::spawn` 으로 새로 만들어 돌려준다. `release` 는 끄고 쉬는 목록에 넣으며,
     *          쉬는 수가 `_maxIdleCount` 를 넘으면 그 인스턴스는 지운다. 다시 켠 인스턴스의 컴포넌트 상태(속도 · 체력)는 돌려줄 때 그대로다 —
     *          쓰는 쪽이 꺼낸 뒤 자리 · 상태를 다시 정한다(풀은 프리팹 원형으로 되돌리지 않는다). 쉬는 인스턴스는 핸들로 들고, 지워졌으면 건너뛴다.
     *          `prewarm` 은 컷 · 레벨 시작 전에 미리 만들어 꺼 둔다(첫 사용 프레임의 스폰 비용을 앞당긴다).
     *          수가 정해진 효과(탄착 · 탄도선)는 `acquireRecycling` 을 쓴다 — 꺼낸 수가 상한이면 가장 오래 꺼낸 것을 다시 내주어(유니티 파티클 `Stop Action Recycle` 의 자리, 기억에 의존한 비교) 연사가 풀보다 빨라도 새 효과가 보인다.
     *          풀이 만든 인스턴스는 씬의 평범한 오브젝트라 상태 스냅샷 앞에 걷어야 한다 — 쓰는 쪽이 `destroyAll` 을 부른다.
     */
    class SW_GF_API PrefabPool
    {
    public:
        /** @brief 새 인스턴스를 만드는 길입니다. 코드로 세우는 쪽과 테스트가 넘기고, 넘기지 않으면 프리팹을 스폰합니다. */
        using SpawnDelegate = Delegate<GameObject*( GameObjectManager& manager, const utf8* pInstanceName )>;

        /** @param prefabPath 리소스 상대 경로(`.prefab.xml`)입니다. 비면 `acquire` 가 오류를 남기고 nullptr 를 돌려준다. */
        PrefabPool( string prefabPath, uint32 maxIdleCount );

        /** @brief 새 인스턴스를 프리팹 대신 @p spawner 로 만듭니다. */
        void setSpawner( SpawnDelegate spawner ) { _spawner = std::move( spawner ); }

        /** @brief 쉬는 인스턴스를 켜서, 없으면 새로 만들어 돌려줍니다. 프리팹을 읽지 못하면 nullptr 입니다(`PrefabCache` 가 이유를 남긴다). */
        GameObject* acquire( GameObjectManager& manager, const utf8* pInstanceName );
        /** @brief `acquire` 와 같되 꺼낸 수가 @p maxBusyCount 면 가장 오래 꺼낸 인스턴스를 그대로 다시 내줍니다(끄고 켜지 않는다 — 쓰는 쪽이 자리 · 상태를 다시 정한다). */
        GameObject* acquireRecycling( GameObjectManager& manager, uint32 maxBusyCount, const utf8* pInstanceName );
        /** @brief 인스턴스를 끄고 쉬는 목록에 넣습니다. 쉬는 수가 상한이면 지웁니다. 이미 지워진 핸들이면 아무것도 하지 않습니다. */
        void release( GameObjectManager& manager, GameObjectHandle handle );
        /** @brief 꺼낸 인스턴스 가운데 @p predicate( 오브젝트 ) 가 참인 것을 모두 돌려받습니다. 지워진 핸들은 걷습니다. */
        template <typename TPredicate>
        void releaseBusyIf( GameObjectManager& manager, TPredicate&& predicate )
        {
            for ( size_t busyIndex = _listBusy.size(); busyIndex > 0; --busyIndex )
            {
                const GameObjectHandle handle  = _listBusy[busyIndex - 1];
                const GameObject*      pObject = findLive( manager, handle );
                if ( pObject == nullptr )
                    _listBusy.erase( _listBusy.begin() + static_cast<ptrdiff_t>( busyIndex - 1 ) );
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
        uint32 getBusyCount() const { return static_cast<uint32>( _listBusy.size() ); }
        /** @brief 이 풀이 새로 만든 인스턴스 수입니다(재사용은 세지 않는다 — 진단 · 테스트). */
        uint32 getCreatedCount() const { return _createdCount; }

    private:
        /** @brief 새로 만듭니다(`_spawner` 또는 프리팹). */
        GameObject* spawnInstance( GameObjectManager& manager, const utf8* pInstanceName );
        /** @brief 살아 있는 오브젝트입니다. 지워졌거나 지워지는 중이면 nullptr 입니다. */
        static GameObject* findLive( GameObjectManager& manager, GameObjectHandle handle );

    private:
        string                   _prefabPath;
        SpawnDelegate            _spawner;
        vector<GameObjectHandle> _listIdle; ///< 꺼 둔 인스턴스(나중에 넣은 것을 먼저 꺼낸다)
        vector<GameObjectHandle> _listBusy; ///< 꺼낸 인스턴스(오래 꺼낸 것이 앞)
        uint32                   _maxIdleCount;
        uint32                   _createdCount;
    };
} // namespace sw
