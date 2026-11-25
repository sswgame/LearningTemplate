#include "pch.h"

#include "DevTools/Profiling/Memory/MemoryExportSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Log/Logger.h"
#include "Core/Task/TaskManager.h"

#include "DevTools/Profiling/Memory/MemorySnapshotExport.h"
#include "DevTools/Profiling/Memory/MemorySnapshotSources.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/EngineSubsystems.h"

namespace sw
{
    namespace
    {
        struct MemoryExportSubsystemInternal
        {
            /** @brief 남은 쓰기를 시나리오 끝(`<Pass/>`)과 에디터 정지 대기가 기다린다 — 쓰기가 끝나기 전에 파일을 읽는 단계가 오지 않게. */
            static AutomationReadiness readPendingWrites()
            {
                const MemoryExportSubsystem* pExport = engine::areEngineSubsystemsBound() ? engine::findSubsystem<MemoryExportSubsystem>() : nullptr;
                if ( pExport == nullptr )
                    return AutomationReadiness::notApplicable();
                const uint32 pendingCount = pExport->getPendingWriteCount();
                if ( pendingCount == 0 )
                    return AutomationReadiness::ready();
                return AutomationReadiness::waiting( pendingCount, "memory exports are writing CSV files" );
            }

            /** @brief 감시 값 `Memory.ExportPendingWrites` — 워커에서 쓰는 중인 메모리 내보내기 수입니다. */
            [[nodiscard]] static bool readPendingWriteWatch( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MemoryExportSubsystem* pExport = engine::findSubsystem<MemoryExportSubsystem>();
                if ( pExport == nullptr ) // 개발 호스트는 늘 세운다 — 없으면 버그
                {
                    SW_LOG_ERROR_ID( "MemoryExport.WatchSubsystemMissing", "[Scenario] watch Memory.ExportPendingWrites: MemoryExportSubsystem is missing" );
                    return false;
                }
                outValue = static_cast<float64>( pExport->getPendingWriteCount() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( memoryTagCount, "Memory.TagCount", "Memory tags this build counts (the rows of a Tags sheet)", []( const GameObjectManager* pManager, float64& outValue )
    {
        (void)pManager;
        outValue = static_cast<float64>( kMemoryTagCount );
        return true;
    } );
    SW_AUTOMATION_FINISH_BLOCKER( memoryExportWrites, "MemoryExportWrites", &MemoryExportSubsystemInternal::readPendingWrites );
    SW_AUTOMATION_WATCH( memoryExportPendingWrites, "Memory.ExportPendingWrites", "Memory snapshot CSV exports still being written on a worker",
                         &MemoryExportSubsystemInternal::readPendingWriteWatch );

    MemoryExportSubsystem::MemoryExportSubsystem()
        : _writeStage{}
    {
    }

    void MemoryExportSubsystem::shutdownSubsystem()
    {
        if ( _writeStage.isValid() )
            engine::getSubsystem<TaskManager>().waitStage( _writeStage, "MemoryExportSubsystem::shutdown" );
    }

    uint32 MemoryExportSubsystem::getPendingWriteCount() const
    {
        if ( _writeStage.isValid() == false )
            return 0;
        return engine::getSubsystem<TaskManager>().getStagePendingCount( _writeStage );
    }

    void MemoryExportSubsystem::writeAsync( shared_ptr<const MemorySnapshot> pSnapshot, const AbsolutePath& folder )
    {
        const auto writeNow = [pSnapshot, folder]()
        {
            const expected<MemoryExportResult> result = MemorySnapshotExport::writeSnapshot( *pSnapshot, folder );
            if ( result.has_value() == false )
                SW_LOG_WARNING_ID( "MemoryExport.Failed", "memory export: %# (%#)", result.error().message(), result.error().detail() );
        };
        TaskManager& taskManager = engine::getSubsystem<TaskManager>();
        TaskHandle   handle      = taskManager.emplaceTask( "MemoryExportWrite", SW_DELEGATE_LAMBDA( TaskDelegate, writeNow ) );
        handle.setPriority( TaskPriority::Low );
        taskManager.addToStage( _writeStage, handle, "MemoryExportWrite" ); // 제출 전에 넣는다
        handle.submit();
    }

    void MemoryExportSubsystem::exportOnLoopEnd()
    {
        const CommandLineManager* pCommandLine = engine::findSubsystem<CommandLineManager>();
        AbsolutePath              root;
        if ( pCommandLine == nullptr || pCommandLine->getArgument( CommandLineArgument::MEMORY_EXPORT, root ) == false )
            return;
        const expected<MemorySnapshotPolicy> policy = MemorySnapshotPolicy::load();
        if ( policy.has_value() == false )
        {
            SW_LOG_ERROR_ID( "MemoryExport.NoPolicy", "-memory-export: %# (%#)", policy.error().message(), policy.error().detail() );
            return;
        }
        const AbsolutePath folder = MemorySnapshotExport::reserveFolder( root );
        if ( folder.empty() )
            return; // 이유는 `FileUtil::reserveUniquePath` 가 남겼다
        const MemorySnapshot               snapshot = MemorySnapshotSources::makeEngine( policy->_allocationSiteLimit ).collect( MemorySnapshot::makeTimeLabel(), "local" );
        const expected<MemoryExportResult> result   = MemorySnapshotExport::writeSnapshot( snapshot, folder );
        if ( result.has_value() == false )
            SW_LOG_ERROR_ID( "MemoryExport.LoopEndFailed", "-memory-export: %# (%#)", result.error().message(), result.error().detail() );
    }
} // namespace sw
