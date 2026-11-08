/**
 * @file DevTools/Client/Render/MeshLODForce.h
 * @brief `-gv_meshLodForce=N` 을 활성 씬의 LOD 시스템에 거는 개발 조종입니다 — 메시 LOD 를 화면 크기와 상관없이 단계 N 으로 그린다(단계 확인 캡처 · 비교 측정).
 * @details 예전에는 런타임 `MeshLODSystem` 이 이 전역 변수를 프레임마다 읽었다 — 개발 스위치가 배포본의 LOD 선택 경로에 있었다. 지금은 런타임에 이미 있는 고정 창구
 *          (`MeshLODSystem::setForcedLOD`, 메시 미리보기 창이 쓰는 길)를 개발 도구가 부른다. 언리얼 `r.ForceLOD` 가 개발 cvar 인 것과 같은 자리다.
 */
#pragma once

namespace sw
{
    /** @struct MeshLODForce @brief 활성 씬에 `-gv_meshLodForce` 를 맞춥니다. 게임 스레드에서 프레임마다 부릅니다. */
    struct MeshLODForce
    {
        /** @brief 값이 0 이상이면 활성 씬의 고정 단계를 그 값으로 맞추고, -1 로 돌아오면 한 번 풀어 준다(씬이 바뀌면 새 씬에 다시 건다). */
        static void apply();
    };
} // namespace sw
