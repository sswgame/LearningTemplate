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
    // 외부 프로파일러(`-gv_tracy`) — 엔진 범위보다 먼저 켜져 기동 단계부터 타임라인에 남는다. 공장이 프레임 프로파일러를 찾으므로 그것을 먼저 만든다.
    SW_ENGINE_SUBSYSTEM( ProfilerBackendSubsystem, Services, ( FrameProfiler ), []( SubsystemScope& scope, engine::EngineSubsystemBuild& build )
    {
        (void)build;
        (void)scope.emplaceSubsystem<ProfilerBackendSubsystem>( scope.get<FrameProfiler>() );
    } );
} // namespace sw
