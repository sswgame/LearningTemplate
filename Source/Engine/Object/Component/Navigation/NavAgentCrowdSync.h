/**
 * @file NavAgentCrowdSync.h
 * @brief 에이전트 컴포넌트와 군중 사이를 옮깁니다 — 요청(목적지 · 멈춤 · 순간이동)과 자리를 군중에 넣고, 군중 결과를 오브젝트 · 캐릭터 컨트롤러에 씁니다.
 * @details 에이전트를 무엇이 움직이는지(`NavAgentDriveMode`), 순간이동으로 보는 거리, 몸을 돌리는 규칙이 바뀔 때만 이 파일이 바뀝니다.
 *          `SceneNavigation::update` 가 에이전트마다 게임 스레드에서 직접 부릅니다(가상 호출 없음).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    struct NavMeshRuntime;

    class NavMeshAgentComponent;

    /** @class NavAgentCrowdSync @brief 파일 머리말 참고. 상태가 없는 정적 함수 묶음입니다. */
    class NavAgentCrowdSync
    {
    public:
        /**
         * @brief 에이전트를 @p runtime 의 군중에 넣거나(처음) 요청 · 자리를 맞춥니다. 처음 넣을 때 @p listRuntime 에서 @p runtime 의 자리를 에이전트에 적습니다.
         */
        static void syncAgentIntoCrowd( NavMeshAgentComponent& agent, NavMeshRuntime& runtime, const vector<unique_ptr<NavMeshRuntime>>& listRuntime );
        /** @brief 군중이 낸 자리 · 속도 · 상태를 에이전트에 쓰고, 방식에 따라 오브젝트를 옮기거나 컨트롤러에 속도를 넘깁니다. */
        static void writeAgentResult( NavMeshAgentComponent& agent, NavMeshRuntime& runtime, float32 deltaTime );
    };
} // namespace sw
