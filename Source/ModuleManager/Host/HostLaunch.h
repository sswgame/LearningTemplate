/**
 * @file ModuleManager/Host/HostLaunch.h
 * @brief App · Server 가 함께 쓰는 부팅 열입니다 — 호스트 범위 · 개발 계층 · 엔진 · 모듈 관리자 · 호스트 서비스의 수명과 그 세우기 · 내리기 순서를 한 형식이 듭니다.
 * @details 호스트마다 이 걸음을 줄로 적으면 공통 부품을 만들어도 부르는 순서 · 종료 순서가 갈라진다(예전에 Server 만 모듈 맡김 칸이 없었고, 모르는 `-gv_*` 를 조용히 버렸다).
 *          호스트에만 있는 일(종료 신호 처리기 · 서버 서비스 · 창)은 이 함수들 사이에 호스트가 끼운다. 언리얼 `FEngineLoop` 하나가 게임 · 서버 대상을 함께 세우는 것과 같은 자리다(기억).
 *
 *          수명 순서는 칸의 선언 순서가 그대로 가진다: 호스트 범위는 엔진보다 먼저 서서 나중에 사라지고(엔진 범위의 부모다), 개발 계층은 엔진 종료 뒤까지 산다(리로드 구동기가 호스트 범위에 건 것을 쥔다).
 *          호스트 서비스의 몸통은 호스트의 멤버를 쓰는데 이 형식은 호스트의 맨 앞 멤버다 — 그래서 `stopHostServices` 를 호스트의 다른 멤버가 사라지기 전(호스트의 `shutdown`)에 반드시 부른다.
 *          기동이 중간에 실패해도 같은 길로 내린다(선 것만 내린다).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemCollection.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Engine.h"
#include "Engine/Module/ModuleCatalog.h"

#include "ModuleManager/Host/HostDevLayer.h"
#include "ModuleManager/ModuleLoaderSubsystem.h"

namespace sw
{
    class GameModuleManager;
    class ILiveReloadManager;

    /** @brief 엔진을 세운 결과입니다. */
    enum class HostEngineStart : uint8
    {
        Failed,   ///< 엔진이 서지 못했거나 헤드리스 작업이 실패했다. 호스트는 기동을 멈춘다.
        Headless, ///< 헤드리스 작업(쿠킹 등)을 마쳤다. 창 · 호스트 서비스를 세우지 않는다.
        Ready,    ///< 일반 기동이다. 호스트가 이어 간다.
    };

    /**
     * @class HostLaunch
     * @brief 호스트의 부팅 열입니다. 파일 머리말 참고. 호스트가 값 멤버로 든다(상속하지 않는다).
     */
    class SW_MODULEMANAGER_API HostLaunch
    {
    public:
        /** @brief 부팅 열의 할당 태그입니다(모듈 라이브러리 올리기 — `loadModuleLibraries`). 모듈 본체는 모듈 매니저가 그 종류의 태그로 건다. */
        static constexpr MemoryTag kMemoryTag = MemoryTag::EngineMisc;

        HostLaunch();
        ~HostLaunch();

        HostLaunch( const HostLaunch& )            = delete;
        HostLaunch& operator=( const HostLaunch& ) = delete;

        /**
         * @brief 호스트 범위를 세웁니다 — 개발 계층(개발 구성만)을 더하고 모듈 라이브러리 창구를 걸어 범위를 초기화합니다. 엔진을 세우기 전에 부릅니다.
         * @details 호스트가 더 넣을 서브시스템은 이 함수 **전에** `getHostScope` 로 더합니다.
         * @param moduleTypeLoader 엔진의 `ModuleLoadStage` 가 부르는 호스트 로더입니다. 안에서 `loadModuleLibraries` 를 부릅니다.
         * @param pDevLayer 개발 계층입니다(호스트가 `makeHostDevLayer` 로 만든다 — 구현은 DevTools.dll). 개발 구성은 반드시 주고 배포는 nullptr 입니다.
         * @return 서지 못하면 false 입니다.
         */
        [[nodiscard]] bool initializeHostScope( ModuleTypeLoaderDelegate moduleTypeLoader, const utf8* pHostName, unique_ptr<IHostDevLayer> pDevLayer );
        /** @brief 엔진을 @p role 로 세우고 개발 도구의 프레임 훅을 붙입니다. 헤드리스 작업은 이 안에서 끝납니다. */
        [[nodiscard]] HostEngineStart initializeEngine( int32 argc, utf8* pArgv[], EngineHostRole role );
        /**
         * @brief 호스트 로더의 본체입니다 — 매니페스트를 @p targetMask 로 해석해 리로드 구동기를 세우고(개발 구성만), @p moduleManager 를 받아 타입 공급자 모듈의 라이브러리를 올립니다.
         * @details 어느 형식의 관리자를 쓸지는 호스트가 정한다(App 은 `ClientModuleManager`). 호스트는 `getModuleManager` 를 자기가 만든 형식으로 읽는다.
         */
        [[nodiscard]] bool loadModuleLibraries( uint8 targetMask, unique_ptr<GameModuleManager> moduleManager );
        /**
         * @brief 호스트 서비스 하나를 더합니다(세우지 않는다 — `startHostServices` 가 의존 순서로 세운다).
         * @details 서비스는 호스트가 자기 폴더에 두는 `Subsystem` 이다. 이름 · 의존은 그 형식의 `kSubsystemName` 상수가 가리키고(철자가 틀리면 컴파일 오류),
         *          몸통은 호스트의 멤버를 쓴다 — 엔진이 호스트의 서비스 목록을 열거형 · 분기로 알지 않는다(언리얼 게임 인스턴스 서브시스템 자리).
         */
        template <class TService, class... TArgs>
        TService& addHostService( TArgs&&... args )
        {
            const ScopedMemoryTag memoryScope{ TService::kSubsystemMemoryTag };
            return _hostServices.add( make_unique<TService>( std::forward<TArgs>( args )... ) );
        }
        /**
         * @brief 더한 호스트 서비스를 의존 순서로 세웁니다. 서면 선 순서를 로그로 남기고, 모듈 · 게임이 모두 선 지금까지 가져간 곳이 없는 `-gv_*` 를 경고합니다.
         * @details 예전에는 두 호스트가 이 세 걸음을 저마다 적어 Server 만 모르는 `-gv_*` 를 조용히 버렸다 — 세우는 길을 여기 하나로 둔다(언리얼 `FEngineLoop::PreInit` 하나가 게임 · 서버 대상을 함께 세운다).
         * @return 하나라도 서지 못하면(모르는 의존 포함) 오류를 남기고 false 이고, 선 것은 `stopHostServices` 가 내립니다.
         */
        [[nodiscard]] bool startHostServices();
        /** @brief 지금 서 있는 호스트 서비스를 선 순서대로 `A > B > C` 로 적습니다(기동 로그 · 테스트). */
        [[nodiscard]] string describeHostServiceOrder() const;
        /** @brief 바뀐 모듈 라이브러리를 보고 리로드를 돌립니다(동적 모듈 구성의 개발 계층만 — 그 밖에는 아무것도 하지 않는다). 호스트가 프레임마다 씬 틱 직전에 부릅니다. */
        void updateModuleReload();

        /** @brief 호스트 서비스를 역순으로 내리고 모듈 관리자를 놓습니다 — 라이브러리를 쥔 관리자는 엔진(모듈 라이브러리 종료 단계)보다 먼저 사라진다. 헤드리스 부팅 · 기동 실패면 선 것만 내립니다. */
        void stopHostServices();
        /** @brief 호스트의 게임 서비스를 내릴 때 모듈 관리자를 지웁니다. */
        void releaseModuleManager();
        /** @brief 개발 도구의 프레임 훅을 떼고 엔진을 내립니다. 엔진 범위를 내린 뒤 코어 범위(종료 누수 보고)보다 먼저 호스트 범위를 비웁니다(`Engine::shutdown`). `stopHostServices` 뒤에 부릅니다. */
        void shutdownEngine();

        SubsystemScope& getHostScope() { return _host; }
        Engine&         getEngine() { return _engine; }
        const Engine&   getEngine() const { return _engine; }
        /** @brief 개발 계층입니다. 배포는 nullptr 입니다(`build::kShipping`). */
        IHostDevLayer* getDevLayer() const { return _pDevLayer.get(); }
        /** @brief 개발 계층의 핫 리로드 매니저입니다. 배포는 nullptr 이다 — 모듈 관리자가 정적 링크로 받는다. */
#if SW_WITH_DYNAMIC_MODULES
        ILiveReloadManager* getLiveReloadManager() const { return _pDevLayer != nullptr ? _pDevLayer->getLiveReloadManager() : nullptr; }
#else
        /** @brief 정적 구성에는 핫 리로드 매니저가 없다 — 모듈 관리자는 정적 링크 표로 선다. */
        ILiveReloadManager* getLiveReloadManager() const { return nullptr; }
#endif
        /** @brief 모듈 관리자입니다. 호스트 로더가 돌기 전과 `stopHostServices` 뒤에는 nullptr 입니다. */
        GameModuleManager* getModuleManager() const { return _moduleManager.get(); }

    private:
        /**
         * @brief 호스트 범위입니다 — 엔진 범위의 부모입니다.
         * @details 모듈 라이브러리 창구(엔진의 `ModuleLoadStage` · `ModuleUnloadStage` 단계가 찾아 부른다)를 듭니다. 개발 도구는 이 범위에 호스트가 직접 넣습니다(호스트 종류 비트를 두지 않는다).
         */
        SubsystemScope _host;
        /** @brief 호스트 범위의 모듈 라이브러리 창구입니다(`_host` 소유 — 빌려 쓴다). */
        ModuleLoaderSubsystem* _pModuleLoader;
        /** @brief 개발 계층입니다. 엔진보다 앞에 선언해 나중에 사라진다. */
        unique_ptr<IHostDevLayer>     _pDevLayer;
        Engine                        _engine;
        unique_ptr<GameModuleManager> _moduleManager;
        /** @brief 모듈 매니페스트(`Bin/Modules`)와 그 해석입니다. 무엇을 어떤 순서로 로드할지 정합니다(개발 구성). */
        ModuleCatalog    _moduleCatalog;
        ModuleResolution _moduleResolution;
        /** @brief 엔진 뒤에 서고 엔진 앞에 내려가는 호스트 서비스입니다. 순서는 각 서비스 형식의 의존 선언이 정한다. */
        SubsystemCollection _hostServices;
    };
} // namespace sw
