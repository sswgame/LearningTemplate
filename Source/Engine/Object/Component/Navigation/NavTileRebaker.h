/**
 * @file NavTileRebaker.h
 * @brief 레벨 일부가 바뀌었을 때 닿은 타일만 워커로 다시 베이크합니다 — 장애물 감시, 알린 영역 모으기, 입력 사본 갱신, 재베이크 줄과 워커 일, 끝난 타일 끼우기.
 * @details `SceneNavigation` 의 값 멤버입니다. 갱신 순서(장애물 → 영역 → 끝난 결과 끼우기 → 워커에 넘기기)는 `SceneNavigation::update` 가 정합니다.
 *          `invalidateArea` 만 아무 스레드에서 부르고(스핀 잠금), 나머지는 게임 스레드에서 부릅니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Container/vector.h"
#include "Core/Math/AABB.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    struct NavConvexVolume;
    struct NavMeshRuntime;
    struct NavMeshSettings;

    class GameObjectManager;
    class INavGeometrySource;
    class NavMeshObstacleComponent;

    /** @class NavTileRebaker @brief 파일 머리말 참고. */
    class SW_API NavTileRebaker
    {
    public:
        NavTileRebaker();
        ~NavTileRebaker() = default;

        NavTileRebaker( const NavTileRebaker& )            = delete;
        NavTileRebaker& operator=( const NavTileRebaker& ) = delete;

        /** @brief 알린 영역과 표시를 모두 비웁니다(내비메시를 모두 놓을 때). 누적 재베이크 수는 그대로입니다. */
        void clear();

        /**
         * @brief 월드 상자 자리의 타일을 다시 베이크하게 합니다. 아무 스레드에서 불러도 됩니다. 유효하지 않은 상자는 버립니다.
         * @param bGeometryChanged 정적 기하가 바뀌었으면(파괴 · 수정자) true — 다음 영역 처리에서 기하를 다시 모은다.
         */
        void invalidateArea( const AABB& bounds, bool bGeometryChanged );
        /** @brief 장애물 부피 목록을 다음 영역 처리에서 다시 만들게 합니다(장애물이 빠지거나 다시 뚫렸을 때). */
        void markObstacleVolumeDirty() { _bObstacleVolumeDirty = true; }
        /** @brief 아직 타일로 바꾸지 않은 알린 영역 수입니다. */
        uint32 getPendingAreaCount() const;
        /** @brief 재베이크로 끼운 타일 수(누적, 진단)입니다. */
        uint32 getRebakedTileCount() const { return _rebakedTileCount; }

        /** @brief 장애물마다 생기거나 사라졌거나 @p moveThreshold 넘게 움직였거나 돌았으면 옛 · 새 자리를 알립니다. */
        void updateObstacles( const vector<NavMeshObstacleComponent*>& listObstacle, float32 moveThreshold );
        /**
         * @brief 알린 영역을 각 종류의 타일로 바꿔 재베이크 줄에 넣습니다. 장애물이 바뀌었으면 부피 목록을, 정적 기하가 바뀌었으면 기하를 새 사본으로 바꿉니다.
         * @details 베이크하는 중인 일은 옛 사본을 끝까지 읽는다(둘 다 `shared_ptr<const>`).
         */
        void processDirtyAreas( const vector<unique_ptr<NavMeshRuntime>>& listRuntime, const vector<NavMeshObstacleComponent*>& listObstacle,
                                GameObjectManager& manager, const NavMeshSettings& settings, const vector<INavGeometrySource*>& listSource );
        /** @brief 끝난 일의 타일을 내비메시에 끼웁니다(질의가 없는 구간에서만 — 갱신 첫머리). */
        void applyFinishedBakes( const vector<unique_ptr<NavMeshRuntime>>& listRuntime );

        /** @brief 줄의 타일을 워커에 넘깁니다. 종류마다 동시에 @p maxConcurrent 개(최소 1)까지, 같은 타일은 하나씩입니다. */
        static void launchTileBakes( const vector<unique_ptr<NavMeshRuntime>>& listRuntime, uint32 maxConcurrent );
        /** @brief 워커에 넘긴 일이 모두 끝날 때까지 기다립니다. */
        static void waitForAllBakes( const vector<unique_ptr<NavMeshRuntime>>& listRuntime );
        /** @brief 뚫고 있는 장애물의 부피를 모읍니다(런타임을 처음 만들 때 · 장애물이 바뀌었을 때). */
        static void collectObstacleVolumes( const vector<NavMeshObstacleComponent*>& listObstacle, vector<NavConvexVolume>& outListVolume );

    private:
        vector<AABB>     _listDirtyArea; ///< `_dirtyLock` 아래
        uint32           _rebakedTileCount;
        mutable SpinLock _dirtyLock;      ///< `invalidateArea` 가 아무 스레드에서 온다
        bool             _bGeometryDirty; ///< `_dirtyLock` 아래
        bool             _bObstacleVolumeDirty;
    };
} // namespace sw
