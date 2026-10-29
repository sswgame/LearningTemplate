/**
 * @file DevToolsHost.h
 * @brief 개발 도구 단위(`DevTools` — 배포 빌드는 짓지 않는다)가 엔진에 붙는 자리입니다: 기동 단계에 서브시스템을 더하고, 프레임마다 그것을 돌립니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"

#include "DevTools/DevToolsAPI.h"

#include "Engine/EngineFrameObserver.h"
#include "Engine/EngineStartupExtension.h"

namespace sw
{
    class Engine;

    /**
     * @class DevToolsHost
     * @brief 호스트(App · Server)가 자기 범위에 두는 개발 도구의 진입점입니다. 엔진은 이것을 모르고 두 확장점으로만 만난다.
     * @details - 기동 확장(`IEngineStartupExtension`): 셰이더 라이브 리로드(`LiveShaderSubsystem` — 렌더러가 있을 때)와 설정 파일 리로드(`ConfigReloadSubsystem`), 디버그 HUD(`DebugHUD` — UI 가 있을 때)를 기동 단계 범위에 더한다.
     *          - 프레임 관찰자(`IEngineFrameObserver`): 씬 틱 뒤에 HUD 를 돌리고, 프레임 끝에 셰이더 재컴파일을 돌리고(Debug) 강제 리로드 단축키를 읽고, 설정 파일 변화를 반영한다.
     *          언리얼의 개발 모듈(HotReload · DirectoryWatcher · ShaderCompiler 의 재컴파일)이 엔진 바깥에서 엔진 확장점에 붙는 모양과 같다. 게임 스레드에서만 쓴다.
     */
    class SW_DEVTOOLS_API DevToolsHost final : public ISubsystem, public IEngineStartupExtension, public IEngineFrameObserver
    {
    public:
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "DevTools";

        DevToolsHost();
        ~DevToolsHost() override;
        DevToolsHost( const DevToolsHost& )            = delete;
        DevToolsHost& operator=( const DevToolsHost& ) = delete;

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override { _pEngine = nullptr; }
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 단축키를 읽을 엔진입니다(디버그 키 맵은 엔진이 든다). 엔진이 선 뒤 · 프레임 관찰자로 붙이기 전에 겁니다. nullptr 이면 단축키를 읽지 않습니다. */
        void setEngine( const Engine* pEngine ) { _pEngine = pEngine; }

        // IEngineStartupExtension
        void addStartupSubsystems( SubsystemScope& startup, const engine::StartupParts& parts ) override;

        // IEngineFrameObserver
        void                onFrameBegin( const EngineFrameContext& /*context*/ ) override {}
        void                onAfterSceneTick( const EngineFrameContext& context ) override;
        [[nodiscard]] int32 onFrameEnd( const EngineFrameContext& context ) override;
        [[nodiscard]] int32 onWindowClosed( const EngineFrameContext& /*context*/ ) override { return kRunning; }

    private:
        const Engine* _pEngine;
    };
} // namespace sw
