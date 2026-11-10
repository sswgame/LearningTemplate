/**
 * @file NavAgentCrowdSync.h
 * @brief 에이전트 컴포넌트와 군중 사이를 옮깁니다 — 요청(목적지 · 멈춤 · 순간이동)과 자리를 군중에 넣고, 군중 결과를 오브젝트 · 캐릭터 컨트롤러에 씁니다.
 *        부모 없는 오브젝트의 자리 · 회전은 쓰기 목록으로 모아 부른 쪽이 `GameObjectManager::applyTransformBatch` 한 번에 적용한다(물리 `ScenePhysics::queuePoseWrite` 와 같은 길).
 * @details 에이전트를 무엇이 움직이는지(`NavAgentDriveMode`), 순간이동으로 보는 거리, 몸을 돌리는 규칙이 바뀔 때만 이 파일이 바뀝니다.
 *          `SceneNavigation::update` 가 에이전트마다 게임 스레드에서 직접 부릅니다(가상 호출 없음).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    struct NavMeshRuntime;
    struct SceneTransformWrite;

    class NavMeshAgentComponent;

    /** @class NavAgentCrowdSync @brief 파일 머리말 참고. 상태가 없는 정적 함수 묶음입니다. */
    class NavAgentCrowdSync
    {
    public:
        /**
         * @brief 에이전트를 @p runtime 의 군중에 넣거나(처음) 요청 · 자리를 맞춥니다. 처음 넣을 때 @p listRuntime 에서 @p runtime 의 자리를 에이전트에 적습니다.
         */
        static void syncAgentIntoCrowd( NavMeshAgentComponent& agent, NavMeshRuntime& runtime, const vector<unique_ptr<NavMeshRuntime>>& listRuntime );
        /**
         * @brief 군중이 낸 자리 · 속도 · 상태를 에이전트에 쓰고, 방식에 따라 오브젝트를 옮기거나 컨트롤러에 속도를 넘깁니다.
         * @details 오브젝트의 자리(`Transform` 방식) · 몸 돌리기는 부모 없는 트랜스폼이면 @p inoutListWrite 에 한 건으로 붙인다(월드가 곧 로컬) — 부모가 있으면
         *          세터로 바로 쓴다(월드를 부모 기준으로 풀어야 한다). 목록을 적용한 뒤 `recordWrittenTransform` 을 불러야 다음 갱신이 자기 쓰기를 바깥 이동으로 보지 않는다.
         * @return 결과를 썼으면 true 입니다(군중에 에이전트 상태가 없으면 false — 그때는 쓴 번호도 적지 않는다).
         */
        [[nodiscard]] static bool writeAgentResult( NavMeshAgentComponent& agent, NavMeshRuntime& runtime, float32 deltaTime, vector<SceneTransformWrite>& inoutListWrite );
        /** @brief 쓰기를 적용 · 펼친 뒤 자기 쓰기 뒤의 월드 변경 번호를 적습니다(`Transform` 방식만 — 다음 갱신의 순간이동 판정이 본다). */
        static void recordWrittenTransform( NavMeshAgentComponent& agent );
    };
} // namespace sw
