#include "pch.h"

#include "Engine/Profiling/ProfilerBackendSubsystem.h"

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
