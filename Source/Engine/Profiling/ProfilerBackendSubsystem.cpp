#include "pch.h"

#include "Engine/Profiling/ProfilerBackendSubsystem.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/ProfilerBackend.h"

namespace sw
{
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
    // 외부 프로파일러 출력을 엔진 범위가 사라진 뒤에 내린다. 프레임 프로파일러보다 먼저 내려간다(켜는 쪽 `ProfilerBackend::startBackend` 가 그것을 켰다).
    SW_ENGINE_SUBSYSTEM( ProfilerBackendSubsystem, Services, ( FrameProfiler ), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        (void)build;
        (void)scope.emplaceSubsystem<ProfilerBackendSubsystem>();
    } );
} // namespace sw
