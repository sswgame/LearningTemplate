#include "pch.h"

#include "Engine/Profiling/ProfilerBackendSubsystem.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/ProfilerBackend.h"

namespace sw
{
    ProfilerBackendSubsystem::ProfilerBackendSubsystem( FrameProfiler& frameProfiler )
        : _frameProfiler{ frameProfiler }
    {
    }

    SubsystemInitResult ProfilerBackendSubsystem::initializeSubsystem()
    {
        if ( ProfilerBackend::initialize() )
            _frameProfiler.setEnabled( true ); // 카운터 그래프 · GPU 타임스탬프는 엔진 프로파일러 경로를 탄다
        return SubsystemInitResult::Succeeded;
    }

    void ProfilerBackendSubsystem::shutdownSubsystem()
    {
        ProfilerBackend::shutdown();
    }

    span<const utf8* const> ProfilerBackendSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { FrameProfiler::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw

namespace sw
{
    // 서브시스템 자리 등록(Engine/Foundation/EngineSubsystemRegistration.h) — 만드는 순서는 자리 표가 정한다.
    // 외부 프로파일러(`-gv_tracy`) — 엔진 범위보다 먼저 켜져 기동 단계부터 타임라인에 남는다. 프레임 프로파일러는 앞 자리다.
    SW_ENGINE_SUBSYSTEM( ProfilerBackendSubsystem, []( SubsystemScope& scope, engine::EngineSubsystemBuild& build )
    {
        (void)build;
        (void)scope.emplaceSubsystem<ProfilerBackendSubsystem>( scope.get<FrameProfiler>() );
    } );
} // namespace sw
