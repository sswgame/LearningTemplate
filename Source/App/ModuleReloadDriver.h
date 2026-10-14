/**
 * @file ModuleReloadDriver.h
 * @brief App 이 소유하는 모듈 핫 리로드 — `LiveReloadManager` 의 수명, 리로드를 거는 입력(단축키 · `-gv_reloadGameAtFrame`), 프레임마다의 교체입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class Engine;
    class LiveReloadManager;

    /**
     * @class ModuleReloadDriver
     * @brief App 이 값 멤버로 들고 직접 부릅니다. **Shipping 에는 리로드할 모듈이 없어** 매니저가 없고 모든 함수가 아무것도 하지 않습니다.
     * @details 모듈을 다시 올리는 장치는 App 에 있고 Engine 에는 "이 셸 액션이 눌렸나"만 묻습니다. 모듈 DLL 을 언로드할 수 있는 곳은 "씬은 사라졌고
     *          서비스는 아직 있는" 좁은 구간뿐이라 매니저 종료는 엔진의 `setOnScenesReleased` 훅에 겁니다.
     */
    class ModuleReloadDriver
    {
    public:
        ModuleReloadDriver();
        ~ModuleReloadDriver();

        /**
         * @brief 매니저를 만들고 자동화 창구(`module.reload` · 탐침 `Module.*`)와 엔진의 씬 해제 훅에 겁니다.
         * @details 모듈을 로드하는 ModuleHost 보다 먼저 불러야 합니다. ModuleHost 가 이 매니저로 모듈을 로드하고 리로드 콜백을 등록합니다.
         */
        void initialize( Engine& engine );

        /**
         * @brief 이번 프레임에 리로드를 걸 입력이 있으면 요청합니다(Ctrl+F7 게임, Ctrl+F6 에디터, `-gv_reloadGameAtFrame=N`).
         * @details 셸 액션(`Engine::updateShellActions`)을 갱신한 뒤에 부릅니다. 실제 교체는 `update` 가 틱 직전에 합니다.
         */
        void pollReloadTriggers( const Engine& engine, bool bEnableEditor );
        /** @brief 요청된 모듈 교체를 실행합니다. **틱 직전**(게임 업데이트 뒤, 씬 틱 앞)에 부릅니다. 여기서 DLL 이 바뀌고 인스턴스가 새로 만들어집니다. */
        void update();

        /** @brief 핫 리로드 매니저입니다. Shipping 에서는 항상 nullptr 이고, 받는 쪽이 그 경우를 처리합니다. */
        LiveReloadManager* getLiveReloadManager() const;

    private:
        /** @brief @p pModuleName 모듈의 리로드를 요청합니다. 매니저가 없으면 아무것도 하지 않습니다. */
        void requestReload( const utf8* pModuleName );

    private:
#if !defined( SW_SHIPPING )
        unique_ptr<LiveReloadManager> _liveReloadManager;
#endif
    };
} // namespace sw
