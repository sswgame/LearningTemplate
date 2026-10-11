/**
 * @file AppAutomation.h
 * @brief App 이 자동화 · 개발 콘솔에 내주는 모듈 리로드 창구입니다. 개발 명령 `module.reload` 와 탐침 `Module.*` 가 쓰는 `LiveReloadManager` 를 이어 줍니다.
 */
#pragma once

namespace sw
{
    class LiveReloadManager;

    /**
     * @struct AppAutomation
     * @brief 핫 리로드 매니저는 App 이 소유하고 Engine 은 모듈을 모른다 — 시나리오의 `<DevCommand line="module.reload SWGame"/>` 와 탐침이 닿도록 App 이 매니저를 여기에 묶는다.
     * @details Shipping 에는 매니저가 없으므로 아무것도 하지 않는다(개발 명령과 탐침도 등록되지 않는다).
     */
    struct AppAutomation
    {
        /** @brief 매니저를 묶습니다. nullptr 면 풉니다(매니저가 사라지기 전에 부릅니다). */
        static void bindLiveReloadManager( LiveReloadManager* pManager );
    };
} // namespace sw
