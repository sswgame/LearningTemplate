#include "pch.h"

#include "Engine/UserSettings/HardwareProbe.h"

#include "Core/Common/PlatformOsHeaders.h"

namespace sw
{
    HardwareProbeResult HardwareProbe::probe()
    {
        HardwareProbeResult result;
        result._logicalCoreCount = std::thread::hardware_concurrency();

#if defined( SW_PLATFORM_WINDOWS )
        MEMORYSTATUSEX memoryStatus{};
        memoryStatus.dwLength = sizeof( memoryStatus );
        if ( GlobalMemoryStatusEx( &memoryStatus ) != FALSE )
            result._systemMemoryMb = static_cast<uint32>( memoryStatus.ullTotalPhys / ( 1024ull * 1024ull ) );
#elif defined( SW_PLATFORM_LINUX )
        const int64 pageCount = static_cast<int64>( sysconf( _SC_PHYS_PAGES ) );
        const int64 pageSize  = static_cast<int64>( sysconf( _SC_PAGE_SIZE ) );
        if ( pageCount > 0 && pageSize > 0 )
            result._systemMemoryMb = static_cast<uint32>( ( pageCount / 1024 ) * pageSize / 1024 );
#endif
        return result;
    }
} // namespace sw
