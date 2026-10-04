/**
 * @file MemoryBudgetMonitor.cpp
 * @brief MemoryBudgetMonitor 구현입니다(예산 JSON · 프레임 검사 · 태그 표).
 */
#include "pch.h"

#include "Engine/Utility/Debug/MemoryBudgetMonitor.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Utility/Debug/FrameProfiler.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    namespace
    {
        SW_LOG_CALLER( "MemoryBudget" );

        struct MemoryBudgetMonitorInternal
        {
            /** @brief 바이트를 KB 의 10 배로 바꿉니다(소수 한 자리를 정수로 찍기 위해). */
            static constexpr uint64 toKilobytesX10( uint64 bytes ) { return ( bytes * 10 ) / 1024; }

            /** @brief 한 메가바이트입니다. */
            static constexpr uint64 kBytesPerMegabyte = 1024ull * 1024ull;

            /** @brief @p value 의 멤버가 @p arrAllowed 밖이면 그 이름을 @p outUnknown 에 담고 false 입니다. */
            template <size_t N>
            static bool hasOnlyKnownKeys( const JsonValue& value, const utf8* const ( &arrAllowed )[N], string& outUnknown )
            {
                for ( const string& memberName : value.getMemberNames() )
                {
                    bool bKnown = false;
                    for ( const utf8* pAllowed : arrAllowed )
                        bKnown = bKnown || memberName == pAllowed;
                    if ( bKnown == false )
                    {
                        outUnknown = memberName;
                        return false;
                    }
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /**
     * @brief `-gv_memoryReport=1`: 다음 프레임 끝에 태그 표(살아 있는 · 최고치 · 예산)를 한 번 남깁니다. 남긴 뒤 0 으로 돌아갑니다.
     * @details 에디터 · 스크립트가 값을 1 로 바꾸면 그 프레임에 다시 남깁니다(언리얼 `stat llm` · 콘솔 명령 자리).
     */
    SW_TEST_GLOBAL_VARIABLE_INT( gv_memoryReport, 0, "다음 프레임에 메모리 태그 표(살아 있는 · 최고치 · 예산)를 한 번 남김 (1=남기기)" );
    /**
     * @brief `-gv_memoryTracking=0|1`: 메모리 태그 추적을 끄거나 켭니다. -1 이면 구성 기본값(Debug 켜짐 · Release 꺼짐)입니다.
     * @details 꺼져 있으면 할당마다 분기 하나만 남습니다. 켜면 태그 · 최고치 · 예산 경고가 돕니다. 배포본에는 프로파일러가 없습니다.
     */
    SW_TEST_GLOBAL_VARIABLE_INT( gv_memoryTracking, -1, "메모리 태그 추적 (-1=구성 기본, 0=끄기, 1=켜기)" );

    MemoryBudgetMonitor::MemoryBudgetMonitor()
        : _arrTagCounterSlot{}
        , _liveCounterSlot{ FrameProfiler::kInvalidSlot }
        , _appliedTrackingSetting{ -1 }
    {
        _arrTagCounterSlot.fill( FrameProfiler::kInvalidSlot );
    }

    bool MemoryBudgetMonitor::applyBudgetJson( string_view jsonText, MemoryProfiler& profiler, string& outError )
    {
        JsonDocument document;
        if ( document.parse( jsonText, "MemoryBudget" ) == false )
        {
            outError = document.getLastError();
            return false;
        }

        const JsonValue          root = document.getRoot();
        string                   unknownKey;
        static const utf8* const kArrRootKey[]  = { "_listBudget" };
        static const utf8* const kArrEntryKey[] = { "_tag", "_megabytes" };
        if ( root.isObject() == false || MemoryBudgetMonitorInternal::hasOnlyKnownKeys( root, kArrRootKey, unknownKey ) == false )
        {
            outError = root.isObject() ? "unknown key '" + unknownKey + "'" : string( "the root must be an object" );
            return false;
        }

        const JsonValue list = root.get( "_listBudget" );
        if ( list.isValid() && list.isArray() == false )
        {
            outError = "_listBudget must be an array";
            return false;
        }

        // 먼저 모두 검사하고, 틀린 곳이 없을 때만 건다 — 반쯤 걸린 예산 표를 남기지 않는다.
        array<uint64, kMemoryTagCount> arrBudgetBytes{};
        const size_t                   entryCount = list.isValid() ? list.size() : 0;
        for ( size_t index = 0; index < entryCount; ++index )
        {
            const JsonValue entry = list.at( index );
            if ( entry.isObject() == false || MemoryBudgetMonitorInternal::hasOnlyKnownKeys( entry, kArrEntryKey, unknownKey ) == false )
            {
                outError = "budget entry " + to_string( index ) + ( entry.isObject() ? ": unknown key '" + unknownKey + "'" : string( " is not an object" ) );
                return false;
            }
            const string tagName = entry.get( "_tag" ).asString();
            MemoryTag    tag{ MemoryTag::Unknown };
            if ( MemoryProfiler::findMemoryTagByName( tagName, tag ) == false )
            {
                outError = "budget entry " + to_string( index ) + ": unknown memory tag '" + tagName + "'";
                return false;
            }
            const JsonValue megabytes = entry.get( "_megabytes" );
            if ( megabytes.isNumber() == false || megabytes.asFloat() <= 0.0 )
            {
                outError = "budget entry " + to_string( index ) + " (" + tagName + "): _megabytes must be a positive number";
                return false;
            }
            const uint32 tagIndex = static_cast<uint32>( tag );
            if ( arrBudgetBytes[tagIndex] != 0 )
            {
                outError = "budget entry " + to_string( index ) + ": tag '" + tagName + "' is listed twice";
                return false;
            }
            arrBudgetBytes[tagIndex] = static_cast<uint64>( megabytes.asFloat() * static_cast<float64>( MemoryBudgetMonitorInternal::kBytesPerMegabyte ) );
        }

        for ( uint32 tagIndex = 0; tagIndex < kMemoryTagCount; ++tagIndex )
            profiler.setBudget( static_cast<MemoryTag>( tagIndex ), arrBudgetBytes[tagIndex] );
        return true;
    }

    bool MemoryBudgetMonitor::loadBudgetFile( string_view absolutePath )
    {
        MemoryProfiler* pProfiler = MemoryProfiler::getActive();
        if ( pProfiler == nullptr || FileUtil::fileExists( absolutePath ) == false )
            return true;

        string text;
        if ( FileUtil::readTextFile( absolutePath, text ) == false )
        {
            SW_LOG_ERROR( "Cannot read the memory budget file '%#'", absolutePath );
            return false;
        }
        string error;
        if ( applyBudgetJson( text, *pProfiler, error ) == false )
        {
            SW_LOG_ERROR( "Invalid memory budget file '%#': %#", absolutePath, error.c_str() );
            return false;
        }
        return true;
    }

    void MemoryBudgetMonitor::applyTrackingSetting()
    {
        MemoryProfiler* pProfiler = MemoryProfiler::getActive();
        if ( pProfiler == nullptr || gv_memoryTracking == _appliedTrackingSetting )
            return;
        if ( gv_memoryTracking >= 0 )
        {
            pProfiler->setTrackingEnabled( gv_memoryTracking != 0 );
            // 꺼져 있는 동안의 할당은 세지 않았다 — 최고치는 켠 순간부터 다시 잰다.
            pProfiler->resetPeaks();
        }
        _appliedTrackingSetting = gv_memoryTracking;
    }

    void MemoryBudgetMonitor::onFrameEnd()
    {
        MemoryProfiler* pProfiler = MemoryProfiler::getActive();
        if ( pProfiler == nullptr )
            return;

        applyTrackingSetting();

        if ( pProfiler->isTrackingEnabled() == false )
            return;

        (void)pProfiler->reportExceededBudgets();
        if ( gv_memoryReport != 0 )
        {
            gv_memoryReport = 0; // 한 번만
            logMemoryReport( *pProfiler, "gv_memoryReport" );
        }
        addFrameProfilerCounters( *pProfiler );
    }

    void MemoryBudgetMonitor::addFrameProfilerCounters( const MemoryProfiler& profiler )
    {
        // 카운터 이름을 처음 등록할 때 이름 풀이 자란다 — 메인 루프에는 태그가 없으므로 여기서 건다(아니면 Unknown 줄로 간다).
        SW_MEMORY_SCOPE( EngineMisc );
        if ( engine::areEngineServicesBound() == false )
            return;
        FrameProfiler& frameProfiler = engine::getFrameProfiler();
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
            frameProfiler.addCount( _arrTagCounterSlot[tagIndex], profiler.getStats( tag )._currentAllocatedBytes.load( std::memory_order_relaxed ) / 1024 );
        }
    }

    void MemoryBudgetMonitor::logMemoryReport( [[maybe_unused]] const MemoryProfiler& profiler, [[maybe_unused]] const utf8* pTitle )
    {
        // 보고는 Info 로그로만 나간다. 그것이 사라지는 구성에는 본문도 두지 않는다.
#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )
        const uint64 liveBytes = profiler.getLiveAllocatedBytes();
        const uint64 liveCount = profiler.getLiveAllocationCount();
        const uint64 totalX10  = MemoryBudgetMonitorInternal::toKilobytesX10( liveBytes );
        SW_LOG_INFO( "[Memory] %# — by tag (live, sw allocator)  %#.%# KB in %# blocks   [live KB  share  blocks | peak KB  peak blocks | budget KB  use%%]", pTitle,
                     totalX10 / 10, totalX10 % 10, liveCount );
        for ( const MemoryTag tag : profiler.makeTagOrderByLiveBytes() )
        {
            const MemoryProfileStats& stats  = profiler.getStats( tag );
            const uint64              bytes  = stats._currentAllocatedBytes.load( std::memory_order_relaxed );
            const uint64              budget = stats._budgetBytes.load( std::memory_order_relaxed );
            if ( bytes == 0 && budget == 0 )
                continue;
            const uint64 kbX10        = MemoryBudgetMonitorInternal::toKilobytesX10( bytes );
            const uint64 peakX10      = MemoryBudgetMonitorInternal::toKilobytesX10( stats._peakAllocatedBytes.load( std::memory_order_relaxed ) );
            const uint64 sharePermill = liveBytes == 0 ? 0 : ( bytes * 1000 ) / liveBytes;
            if ( budget == 0 )
            {
                SW_LOG_INFO( "[Memory]   %#  %#.%# KB  %#.%#%%  %# blocks | peak %#.%# KB  %# blocks", MemoryProfiler::getMemoryTagName( tag ), kbX10 / 10, kbX10 % 10,
                             sharePermill / 10, sharePermill % 10, stats._currentAllocationCount.load( std::memory_order_relaxed ), peakX10 / 10, peakX10 % 10,
                             stats._peakAllocationCount.load( std::memory_order_relaxed ) );
                continue;
            }
            const uint64 usePermill = ( bytes * 1000 ) / budget;
            SW_LOG_INFO( "[Memory]   %#  %#.%# KB  %#.%#%%  %# blocks | peak %#.%# KB  %# blocks | budget %# KB  %#.%#%%%#", MemoryProfiler::getMemoryTagName( tag ),
                         kbX10 / 10, kbX10 % 10, sharePermill / 10, sharePermill % 10, stats._currentAllocationCount.load( std::memory_order_relaxed ), peakX10 / 10,
                         peakX10 % 10, stats._peakAllocationCount.load( std::memory_order_relaxed ), budget / 1024, usePermill / 10, usePermill % 10,
                         bytes > budget ? "  OVER" : "" );
        }

        const uint64 platformBytes = MemoryProfiler::getPlatformHeapBytes();
        const uint64 swBlockBytes  = liveBytes + liveCount * Memory::getAllocationHeaderSize();
        if ( platformBytes > swBlockBytes )
        {
            const uint64 outsideX10 = MemoryBudgetMonitorInternal::toKilobytesX10( platformBytes - swBlockBytes );
            SW_LOG_INFO( "[Memory]   (outside the sw allocator — std::allocator · third-party · static init)  %#.%# KB", outsideX10 / 10, outsideX10 % 10 );
        }
#endif
    }
} // namespace sw
