#include "pch.h"

#include "Engine/UserSettings/HardwareDetection.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EnginePlatformHeaders.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct HardwareDetectionInternal
        {
            /** @brief 해상도 목록에 넣는 가장 작은 변(픽셀)입니다. 옛 모드(640x480 등)는 옵션 메뉴에 필요 없다. */
            static constexpr int32 kMinResolutionSide = 640;

            static bool isResolutionBefore( const int2& left, const int2& right )
            {
                return left._x != right._x ? left._x < right._x : left._y < right._y;
            }

#if defined( SW_PLATFORM_WINDOWS )
            using PfnCreateDXGIFactory1 = HRESULT( WINAPI* )( REFIID, void** );

            /**
             * @brief 기본 어댑터(RHI 가 고르는 0 번)의 이름과 전용 메모리를 읽습니다. dxgi.dll 을 링크하지 않고 올려 쓴다 — Engine 은 DXGI 를 링크하지 않는다.
             * @details 이미 올라와 있어도 `LoadLibraryExW` 로 참조를 하나 가진다(프로세스 끝까지 둔다). `GetModuleHandleW` 로 빌리면 다른 코드가 내릴 때 함수 포인터가 허공을 가리킨다.
             */
            static void probeAdapter( HardwareDetectionResult& result )
            {
                const HMODULE hModule = LoadLibraryExW( L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 );
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
            static void probeAdapter( HardwareDetectionResult& result )
            {
                for ( int32 cardIndex = 0; cardIndex < 8; ++cardIndex )
                {
                    string text;
                    utf8   arrPath[constant::kMaxBuffer128]{};
                    formatstring( arrPath, constant::kMaxBuffer128, "/sys/class/drm/card%#/device/mem_info_vram_total", cardIndex );
                    if ( FileUtil::exists( arrPath ) == false || FileUtil::readTextFile( arrPath, text ) == false ) // 없는 카드는 오류가 아니라 건너뜀
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
            static void probeAdapter( HardwareDetectionResult& /*result*/ )
            {
            }
#endif
        };
    } // namespace

    /** @brief 부하 검증의 하드웨어 흉내(`Test/Load/Profiles.json` 의 `vram_mb`) — 비디오 메모리를 이 값(MB) 이하로 보고합니다(0 = 실제 값). 품질 자동 선택이 흉내 낸 기계처럼 고른다. */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_emulatedGPUMemoryMb, 0, "하드웨어 흉내: 사양 조사가 보고하는 비디오 메모리 상한(MB, 0=실제 값)" );

    HardwareDetectionResult HardwareDetection::probe()
    {
        HardwareDetectionResult result;
        result._logicalCoreCount = thread::getUsableProcessorCount();

#if defined( SW_PLATFORM_WINDOWS )
        MEMORYSTATUSEX memoryStatus{};
        memoryStatus.dwLength = sizeof( memoryStatus );
        if ( GlobalMemoryStatusEx( &memoryStatus ) != FALSE )
            result._systemMemoryMb = static_cast<uint32>( memoryStatus.ullTotalPhys / constant::kBytesPerMegabyte );
        // 메모리 상한이 걸린 Job Object 안이면(부하 검증의 하드웨어 흉내) 그 상한이 이 프로세스의 메모리다.
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimit{};
        const bool                           bJobLimited = QueryInformationJobObject( nullptr, JobObjectExtendedLimitInformation, &jobLimit, sizeof( jobLimit ), nullptr ) != FALSE &&
                                 ( jobLimit.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_PROCESS_MEMORY ) != 0;
        if ( bJobLimited )
            result._systemMemoryMb = MathUtil::min( result._systemMemoryMb, static_cast<uint32>( jobLimit.ProcessMemoryLimit / constant::kBytesPerMegabyte ) );
#elif defined( SW_PLATFORM_LINUX )
        const int64 pageCount = static_cast<int64>( sysconf( _SC_PHYS_PAGES ) );
        const int64 pageSize  = static_cast<int64>( sysconf( _SC_PAGE_SIZE ) );
        if ( pageCount > 0 && pageSize > 0 )
            result._systemMemoryMb = static_cast<uint32>( ( pageCount / 1024 ) * pageSize / 1024 );
#endif
        HardwareDetectionInternal::probeAdapter( result );
        if ( gv_emulatedGPUMemoryMb > 0 )
            result._gpuMemoryMb = result._gpuMemoryMb > 0 ? MathUtil::min( result._gpuMemoryMb, static_cast<uint32>( gv_emulatedGPUMemoryMb ) ) : static_cast<uint32>( gv_emulatedGPUMemoryMb );
        return result;
    }

    void HardwareDetection::probeDisplayResolutions( vector<int2>& outListResolution )
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
            if ( resolution._x < HardwareDetectionInternal::kMinResolutionSide || resolution._y < HardwareDetectionInternal::kMinResolutionSide )
                continue;
            bool bKnown = false;
            for ( const int2& known : outListResolution )
            {
                bKnown = bKnown || ( known._x == resolution._x && known._y == resolution._y );
            }
            if ( bKnown == false )
                outListResolution.push_back( resolution );
        }
        std::sort( outListResolution.begin(), outListResolution.end(), &HardwareDetectionInternal::isResolutionBefore );
#endif
    }
} // namespace sw
