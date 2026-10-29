/**
 * @file DevToolsHost.h
 * @brief 개발 도구 단위(`DevTools` — 배포 빌드는 짓지 않는다)가 엔진에 붙는 자리입니다: 기동 단계에 서브시스템을 더하고, 프레임마다 그것을 돌립니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

#include "DevTools/DevToolsAPI.h"

#include "Engine/EngineFrameHooks.h"
#include "Engine/EngineStartupExtension.h"

namespace sw
{
    class ContentFolderWatchService;
    class DevConsoleHost;

    /**
     * @class DevToolsHost
     * @brief 호스트(App · Server)가 자기 범위에 두는 개발 도구의 진입점입니다. 엔진은 이것을 모르고 두 확장점으로만 만난다.
     * @details 엔진 확장점은 상속하지 않고 델리게이트로 붙는다 —
     *          - 기동 확장(`EngineStartupExtensionDelegate`): 셰이더 라이브 리로드(`LiveShaderSubsystem` — 렌더러가 있을 때)와 설정 파일 리로드(`ConfigReloadSubsystem`), 디버그 HUD(`DebugHUD` — UI 가 있을 때)를 기동 단계 범위에 더한다.
     *          - 프레임 훅(`EngineFrameHooks`): 씬 틱 뒤에 HUD 를 돌리고, 프레임 끝에 셰이더 재컴파일을 돌리고(Debug) 강제 리로드 단축키를 읽고, 설정 파일 변화를 반영한다.
     *          언리얼의 개발 모듈(HotReload · DirectoryWatcher · ShaderCompiler 의 재컴파일)이 엔진 바깥에서 엔진 확장점에 붙는 모양과 같다. 게임 스레드에서만 쓴다.
     */
    class SW_DEVTOOLS_API DevToolsHost final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "DevTools";

        DevToolsHost();
        ~DevToolsHost() override;
        DevToolsHost( const DevToolsHost& )            = delete;
        DevToolsHost& operator=( const DevToolsHost& ) = delete;

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /**
         * @brief 게임 창 개발 콘솔과 표준 입력 명령 채널을 세웁니다(App 이 호스트 서비스 `DevConsole` 자리에서 부른다). @p bEnableEditor 면 입력은 받지 않고 시작 명령만 돌린다.
         * @details 이후 프레임마다 관찰자 훅이 돌린다 — 프레임 앞에 시작 명령, 씬 틱 뒤에 콘솔 키, 프레임 끝에 표준 입력(종료 낱말이면 종료 코드 0).
         */
        void startConsole( bool bEnableEditor );
        /** @brief 콘솔을 내립니다(입력 관리자 · UI 보다 먼저 — 엔진 종료 전). 세우지 않았으면 아무것도 하지 않습니다. */
        void stopConsole();

        /** @brief 콘텐츠 폴더 감시 창구입니다 — 호스트가 범위에 `IContentFolderWatch` 로 건다(기능 · 게임의 데이터 다시 읽기가 찾는다). */
        ContentFolderWatchService& getContentFolderWatch() { return *_pContentFolderWatch; }

        /** @brief 기동 확장 — 엔진이 기동 단계 범위를 채운 뒤 · 초기화 전에 부른다(`makeStartupExtension`). */
        void addStartupSubsystems( SubsystemScope& startup, const engine::StartupParts& parts );
        /** @brief 엔진 초기화에 넘길 기동 확장입니다(`Engine::setStartupExtension`). */
        EngineStartupExtensionDelegate makeStartupExtension();
        /** @brief 엔진이 선 뒤 붙일 프레임 훅(프레임 앞 · 씬 틱 뒤 · 프레임 끝)입니다(`Engine::registerFrameHooks`). */
        EngineFrameHooks makeFrameHooks();

    private:
        void  onFrameBegin( const EngineFrameContext& context );
        void  onAfterSceneTick( const EngineFrameContext& context );
        int32 onFrameEnd( const EngineFrameContext& context );

    public:
    private:
        unique_ptr<DevConsoleHost>            _pConsole;
        unique_ptr<ContentFolderWatchService> _pContentFolderWatch; ///< `startConsole` 이 세운 콘솔(App 만 — 서버는 자기 명령 채널이 있다)
    };
} // namespace sw
