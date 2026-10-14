#include "pch.h"

#include "Engine/UserSettings/HardwareProbe.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"
#include "Core/File/FileUtil.h"

#include "Engine/Common/EnginePlatformHeaders.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct HardwareProbeInternal
        {
            /** @brief 해상도 목록에 넣는 가장 작은 변(픽셀)입니다. 옛 모드(640x480 등)는 옵션 메뉴에 필요 없다. */
            static constexpr int32 kMinResolutionSide = 640;

            static bool isResolutionBefore( const int2& left, const int2& right )
            {
                return left._x != right._x ? left._x < right._x : left._y < right._y;
            }

#if defined( SW_PLATFORM_WINDOWS )
            using PfnCreateDXGIFactory1 = HRESULT( WINAPI* )( REFIID, void** );

            /** @brief 기본 어댑터(RHI 가 고르는 0 번)의 이름과 전용 메모리를 읽습니다. dxgi.dll 을 링크하지 않고 올려 쓴다 — Engine 은 DXGI 를 링크하지 않는다. */
            static void probeAdapter( HardwareProbeResult& result )
            {
                HMODULE hModule = GetModuleHandleW( L"dxgi.dll" );
                if ( hModule == nullptr )
                    hModule = LoadLibraryExW( L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 );
                if ( hModule == nullptr )
                    return;
                const PfnCreateDXGIFactory1 pfnCreateFactory = reinterpret_cast<PfnCreateDXGIFactory1>( reinterpret_cast<void*>( GetProcAddress( hModule, "CreateDXGIFactory1" ) ) );
                if ( pfnCreateFactory == nullptr )
                    return;
                Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
                if ( FAILED( pfnCreateFactory( IID_PPV_ARGS( factory.GetAddressOf() ) ) ) )
                    return;
                Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
                if ( FAILED( factory->EnumAdapters1( 0, adapter.GetAddressOf() ) ) || adapter == nullptr )
                    return;
                DXGI_ADAPTER_DESC1 desc{};
                if ( FAILED( adapter->GetDesc1( &desc ) ) )
                    return;
                result._gpuName = StringUtil::utf16ToUtf8( desc.Description );
                if ( ( desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE ) == 0 )
                    result._gpuMemoryMb = static_cast<uint32>( desc.DedicatedVideoMemory / constant::kBytesPerMegabyte );
            }
#elif defined( SW_PLATFORM_LINUX )
            /** @brief amdgpu 드라이버가 sysfs 에 내는 전용 비디오 메모리(바이트)를 읽습니다. 다른 드라이버는 아직 읽지 않는다(0). */
            static void probeAdapter( HardwareProbeResult& result )
            {
                for ( int32 cardIndex = 0; cardIndex < 8; ++cardIndex )
                {
                    string text;
                    utf8   arrPath[constant::kMaxBuffer128]{};
                    formatstring( arrPath, constant::kMaxBuffer128, "/sys/class/drm/card%#/device/mem_info_vram_total", cardIndex );
                    if ( FileUtil::readTextFile( arrPath, text ) == false )
                        continue;
                    int64 bytes{ 0 };
                    if ( StringUtil::parseInt64( StringUtil::trim( text ), bytes ) && bytes > 0 )
                    {
                        result._gpuMemoryMb = static_cast<uint32>( bytes / static_cast<int64>( constant::kBytesPerMegabyte ) );
                        return;
                    }
                }
            }
#else
            static void probeAdapter( HardwareProbeResult& /*result*/ )
            {
            }
#endif
        };
    } // namespace

    HardwareProbeResult HardwareProbe::probe()
    {
        HardwareProbeResult result;
        result._logicalCoreCount = std::thread::hardware_concurrency();

#if defined( SW_PLATFORM_WINDOWS )
        MEMORYSTATUSEX memoryStatus{};
        memoryStatus.dwLength = sizeof( memoryStatus );
        if ( GlobalMemoryStatusEx( &memoryStatus ) != FALSE )
            result._systemMemoryMb = static_cast<uint32>( memoryStatus.ullTotalPhys / constant::kBytesPerMegabyte );
#elif defined( SW_PLATFORM_LINUX )
        const int64 pageCount = static_cast<int64>( sysconf( _SC_PHYS_PAGES ) );
        const int64 pageSize  = static_cast<int64>( sysconf( _SC_PAGE_SIZE ) );
        if ( pageCount > 0 && pageSize > 0 )
            result._systemMemoryMb = static_cast<uint32>( ( pageCount / 1024 ) * pageSize / 1024 );
#endif
        HardwareProbeInternal::probeAdapter( result );
        return result;
    }

    void HardwareProbe::probeDisplayResolutions( vector<int2>& outListResolution )
    {
        outListResolution.clear();
#if defined( SW_PLATFORM_WINDOWS )
        DEVMODEW mode{};
        mode.dmSize = sizeof( mode );
        for ( DWORD modeIndex = 0; EnumDisplaySettingsW( nullptr, modeIndex, &mode ) != FALSE; ++modeIndex )
        {
            if ( mode.dmBitsPerPel < 32 )
                continue;
            const int2 resolution{ static_cast<int32>( mode.dmPelsWidth ), static_cast<int32>( mode.dmPelsHeight ) };
            if ( resolution._x < HardwareProbeInternal::kMinResolutionSide || resolution._y < HardwareProbeInternal::kMinResolutionSide )
                continue;
            bool bKnown = false;
            for ( const int2& known : outListResolution )
            {
                bKnown = bKnown || ( known._x == resolution._x && known._y == resolution._y );
            }
            if ( bKnown == false )
                outListResolution.push_back( resolution );
        }
        std::sort( outListResolution.begin(), outListResolution.end(), &HardwareProbeInternal::isResolutionBefore );
#endif
    }
} // namespace sw
