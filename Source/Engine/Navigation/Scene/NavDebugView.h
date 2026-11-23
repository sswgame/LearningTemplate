/**
 * @file NavDebugView.h
 * @brief 내비게이션 디버그 그리기 — 폴리곤 테두리 · 경로 · 속도 · 장애물을 선으로 내고, 걷는 면 · 경로 띠를 씬 오브젝트(`NavMeshDebugView`)의 메시로 짓습니다.
 * @details `SceneNavigation` 의 값 멤버입니다. 무엇을 그릴지는 `NavDebugDrawFlag`가 정하고(디버그 HUD 범주), 그리는 모양 · 색이 바뀔 때만 이 파일이 바뀝니다.
 *          게임 스레드, 씬 틱 밖에서 부릅니다(`DebugHUD::drawSections`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Foundation/GameObjectHandle.h"

namespace sw
{
    struct NavMeshRuntime;

    class GameObjectManager;
    class IPhysicsDebugRenderer;
    class NavMeshAgentComponent;
    class NavMeshObstacleComponent;

    /** @class NavDebugView @brief 파일 머리말 참고. */
    class NavDebugView
    {
    public:
        NavDebugView() = default;

        /** @brief @p flags(`NavDebugDrawFlag`)의 선(테두리 · 경로 · 속도 · 장애물 · 대략 그래프와 먼 경로)을 냅니다. */
        static void drawLines( IPhysicsDebugRenderer& renderer, uint32 flags, const vector<unique_ptr<NavMeshRuntime>>& listRuntime,
                               const vector<NavMeshObstacleComponent*>& listObstacle, const vector<NavMeshAgentComponent*>& listAgent );
        /**
         * @brief @p bShown 이고 내비메시가 있으면 걷는 면(영역 색 · 폴리곤마다 명암)과 에이전트 경로 띠를 메시 하나로 지어
         *        씬의 오브젝트(베이크에서 빠진다)에 걸고, 아니면 그 오브젝트를 지웁니다.
         */
        void updateSolidView( GameObjectManager& manager, bool bShown, const vector<unique_ptr<NavMeshRuntime>>& listRuntime,
                              const vector<NavMeshAgentComponent*>& listAgent );

    private:
        /** @brief 대략 그래프(클러스터 · 간선 — 타일 스트리밍 단계 색)와 에이전트의 먼 경로를 선으로 냅니다(`NavDebugDrawFlag::kCluster`). */
        static void drawClusterGraph( IPhysicsDebugRenderer& renderer, const vector<unique_ptr<NavMeshRuntime>>& listRuntime, const vector<NavMeshAgentComponent*>& listAgent );

    private:
        GameObjectHandle _viewObject; ///< `updateSolidView` 가 세운 오브젝트
    };
} // namespace sw
