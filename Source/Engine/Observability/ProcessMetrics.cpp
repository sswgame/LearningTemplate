#include "pch.h"

#include "Engine/Observability/ProcessMetrics.h"

#include "Core/Common/PlatformOSHeaders.h"

#include "Engine/Observability/MetricRegistry.h"

#if SW_PLATFORM_WINDOWS
    #include <Psapi.h>
#elif SW_PLATFORM_LINUX
    #include "Core/Container/StringUtil.h"
    #include "Core/File/FileUtil.h"

    #include <sys/resource.h>
#endif

namespace sw
{
    ProcessMetrics::ProcessMetrics()
        : _pResidentGauge{ nullptr }
        , _pPeakResidentGauge{ nullptr }
    {
    }

    void ProcessMetrics::initialize( MetricRegistry* pRegistry, int64 startUnixMilliseconds )
    {
        if ( pRegistry == nullptr )
            return;
        _pResidentGauge     = pRegistry->registerGauge( "process_resident_memory_bytes", "Resident memory size in bytes" );
        _pPeakResidentGauge = pRegistry->registerGauge( "process_resident_memory_max_bytes", "Peak resident memory size in bytes since the process started" );
        MetricGauge* pStart = pRegistry->registerGauge( "process_start_time_seconds", "Start time of the process since the Unix epoch in seconds" );
        if ( pStart != nullptr )
            pStart->set( static_cast<float64>( startUnixMilliseconds ) / constant::kMillisecondsPerSecond );
        update();
    }

    void ProcessMetrics::update()
    {
        if ( _pResidentGauge == nullptr )
            return;
        uint64 residentBytes     = 0;
        uint64 peakResidentBytes = 0;
        if ( tryGetResidentBytes( residentBytes, peakResidentBytes ) == false )
            return;
        _pResidentGauge->set( static_cast<float64>( residentBytes ) );
        if ( _pPeakResidentGauge != nullptr )
            _pPeakResidentGauge->set( static_cast<float64>( peakResidentBytes ) );
    }

    bool ProcessMetrics::tryGetResidentBytes( uint64& outResidentBytes, uint64& outPeakResidentBytes )
    {
        outResidentBytes     = 0;
        outPeakResidentBytes = 0;
#if SW_PLATFORM_WINDOWS
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof( counters );
        if ( GetProcessMemoryInfo( GetCurrentProcess(), &counters, sizeof( counters ) ) == FALSE )
            return false;
        outResidentBytes     = static_cast<uint64>( counters.WorkingSetSize );
        outPeakResidentBytes = static_cast<uint64>( counters.PeakWorkingSetSize );
        return true;
#elif SW_PLATFORM_LINUX
        // statm 의 둘째 값이 상주 페이지 수다. 최대값은 getrusage 의 ru_maxrss(KiB)다.
        string statmText;
        if ( FileUtil::readStreamTextFile( "/proc/self/statm", statmText, constant::kMaxBuffer256 ) == false )
            return false;
        const string_view line{ statmText };
        const size_t      firstSpace = line.find( ' ' );
        if ( firstSpace == string_view::npos )
            return false;
        const string_view rest          = line.substr( firstSpace + 1 );
        int64             residentPages = 0;
        if ( StringUtil::parseInt64( rest.substr( 0, rest.find( ' ' ) ), residentPages ) == false || residentPages < 0 )
            return false;
        const int64 pageSize               = static_cast<int64>( sysconf( _SC_PAGESIZE ) );
        outResidentBytes                   = static_cast<uint64>( residentPages ) * static_cast<uint64>( pageSize > 0 ? pageSize : 0 );
        constexpr uint64 kBytesPerKibibyte = 1024; // getrusage 의 ru_maxrss 단위(KiB)
        struct rusage    usage{};
        if ( getrusage( RUSAGE_SELF, &usage ) == 0 )
            outPeakResidentBytes = static_cast<uint64>( usage.ru_maxrss ) * kBytesPerKibibyte;
        // 커널은 최대값(hiwater)을 메모리 사상이 바뀔 때만 갱신한다 — 막 늘어난 상주량이 기록된 최대보다 클 수 있다. 최대는 지금 값보다 작을 수 없다.
        if ( outPeakResidentBytes < outResidentBytes )
            outPeakResidentBytes = outResidentBytes;
        return true;
#else
        return false;
#endif
    }
} // namespace sw
