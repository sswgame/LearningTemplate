/**
 * @file MemoryBudgetMonitor.cpp
 * @brief MemoryBudgetMonitor 구현입니다(예산 JSON · 프레임 검사 · 태그 표).
 */
#include "pch.h"

#include "Engine/Profiling/MemoryBudgetMonitor.h"

#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    namespace
    {
        /** @brief 바이트를 KB 의 10 배로 바꿉니다(소수 한 자리를 정수로 찍기 위해). */
        [[maybe_unused]] constexpr uint64 toKilobytesX10( uint64 bytes )
        {
            return ( bytes * 10 ) / constant::memory::kBytesPerKilobyte;
        }
    } // namespace
} // namespace sw

namespace sw
{
    MemoryBudgetMonitor::MemoryBudgetMonitor()
        : _arrTagCounterSlot{}
        , _liveCounterSlot{ FrameProfiler::kInvalidSlot }
    {
        _arrTagCounterSlot.fill( FrameProfiler::kInvalidSlot );
    }

    bool MemoryBudgetMonitor::applyBudgetJSON( string_view jsonText, MemoryProfiler& profiler, string& outError )
    {
        // 키는 대소문자까지 맞아야 하고, 모르는 키 · 읽지 못한 값 · 범위 밖 숫자는 오류다(`readConfigJSON` 이 어느 키인지 로그로 알린다).
        MemoryBudgetFile file;
        if ( ConfigManager::readConfigJSON( file, jsonText, "MemoryBudget" ) == false )
        {
            outError = "the budget file is not a JSON object, has an unknown key or an unreadable value (the log names the key)";
            return false;
        }

        // 먼저 모두 검사하고, 틀린 곳이 없을 때만 건다 — 반쯤 걸린 예산 표를 남기지 않는다.
        array<uint64, kMemoryTagCount> arrBudgetBytes{};
        for ( size_t index = 0; index < file._listBudget.size(); ++index )
        {
            const MemoryBudgetEntry& entry = file._listBudget[index];
            MemoryTag                tag{ MemoryTag::Unknown };
            if ( MemoryProfiler::findMemoryTagByName( entry._tag, tag ) == false )
            {
                outError = formatToString( "budget entry %#: unknown memory tag '%#'", index, entry._tag );
                return false;
            }
            if ( entry._megabytes <= 0.0 )
            {
                outError = formatToString( "budget entry %# (%#): _megabytes must be a positive number", index, entry._tag );
                return false;
            }
            const uint32 tagIndex = static_cast<uint32>( tag );
            if ( arrBudgetBytes[tagIndex] != 0 )
            {
                outError = formatToString( "budget entry %#: tag '%#' is listed twice", index, entry._tag );
                return false;
            }
            arrBudgetBytes[tagIndex] = static_cast<uint64>( entry._megabytes * static_cast<float64>( constant::memory::kBytesPerMegabyte ) );
        }

        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
        {
            profiler.setBudget( static_cast<MemoryTag>( tagIndex ), arrBudgetBytes[tagIndex] );
        }
        return true;
    }

    bool MemoryBudgetMonitor::loadBudgetFile( string_view absolutePath )
    {
        MemoryProfiler* pProfiler = MemoryProfiler::getActive();
        if ( pProfiler == nullptr || FileUtil::exists( absolutePath ) == false )
            return true;

        string text;
        if ( FileUtil::readTextFile( absolutePath, text ) == false )
        {
            SW_LOG_ERROR_ID( "MemoryBudget.CannotReadMemoryBudget", "Cannot read the memory budget file '%#'", absolutePath );
            return false;
        }
        string error;
        if ( applyBudgetJSON( text, *pProfiler, error ) == false )
        {
            SW_LOG_ERROR_ID( "MemoryBudget.InvalidMemoryBudgetFile", "Invalid memory budget file '%#': %#", absolutePath, error.c_str() );
            return false;
        }
        return true;
    }

    void MemoryBudgetMonitor::onFrameEnd()
    {
        MemoryProfiler* pProfiler = MemoryProfiler::getActive();
        if ( pProfiler == nullptr )
            return;

        if ( pProfiler->isTrackingEnabled() == false )
            return;

        (void)pProfiler->reportExceededBudgets();
        addFrameProfilerCounters( *pProfiler );
    }

    void MemoryBudgetMonitor::addFrameProfilerCounters( const MemoryProfiler& profiler )
    {
        // 카운터 이름을 처음 등록할 때 이름 풀이 자란다 — 메인 루프에는 태그가 없으므로 여기서 건다(아니면 Unknown 줄로 간다).
        SW_MEMORY_SCOPE( EngineMisc );
        if ( engine::areEngineServicesBound() == false )
            return;
        FrameProfiler& frameProfiler = engine::getSubsystem<FrameProfiler>();
        if ( frameProfiler.isEnabled() == false )
            return;

        if ( _liveCounterSlot == FrameProfiler::kInvalidSlot )
            _liveCounterSlot = frameProfiler.registerScope( "Mem.LiveKB" );
        frameProfiler.addCount( _liveCounterSlot, profiler.getLiveAllocatedBytes() / 1024 );

        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
        {
            const MemoryTag tag = static_cast<MemoryTag>( tagIndex );
            if ( profiler.getBudget( tag ) == 0 )
                continue;
            if ( _arrTagCounterSlot[tagIndex] == FrameProfiler::kInvalidSlot )
            {
                const string scopeName       = string( "Mem." ) + MemoryProfiler::getMemoryTagName( tag ) + "KB";
                _arrTagCounterSlot[tagIndex] = frameProfiler.registerScope( scopeName.c_str() );
            }
            frameProfiler.addCount( _arrTagCounterSlot[tagIndex], profiler.getStats( tag )._currentAllocatedBytes / 1024 );
        }
    }

    void MemoryBudgetMonitor::logMemoryReport( [[maybe_unused]] const MemoryProfiler& profiler, [[maybe_unused]] const utf8* pTitle )
    {
        // 보고는 Info 로그로만 나간다. 그것이 사라지는 구성에는 본문도 두지 않는다.
#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )
        const uint64 liveBytes = profiler.getLiveAllocatedBytes();
        const uint64 liveCount = profiler.getLiveAllocationCount();
        const uint64 totalX10  = toKilobytesX10( liveBytes );
        SW_LOG_INFO_ID( "MemoryBudget.TagLiveSwAllocator", "[Memory] %# — by tag (live, sw allocator)  %#.%# KB in %# blocks   [live KB  share  blocks | peak KB  peak blocks | budget KB  use%%]", pTitle,
                        totalX10 / 10, totalX10 % 10, liveCount );
        for ( const MemoryTag tag : profiler.makeTagOrderByLiveBytes() )
        {
            const MemoryProfileStats& stats  = profiler.getStats( tag );
            const uint64              bytes  = stats._currentAllocatedBytes;
            const uint64              budget = stats._budgetBytes;
            if ( bytes == 0 && budget == 0 )
                continue;
            const uint64 kbX10        = toKilobytesX10( bytes );
            const uint64 peakX10      = toKilobytesX10( stats._peakAllocatedBytes );
            const uint64 sharePermill = liveBytes == 0 ? 0 : ( bytes * 1000 ) / liveBytes;
            if ( budget == 0 )
            {
                SW_LOG_INFO_ID( "MemoryBudget.KBBlocksPeakKB", "[Memory]   %#  %#.%# KB  %#.%#%%  %# blocks | peak %#.%# KB  %# blocks", MemoryProfiler::getMemoryTagName( tag ), kbX10 / 10, kbX10 % 10,
                                sharePermill / 10, sharePermill % 10, stats._currentAllocationCount, peakX10 / 10, peakX10 % 10,
                                stats._peakAllocationCount );
                continue;
            }
            const uint64 usePermill = ( bytes * 1000 ) / budget;
            SW_LOG_INFO_ID( "MemoryBudget.KBBlocksPeakKB2", "[Memory]   %#  %#.%# KB  %#.%#%%  %# blocks | peak %#.%# KB  %# blocks | budget %# KB  %#.%#%%%#", MemoryProfiler::getMemoryTagName( tag ),
                            kbX10 / 10, kbX10 % 10, sharePermill / 10, sharePermill % 10, stats._currentAllocationCount, peakX10 / 10,
                            peakX10 % 10, stats._peakAllocationCount, budget / 1024, usePermill / 10, usePermill % 10,
                            bytes > budget ? "  OVER" : "" );
        }

        const uint64 platformBytes = MemoryProfiler::getPlatformHeapBytes();
        const uint64 swBlockBytes  = liveBytes + liveCount * Memory::getAllocationHeaderSize();
        if ( platformBytes > swBlockBytes )
        {
            const uint64 outsideX10 = toKilobytesX10( platformBytes - swBlockBytes );
            SW_LOG_INFO_ID( "MemoryBudget.OutsideSwAllocatorStd", "[Memory]   (outside the sw allocator — std::allocator · third-party · static init)  %#.%# KB", outsideX10 / 10, outsideX10 % 10 );
        }

        uint64 platformTotalBytes{ 0 };
        uint64 platformRequestCount{ 0 };
        if ( MemoryProfiler::getPlatformHeapTotals( platformTotalBytes, platformRequestCount ) )
        {
            // sw 블록도 CRT 에서 온다(헤더 포함). 추적을 켜기 전(기동 맨 앞)의 sw 할당은 밖 몫으로 세인다 — 근사다.
            const uint64 swCount      = profiler.getTotalAllocationCount();
            const uint64 swBytes      = profiler.getTotalAllocatedBytes() + swCount * Memory::getAllocationHeaderSize();
            const uint64 outsideCount = platformRequestCount > swCount ? platformRequestCount - swCount : 0;
            const uint64 outsideX10   = toKilobytesX10( platformTotalBytes > swBytes ? platformTotalBytes - swBytes : 0 );
            SW_LOG_INFO_ID( "MemoryBudget.OutsideSwAllocatorCumulative", "[Memory]   (outside the sw allocator, cumulative since start)  %#.%# KB in %# allocations", outsideX10 / 10, outsideX10 % 10, outsideCount );
        }
#endif
    }
} // namespace sw

namespace sw
{
    namespace
    {
        /**
         * @brief 메모리 태그 개발 명령입니다(배포본에는 등록되지 않는다 — `SW_DEV_COMMAND`). 언리얼 `stat llm` · `memreport` 콘솔 명령 자리.
         * @details 예전에는 테스트용 변수 둘(`-gv_memoryReport` · `-gv_memoryTracking`)을 감시가 프레임마다 읽었다. 묻는 쪽이 부르는 명령으로 바꿔 런타임 프레임에서 개발 분기를 뺐다.
         *          시작부터는 `-gv_devConsoleExec=memory.tracking on`(시작 씬이 열린 뒤 — 기동 중 할당까지 세려면 Debug 구성을 쓴다).
         */
        struct MemoryDevCommandsInternal
        {
            /** @brief `memory.report`: 태그 표(살아 있는 · 최고치 · 예산)를 로그로 한 번 남깁니다. */
            static bool runReport( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                const MemoryProfiler* pProfiler = MemoryProfiler::getActive();
                if ( pProfiler == nullptr )
                {
                    outReply = "memory: no memory profiler in this build";
                    return true;
                }
                MemoryBudgetMonitor::logMemoryReport( *pProfiler, "memory.report" );
                outReply = pProfiler->isTrackingEnabled() ? "memory: tag table logged" : "memory: tag table logged - tracking is off, the counts did not move (memory.tracking on)";
                return true;
            }

            /** @brief `memory.tracking [on|off]`: 메모리 태그 추적을 켜고 끕니다(인자가 없으면 지금 상태를 답한다). 켜거나 끄면 최고치를 다시 잰다 — 꺼진 동안의 할당은 세지 않았다. */
            static bool runTracking( const vector<string>& listArgument, string& outReply )
            {
                MemoryProfiler* pProfiler = MemoryProfiler::getActive();
                if ( pProfiler == nullptr )
                {
                    outReply = "memory: no memory profiler in this build";
                    return true;
                }
                if ( listArgument.size() > 1 )
                    return false;
                if ( listArgument.size() == 1 )
                {
                    bool bOn = false;
                    if ( StringUtil::tryParseBool( listArgument[0], bOn ) == false )
                        return false;
                    pProfiler->setTrackingEnabled( bOn );
                    pProfiler->resetPeaks();
                }
                outReply = pProfiler->isTrackingEnabled() ? "memory tracking: on" : "memory tracking: off";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( MemoryReport, "memory.report", "memory.report", "Log the memory tag table once (live, peak, budget)", &MemoryDevCommandsInternal::runReport );
    SW_DEV_COMMAND( MemoryTracking, "memory.tracking", "memory.tracking [on|off]", "Show or switch memory tag tracking (peaks restart when switched)", &MemoryDevCommandsInternal::runTracking );
} // namespace sw
