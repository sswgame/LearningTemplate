/**
 * @file CrashInjectionSubsystem.h
 * @brief `-gv_crashTest=N` 이면 기동 때(렌더러 뒤) 일부러 죽는 개발 서브시스템입니다. 클라이언트 전용(`DevTools/Client`) — 렌더러를 기다린다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    /**
     * @class CrashInjectionSubsystem
     * @brief `-gv_crashTest=N` 이면 기동 때 일부러 죽습니다(`CrashInjectionKind`). 렌더러(RHI) 뒤에 서므로 백엔드 이름이 크래시 문맥에 들어간 뒤다.
     * @details 예전에는 런타임 `RHISubsystem` 이 이 전역 변수를 읽고 죽었다 — 개발 기능이 런타임 기동 경로에 있었다. 개발 도구 클라이언트가 붙인다(`DevToolsClient`).
     */
    class SW_DEVTOOLS_API CrashInjectionSubsystem final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName      = "CrashInjection";
        static constexpr MemoryTag   kSubsystemMemoryTag = MemoryTag::EngineMisc;

        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override {}
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
        SubsystemHostMask       getSubsystemHost() const override;
    };
} // namespace sw
