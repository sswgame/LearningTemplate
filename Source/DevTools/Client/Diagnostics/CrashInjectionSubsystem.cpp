#include "pch.h"

#include "DevTools/Client/Diagnostics/CrashInjectionSubsystem.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "DevTools/Diagnostics/CrashInjection.h"

#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Renderer/RHISubsystem.h"

namespace sw
{
    /**
     * @brief `-gv_crashTest=N`: 기동 때(렌더러 뒤) 일부러 크래시를 냅니다(리포트 경로 검증용). N 은 `CrashInjectionKind` 입니다.
     * @details 크래시 리포트 경로를 실제로 확인하는 유일한 방법이다(리포트는 크래시가 나야만 만들어진다).
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_crashTest, 0, "일부러 크래시를 내 리포트 경로를 검증합니다 (1=널 쓰기 2=스택 오버플로 3=작업 스레드 스택 오버플로 4=abort 5=순수 가상 호출)" );

    SubsystemInitResult CrashInjectionSubsystem::initializeSubsystem()
    {
        if ( gv_crashTest != 0 )
            CrashInjector::crash( static_cast<CrashInjectionKind>( gv_crashTest ) );
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> CrashInjectionSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { RHISubsystem::kSubsystemName };
        return kArrDependency;
    }

    SubsystemHostMask CrashInjectionSubsystem::getSubsystemHost() const
    {
        return EngineSubsystemHost::kClient;
    }
} // namespace sw
