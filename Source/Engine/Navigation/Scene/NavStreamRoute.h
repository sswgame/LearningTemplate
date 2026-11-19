/**
 * @file NavStreamRoute.h
 * @brief 스트리밍 월드의 먼 경로(계층 경로)입니다 — 대략 그래프(`NavClusterGraph`)로 지나갈 클러스터 열을 정하고, 오른 타일 구간만 Detour 로 세밀하게 걷습니다.
 * @details **계획.** 시작 · 목적지 자리의 클러스터를 찾아 대략 그래프에서 A* 로 클러스터 열을 구한다. 시작부터 타일이 이어 오른 마지막 클러스터가 "앞선(frontier)" 이다.
 *          목적지까지 다 올라 있으면 세밀 경로 그대로(군중에 목적지를 건다). 아니면 앞선 클러스터에서 다음 클러스터 쪽 타일 가장자리 점(중간 목표)을 군중에 걸고,
 *          그 너머는 클러스터 중심과 목적지를 잇는 대략 점들로 둔다. 타일이 오르거나(내비메시 판 번호) 중간 목표에 닿으면 다시 계획한다 — 언리얼 계층 경로 ·
 *          유니티 대형 월드의 "다가가며 다시 계획" 과 같다.
 *
 *          **내려간 구역 위 이동(가상 이동).** 에이전트가 선 타일이 오르지 않았거나, 앞선 클러스터 가장자리에 닿았는데 다음이 아직 내려가 있으면 군중 대신
 *          대략 점들을 따라 최대 속도로 시간을 진행한다(충돌 · 회피 없음 — 언리얼 Mass 의 낮은 LOD 이동 자리). 지나는 자리의 타일이 오르고 그 너머가 이어지면
 *          군중으로 순간이동해 세밀 이동으로 돌아온다. 오브젝트는 `Transform` 이면 자리를, `CharacterController` 면 컨트롤러 자리를 옮긴다(내려간 셀에는 땅 충돌이
 *          없다 — 물리로 걷지 않는다). `SteerOnly` 는 폰 이동이 자리를 정하므로 가상 이동하지 않고 앞선 가장자리에서 기다린다.
 *
 *          대략 그래프가 없는 내비메시(스트리밍하지 않는 씬 · 런타임 베이크)는 이 길을 타지 않는다 — 모든 타일이 늘 끼워져 있다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Navigation/NavigationTypes.h"

namespace sw
{
    struct NavMeshRuntime;
    struct SceneTransformWrite;

    class NavMeshAgentComponent;

    /** @brief 계획 하나 — 지나갈 클러스터 열과 앞선 자리입니다. */
    struct NavRoutePlan
    {
        vector<uint32> _listCluster;           ///< 시작 클러스터 … 목적지 클러스터
        uint32         _frontier{ 0 };         ///< 시작부터 타일이 이어 오른 마지막 자리(`_listCluster` 안). `_bStartLoaded` 가 false 면 뜻이 없다
        bool           _bStartLoaded{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없다

        /** @brief 목적지 클러스터까지 이어 올라 있으면 true 입니다(세밀 경로 하나로 간다). */
        bool isLoadedToGoal() const { return _bStartLoaded && _listCluster.empty() == false && _frontier + 1 == _listCluster.size(); }
    };

    /** @class NavStreamRoute @brief 파일 머리말 참고. 상태가 없는 정적 함수 묶음입니다(에이전트 상태는 에이전트가 든다). */
    class NavStreamRoute
    {
    public:
        /** @brief @p runtime 이 대략 그래프로 먼 경로를 푸는 내비메시(스트리밍 쿠킹본)면 true 입니다. */
        static bool usesRoutes( const NavMeshRuntime& runtime );
        /** @brief @p position 의 타일이 끼워져 있으면 true 입니다(스트리밍하지 않는 내비메시는 늘 true). */
        static bool isPositionLoaded( const NavMeshRuntime& runtime, const float3& position );
        /** @brief @p start 에서 @p end 까지의 클러스터 열과 앞선 자리를 정합니다. 두 끝의 클러스터가 없거나 이어지지 않으면 false 입니다. */
        [[nodiscard]] static bool plan( const NavMeshRuntime& runtime, const float3& start, const float3& end, NavRoutePlan& outPlan );
        /** @brief 앞선 클러스터에서 다음 클러스터 쪽 타일 가장자리의 내비메시 위 점입니다(목적지까지 이어졌으면 @p end). 붙이기는 @p searchExtent · @p filter 로 한다(워커는 요청의 사본을 넘긴다 — 종류의 거름은 게임 스레드가 바꾼다). */
        static float3 computeFrontierTarget( const NavMeshRuntime& runtime, const NavRoutePlan& route, const float3& end, const float3& searchExtent, const NavQueryFilter& filter );
        /**
         * @brief 계층 경로 — 동기 질의(`SceneNavigation::findStreamingPath` · 경로 감시 값)와 비동기 요청(`NavPathSolver`)이 함께 지나는 하나뿐인 길입니다.
         * @details 목적지까지 타일이 이어 올라 있거나 대략 그래프가 없으면 세밀 경로 그대로다. 아니면 @p outPath 는 앞선 타일 가장자리까지의 세밀 경로이고
         *          그 너머는 `NavPath::_listCoarsePoint` 이며 결과는 `Partial` 이다(시작 타일이 내려가 있으면 세밀 경로는 시작 한 점). 질의만 하므로 워커에서 불러도 된다 —
         *          타일 교체 · 스트리밍 단계는 질의를 수확한 뒤(쓰기 증표)에만 바뀐다. @p searchExtent · @p filter 는 세밀 경로의 것이다(요청자의 거름 사본).
         */
        static NavPathStatus findPath( const NavMeshRuntime& runtime, const float3& start, const float3& end, const float3& searchExtent, const NavQueryFilter& filter, NavPath& outPath );
        /** @brief 앞선 자리 너머의 대략 점(클러스터 중심들 · 마지막은 @p end)을 @p outListPoint 에 채웁니다. @p firstIndex 부터입니다. */
        static void collectCoarsePoints( const NavMeshRuntime& runtime, const NavRoutePlan& route, uint32 firstIndex, const float3& end, vector<float3>& outListPoint );

        // 에이전트 쪽 — `NavAgentCrowdSync` 가 게임 스레드에서 부른다.
        /** @brief 군중에 넣기 전 — 판 번호가 바뀌었거나 가상 이동이 오른 타일에 들었으면 다시 계획하게 표시합니다(`_bDestinationDirty`). */
        static void refreshAgent( NavMeshAgentComponent& agent, NavMeshRuntime& runtime, const float3& currentPosition );
        /** @brief 목적지 요청을 계획대로 군중(세밀) · 중간 목표 · 가상 이동 가운데 하나로 넣습니다. */
        static void requestAgentMove( NavMeshAgentComponent& agent, NavMeshRuntime& runtime, const float3& currentPosition );
        /** @brief 계획 상태를 비웁니다(멈춤 · 순간이동 · 도착). */
        static void clearAgentRoute( NavMeshAgentComponent& agent );
        /** @brief 중간 목표로 가는 에이전트(`_bRouteIntermediate`)의 군중 자리가 그 목표에 닿았으면 다시 계획하게 표시합니다(도착으로 두지 않는다). */
        static void handleIntermediateReached( NavMeshAgentComponent& agent, const float3& agentPosition, bool bCrowdArrived );
        /**
         * @brief 가상 이동을 @p deltaTime 만큼 진행하고 자리를 씁니다(부모 없는 트랜스폼은 @p inoutListWrite 에 한 건). 마지막 점에 닿으면 도착입니다.
         * @details `NavAgentCrowdSync::writeAgentResult` 가 가상 이동 중인 에이전트에 군중 결과 대신 부른다.
         */
        static void writeVirtualResult( NavMeshAgentComponent& agent, NavMeshRuntime& runtime, float32 deltaTime, vector<SceneTransformWrite>& inoutListWrite );

    private:
        /** @brief 가상 이동으로 바꿉니다 — 군중 목표를 지우고 @p route 의 @p firstIndex 너머 대략 점을 따라간다. */
        static void enterVirtualMove( NavMeshAgentComponent& agent, NavMeshRuntime& runtime, const NavRoutePlan& route, uint32 firstIndex, const float3& currentPosition );
    };
} // namespace sw
