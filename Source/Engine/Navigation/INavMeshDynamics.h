/**
 * @file INavMeshDynamics.h
 * @brief 플레이 중에 내비메시를 바꾸는 쪽 인터페이스입니다. 오프 메시 링크 막기와 움직이는 장애물을 다룹니다.
 * @details 모두 게임 스레드에서, 질의가 없는 동안만 부른다(타일 교체와 같은 자리 — `INavMesh.h` 머리말). 언리얼 Smart Link · Dynamic Obstacle 자리다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Navigation/NavigationTypes.h"

namespace sw
{
    enum class NavObstacleMode : uint8;

    /** @brief 링크 막기와 장애물 캐시입니다. */
    class INavMeshDynamics
    {
    public:
        virtual ~INavMeshDynamics() = default;

        /**
         * @brief 번호가 @p linkID 인 오프 메시 링크를 막거나 풉니다(닫힌 문, 걷힌 사다리). 막힌 링크는 경로가 지나지 않고, 그 링크를 지나던 군중 경로는 다시 구해집니다.
         * @details 언리얼 Smart Link 의 `SetLinkEnabled` · 유니티 `NavMeshLink.activated` 에 해당한다. 상태는 내비메시가 들고 있어서 나중에 끼운 타일
         *          (재베이크 · 쿠킹본)에도 그대로 적용된다. 타일 교체와 같이 게임 스레드에서, 질의가 없는 동안만 부른다.
         */
        virtual void setOffMeshLinkEnabled( uint32 linkID, bool bEnabled ) = 0;
        /** @brief 링크가 막히지 않았으면 true 입니다(모르는 번호도 true). */
        virtual bool isOffMeshLinkEnabled( uint32 linkID ) const = 0;

        /**
         * @brief 움직이는 장애물을 다루는 방식입니다(`initialize` 의 에이전트 종류 `_obstacleMode`).
         * @details `TileCache` 면 타일 바이트(`bakeTile` · `replaceTile` · `collectTiles`)가 높이 층 묶음이고, `bakeTile` 은 덧칠 부피(@p pExtraVolume)를 받지 않는다 —
         *          장애물은 `addCacheObstacle` 로 넣는다. `RebakeTiles` 면 장애물 부피를 덧칠해 타일을 다시 베이크하고, 아래 세 함수는 오류다.
         */
        virtual NavObstacleMode getObstacleMode() const = 0;
        /** @brief 장애물 캐시에 장애물을 넣습니다(다음 `updateCacheObstacles` 가 닿은 타일을 층에서 다시 짓는다). 실패하면 오류를 남기고 0 입니다 — 경계 상자(돌린 상자는 1.41 × 긴 반 크기)가 타일 층 8 개를 넘게 걸쳐도 실패다. */
        virtual NavObstacleRef addCacheObstacle( const NavCacheObstacleDesc& desc ) = 0;
        /** @brief 장애물 캐시에서 장애물을 뺍니다. */
        virtual void removeCacheObstacle( NavObstacleRef obstacle ) = 0;
        /**
         * @brief 쌓인 장애물 요청을 처리합니다(게임 스레드 · 질의가 없는 동안 — `replaceTile` 과 같은 자리). 타일을 다시 지었으면 true 이고 번호(`getRevision`)가 오릅니다.
         */
        virtual bool updateCacheObstacles( float32 deltaSeconds ) = 0;
    };
} // namespace sw
