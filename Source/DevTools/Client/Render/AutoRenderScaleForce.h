/**
 * @file DevTools/Client/Render/AutoRenderScaleForce.h
 * @brief `-gv_autoRenderScaleForce=S` 를 주 렌더러의 패킷 빌더에 거는 개발 조종입니다 — 자동 렌더 배율을 GPU 시간과 무관하게 S 로 고정한다(단계 전환 시나리오 · 측정).
 * @details 런타임은 개발 변수를 읽지 않고 고정 창구(`RenderPacketBuilder::setForcedAutoRenderScale`)만 둔다 — Shipping 에서 이 변수는 상수라 켤 길이 없다.
 *          언리얼 `r.DynamicRes.TestScreenPercentage` 가 개발 cvar 인 것과 같은 자리다(`MeshLODForce` 와 같은 모양).
 */
#pragma once

namespace sw
{
    /** @struct AutoRenderScaleForce @brief 주 렌더러에 `-gv_autoRenderScaleForce` 를 맞춥니다. 게임 스레드에서 프레임마다 부릅니다. */
    struct AutoRenderScaleForce
    {
        /** @brief 값이 0 초과면 그 배율로 고정하고, 0 으로 돌아오면 고정을 풉니다(렌더러가 없는 실행은 아무것도 하지 않는다). */
        static void apply();
    };
} // namespace sw
