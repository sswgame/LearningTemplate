/**
 * @file ModuleHost/ModuleReloadDriver.h
 * @brief 호스트(App · Server)가 소유하는 모듈 핫 리로드입니다 — `LiveReloadManager` 의 수명, 리로드 요청, 프레임마다의 교체, 그리고 자동화 창구(`module.reload` · 탐침 `Module.*`)입니다.
 * @details App 과 Server 가 같은 모양으로 씁니다. 리로드를 거는 입력(단축키)은 그 입력을 아는 호스트가 정하고 여기에는 `requestReload` 로 넘깁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class LiveReloadManager;
    class ModuleImageHostSubsystem;
    class SubsystemScope;

    /**
     * @class ModuleReloadDriver
     * @brief 호스트가 값 멤버로 들고 직접 부릅니다. **Shipping 에는 리로드할 모듈이 없어** 매니저가 없고 모든 함수가 아무것도 하지 않습니다.
     * @details `initialize` 가 자신을 호스트 범위에 겁니다(`SubsystemScope::bind`). 개발 명령 `module.reload` 와 탐침 `Module.*` 는 `engine::findSubsystem<ModuleReloadDriver>()`
     *          로 엔진 범위에서 호스트 범위까지 올라가 찾습니다 — 정적 포인터 창구를 두지 않습니다. 모듈 DLL 을 언로드할 수 있는 곳은 "씬은 사라졌고 서비스는 아직 있는" 좁은 구간뿐이라
     *          매니저 종료와 범위에서 풀기는 호스트 범위의 모듈 이미지 창구(`ModuleImageHostSubsystem`)의 씬 뒤 훅에 겁니다.
     *          등록 매크로(개발 명령 · 탐침)는 이 클래스의 `.cpp` 에 있습니다. 호스트가 이 클래스를 부르므로 정적 라이브러리에서도 그 오브젝트가 링크에서 빠지지 않습니다.
     */
    class ModuleReloadDriver
    {
    public:
        ModuleReloadDriver();
        ~ModuleReloadDriver();

        ModuleReloadDriver( const ModuleReloadDriver& )            = delete;
        ModuleReloadDriver& operator=( const ModuleReloadDriver& ) = delete;

        /**
         * @brief 매니저를 만들고 자신을 @p hostScope 에 걸며, @p moduleImageHost 의 씬 뒤 훅에 매니저 종료를 겁니다.
         * @details 모듈을 로드하는 ModuleHost 보다 먼저 불러야 합니다. ModuleHost 가 이 매니저로 모듈을 로드하고 리로드 콜백을 등록합니다.
         */
        void initialize( SubsystemScope& hostScope, ModuleImageHostSubsystem& moduleImageHost );

        /** @brief @p pModuleName 모듈과 그것에 의존하는 모듈의 리로드를 요청합니다(단축키 · 개발 명령). 실제 교체는 `update` 가 합니다. */
        void requestReload( const utf8* pModuleName );
        /** @brief 요청된 모듈 교체를 실행합니다. **틱 직전**(게임 업데이트 뒤, 씬 틱 앞)에 부릅니다. 여기서 DLL 이 바뀌고 인스턴스가 새로 만들어집니다. */
        void update();

        /** @brief 핫 리로드 매니저입니다. Shipping 에서는 항상 nullptr 이고, 받는 쪽이 그 경우를 처리합니다. */
        LiveReloadManager* getLiveReloadManager() const;

        // --- 자동화 창구(개발 명령 `module.reload` · 탐침 `Module.*`) ---------------------------------------------------------------------------------
        /**
         * @brief `module.reload` — 실린 모듈이면 리로드를 예약하고 끝을 볼 완료 수를 적습니다(`isReloadPending`).
         * @return 그 이름의 모듈이 실려 있지 않으면 false 입니다.
         */
        [[nodiscard]] bool queueReload( const utf8* pModuleName );
        /** @brief `queueReload` 로 예약한 리로드가 아직 끝나지 않았으면 true 입니다(그래프가 깨졌으면 끝난 것으로 봅니다). 앞선 리로드 횟수와 무관합니다. */
        bool isReloadPending() const;

    private:
#if !defined( SW_SHIPPING )
        unique_ptr<LiveReloadManager> _liveReloadManager;
        /** @brief 자신을 건 호스트 범위입니다(소유하지 않는다). 씬 뒤 훅이 여기서 자신을 풉니다. */
        SubsystemScope* _pHostScope;
        /** @brief `queueReload` 가 예약한 리로드가 끝났다고 볼 완료 수입니다(예약 때의 완료 수 + 1). 0 이면 예약한 적이 없다. */
        uint32 _reloadTargetCount;
        /** @brief 모듈 이미지 창구에 건 "씬이 사라졌다" 훅의 구독입니다(창구가 먼저 사라지면 무효가 된다). */
        Subscription _scenesReleasedHook;
#endif
    };
} // namespace sw
