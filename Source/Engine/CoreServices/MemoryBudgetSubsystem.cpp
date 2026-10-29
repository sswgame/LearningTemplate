#include "pch.h"

#include "Engine/CoreServices/MemoryBudgetSubsystem.h"

#include "Core/File/FileUtil.h"

#include "Engine/CoreServices/ContentRootSubsystem.h"
#include "Engine/CoreServices/CrashHandlerSubsystem.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    MemoryBudgetSubsystem::MemoryBudgetSubsystem( const CrashHandlerSubsystem& crashHandler )
        : _crashHandler{ crashHandler }
        , _monitor{}
        , _bBudgetFileRead{ false }
    {
    }

    SubsystemInitResult MemoryBudgetSubsystem::initializeSubsystem()
    {
        // `-gv_memoryTracking=1` 은 기동의 할당부터 센다(Release 는 추적이 꺼진 채 선다).
        _monitor.applyTrackingSetting();
        return SubsystemInitResult::Succeeded;
    }

    void MemoryBudgetSubsystem::onFrameEnd()
    {
        if ( _bBudgetFileRead == false )
        {
            _bBudgetFileRead = true;
            if ( _crashHandler.isCrashReporterRun() == false )
                (void)_monitor.loadBudgetFile( FileUtil::joinPath( engine::getSubsystem<AssetPaths>().getProjectFolderPath(), MemoryBudgetMonitor::kBudgetFile ) ); // 틀린 표는 오류를 남기고 예산 없이 간다
        }
        _monitor.onFrameEnd();
    }

    span<const utf8* const> MemoryBudgetSubsystem::getSubsystemDependencies() const
    {
        // 예산 표는 게임 프로젝트 폴더에 있다 — 리소스 루트 · 게임을 정하는 단계 뒤에 선다.
        static constexpr const utf8* kArrDependency[] = { ContentRootSubsystem::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw
