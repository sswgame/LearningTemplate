/**
 * @file Engine/Scene/SceneDebugDraw.h
 * @brief 활성 씬의 물리 · 내비 디버그 그리기(`gv_physicsDebugDraw` · `gv_navDebugDraw`)를 디버그 드로우 큐로 옮깁니다.
 * @details 물리는 그리는 쪽을, Graphics 는 물리를 모릅니다 — 둘보다 위 티어인 Scene 이 잇습니다(`Engine::update` 이 씬 틱 뒤에 부른다). 상태가 없어 정적 함수 하나입니다.
 */
#pragma once
#include "Core/Common/Macros.h"

namespace sw
{
    class DebugDrawQueue;
    class Scene;

    /**
     * @struct SceneDebugDraw
     * @brief 씬 틱 뒤에 한 번 부르는 디버그 선 · 내비 디버그 뷰 갱신입니다.
     */
    struct SW_API SceneDebugDraw
    {
        /**
         * @brief @p pScene 의 강체 물리(켜져 있으면)를 선으로 그리고, 내비 디버그 뷰를 이번 플래그로 맞춘 뒤 선을 그립니다.
         * @details 씬이 없거나 오브젝트 매니저가 없으면 아무것도 하지 않습니다. 비트 16(게임 화면의 메시)은 내비 디버그 뷰가 그립니다.
         */
        static void update( Scene* pScene, DebugDrawQueue& queue );
    };
} // namespace sw
