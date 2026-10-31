/**
 * @file PrefabPool.h
 * @brief 프리팹 하나의 인스턴스 풀입니다 — 돌려받은 인스턴스를 꺼 두었다가 다시 켜서 내줍니다(탄 · 손님 · 유닛처럼 자주 생기고 사라지는 것).
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

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
     */
    class SW_GF_API PrefabPool
    {
    public:
        /** @param prefabPath 리소스 상대 경로(`.prefab.xml`)입니다. 비면 `acquire` 가 오류를 남기고 nullptr 를 돌려준다. */
        PrefabPool( string prefabPath, uint32 maxIdleCount );

        /** @brief 쉬는 인스턴스를 켜서, 없으면 새로 만들어 돌려줍니다. 프리팹을 읽지 못하면 nullptr 입니다(`PrefabCache` 가 이유를 남긴다). */
        GameObject* acquire( GameObjectManager& manager, const utf8* pInstanceName );
        /** @brief 인스턴스를 끄고 쉬는 목록에 넣습니다. 쉬는 수가 상한이면 지웁니다. 이미 지워진 핸들이면 아무것도 하지 않습니다. */
        void release( GameObjectManager& manager, GameObjectHandle handle );
        /** @brief 쉬는 인스턴스가 @p count 개가 되도록 미리 만들어 꺼 둡니다(상한을 넘지 않는다). 만든 수를 반환합니다. */
        uint32 prewarm( GameObjectManager& manager, uint32 count, const utf8* pInstanceName );
        /** @brief 쉬는 인스턴스를 모두 지웁니다(꺼낸 것은 그대로). */
        void clear( GameObjectManager& manager );

        const string& getPrefabPath() const { return _prefabPath; }
        /** @brief 지금 쉬는(꺼 둔) 인스턴스 수입니다. 지워진 핸들도 셉니다 — 다음 `acquire` 가 걷는다. */
        uint32 getIdleCount() const { return static_cast<uint32>( _listIdle.size() ); }
        /** @brief 이 풀이 새로 만든 인스턴스 수입니다(재사용은 세지 않는다 — 진단 · 테스트). */
        uint32 getCreatedCount() const { return _createdCount; }

    private:
        /** @brief 프리팹을 새로 스폰합니다. */
        GameObject* spawnInstance( GameObjectManager& manager, const utf8* pInstanceName );

        string                   _prefabPath;
        vector<GameObjectHandle> _listIdle; ///< 꺼 둔 인스턴스(나중에 넣은 것을 먼저 꺼낸다)
        uint32                   _maxIdleCount;
        uint32                   _createdCount;
    };
} // namespace sw
