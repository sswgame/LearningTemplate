/**
 * @file ModuleManager/Dev/ModuleReloadDriver.h
 * @brief 호스트(App · Server)가 소유하는 모듈 핫 리로드입니다 — `LiveReloadManager` 의 수명, 리로드 요청, 프레임마다의 교체, 그리고 자동화 창구(`module.reload` · 감시 값 `Module.*`)입니다.
 * @details App 과 Server 가 같은 모양으로 씁니다. 리로드를 거는 입력(단축키)은 그 입력을 아는 호스트가 정하고 여기에는 `requestReload` 로 넘깁니다.
 *          개발 구성에만 있다(`ModuleManager/Dev/` — 배포는 모듈이 정적 링크라 리로드할 것이 없어 폴더째 짓지 않는다). 호스트는 개발 계층(`IHostDevLayer`)으로 닿는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "ModuleManager/HostDevLayer.h"

namespace sw
{
    struct ModuleResolution;

    class LiveReloadManager;
    class ModuleCatalog;
    class ModuleLoaderSubsystem;
    class SubsystemScope;

    /**
     * @class ModuleReloadDriver
     * @brief 호스트가 값 멤버로 들고 직접 부릅니다. **Shipping 에는 리로드할 모듈이 없어** 매니저가 없고 모든 함수가 아무것도 하지 않습니다.
     * @details `initialize` 가 자신을 호스트 범위에 겁니다(`SubsystemScope::bind`). 개발 명령 `module.reload` 와 감시 값 `Module.*` 는 `engine::findSubsystem<ModuleReloadDriver>()`
     *          로 엔진 범위에서 호스트 범위까지 올라가 찾습니다 — 정적 포인터 창구를 두지 않습니다. 모듈 DLL 을 언로드할 수 있는 곳은 "씬은 사라졌고 서비스는 아직 있는" 좁은 구간뿐이라
     *          매니저 종료와 범위에서 풀기는 호스트 범위의 모듈 라이브러리 창구(`ModuleLoaderSubsystem`)의 씬 뒤 훅에 겁니다.
     *          등록 매크로(개발 명령 · 감시 값)는 이 클래스의 `.cpp` 에 있습니다. 호스트가 이 클래스를 부르므로 정적 라이브러리에서도 그 오브젝트가 링크에서 빠지지 않습니다.
     */
    class ModuleReloadDriver
    {
    public:
        ModuleReloadDriver();
        ~ModuleReloadDriver();

        ModuleReloadDriver( const ModuleReloadDriver& )            = delete;
        ModuleReloadDriver& operator=( const ModuleReloadDriver& ) = delete;

        /**
         * @brief 모듈 매니페스트를 읽어 @p targetMask(`ModuleTarget` 비트)로 해석해 @p moduleLibraryHost 에 넘기고, 매니저를 만들어 자신을 @p hostScope 에 걸며,
         *        @p moduleLibraryHost 의 씬 뒤 훅에 매니저 종료를 겁니다.
         * @details 모듈을 로드하는 GameModuleManager 보다 먼저 불러야 합니다. GameModuleManager 가 이 매니저로 모듈을 로드하고 리로드 콜백을 등록합니다.
         *          App 은 `ModuleCatalog::getBuildTargetMask()`, 전용 서버는 `ModuleTarget::Server` 를 넘깁니다. 배포는 모듈을 정적으로 링크해 매니페스트가 없다 —
         *          아무것도 하지 않고 true 다(@p outCatalog · @p outResolution 은 비어 있다).
         * @return 매니페스트를 읽거나 해석하지 못하면 false 입니다(기동 실패).
         */
        [[nodiscard]] bool initialize( uint8 targetMask, SubsystemScope& hostScope, ModuleLoaderSubsystem& moduleLibraryHost, ModuleCatalog& outCatalog,
                                       ModuleResolution& outResolution );
        /**
         * @brief 리로드 배치 직전 콜백을 겁니다(전용 서버가 모듈 안의 서버 서비스를 먼저 멈춘다). 배치 콜백은 나중에 건 것이 먼저 불립니다.
         * @return 구독입니다. 매니저가 없으면(배포, `initialize` 앞) 빈 구독이고 콜백은 불리지 않습니다.
         */
        [[nodiscard]] Subscription addOnBeforeReloadBatch( BeforeReloadBatchDelegate onBeforeBatch );

        /** @brief @p pModuleName 모듈과 그것에 의존하는 모듈의 리로드를 요청합니다(단축키 · 개발 명령). 실제 교체는 `update` 가 합니다. */
        void requestReload( const utf8* pModuleName );
        /** @brief 요청된 모듈 교체를 실행합니다. **틱 직전**(게임 업데이트 뒤, 씬 틱 앞)에 부릅니다. 여기서 DLL 이 바뀌고 인스턴스가 새로 만들어집니다. */
        void update();

        /** @brief 핫 리로드 매니저입니다. Shipping 에서는 항상 nullptr 이고, 받는 쪽이 그 경우를 처리합니다. */
        LiveReloadManager* getLiveReloadManager() const;

        // --- 자동화 창구(개발 명령 `module.reload` · 감시 값 `Module.*`) ---------------------------------------------------------------------------------
        /**
         * @brief `module.reload` — 실린 모듈이면 리로드를 예약하고 끝을 볼 완료 수를 적습니다(`isReloadPending`).
         * @return 그 이름의 모듈이 실려 있지 않으면 false 입니다.
         */
        [[nodiscard]] bool queueReload( const utf8* pModuleName );
        /** @brief `queueReload` 로 예약한 리로드가 아직 끝나지 않았으면 true 입니다(그래프가 깨졌으면 끝난 것으로 봅니다). 앞선 리로드 횟수와 무관합니다. */
        bool isReloadPending() const;

    private:
        /** @brief 구성의 리로드 상태입니다(구현 파일에만 있다 — Dev 는 매니저 · 범위 · 훅, Shipping 은 비어 있다). */
        struct State;
        unique_ptr<State> _pState;
    };
} // namespace sw
