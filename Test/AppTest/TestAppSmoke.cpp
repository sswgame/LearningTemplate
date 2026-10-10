#include "pch.h"

#include "AppTest/AppTestUtil.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Compression/CompressionStream.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/MemoryTag.h"
#include "Core/Process/Process.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Compression/EngineCompressionCodecUtil.h"
#include "Engine/Graphics/RHI/RHIInitResult.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/CookContract.gen.h"

#include <chrono>
#include <cstdlib>
#include <thread>

using namespace sw;

// 실제 App.exe 를 띄운다 — GPU · 창 · 셰이더가 필요하다. CI 러너엔 없다.
SW_TEST_REQUIRES_HOST( AppSmokeTest, "launches the real App.exe, which needs a GPU, a window and cooked shaders" );

// ------------------------------------------------------------------------------
// 1) AppSmokeTest — "실기동 게이트" 를 자동화한 것
//
// 이 저장소의 실질적인 최종 검증은 **App 을 띄워 보는 것**이다(네 백엔드 × 에디터 유무, 종료 코드 0,
// 로그에 `[Error]` 0건). `EngineLoop` 은 어떤 단위 테스트도 돌리지 않으므로, 이 절차를 테스트로 두지
// 않으면 **엔진 기동 전체가 자동 그물 밖**에 있다.
//
// 무엇을 보는가:
//   - 종료 코드 0 (`-gv_profileFrames=N` 이 N 프레임 뒤 스스로 끝낸다)
//   - 로그에 `[Error]` 0건 — 기동이 "성공했는데 조용히 망가진" 경우를 여기서 잡는다
//   - 프레임이 실제로 돌았다는 증거 한 줄 (출력이 비어 있으면 띄우지도 못한 것이다)
//
// 무엇을 안 보는가: 화면의 그림. 그건 `-gv_screenshot` 픽셀 비교의 일이고 여기 섞으면 이 게이트가
// 느려져 아무도 안 돌린다.
//
// **로거가 서기 전의 실패는 이 게이트가 못 본다.** `Logger` 는 `EngineLoop::initialize` 안에서
// 만들어지므로 그 앞에서 부른 `SW_LOG_ERROR` 는 아무 데도 남지 않는다. 그 구간의 실패는 **종료 코드로만** 드러난다.
// ------------------------------------------------------------------------------

namespace
{
    /** @brief App 한 판의 결과. */
    struct AppRunResult
    {
        int32  _exitCode{ -1 };
        uint32 _errorCount{ 0 };
        uint32 _lineCount{ 0 };
        string _firstErrorLine{};
        /** @brief `runApp` 에 준 표식으로 시작하는 줄들(표식부터 줄 끝까지). 표식을 주지 않으면 비어 있습니다. */
        vector<string> _listMarkedLine{};
        uint32         _missingComponentLineCount{ 0 }; /**< `MissingComponent` 가 든 줄 수 — 씬이 모르는 타입을 만났다. */
        bool           _bLaunched{ false };
        bool           _bBackendUnusableHere{ false };     /**< App 이 종료 코드 kRHIUnusableHereExitCode 로 "이 기계에서 그 백엔드를 못 돌린다" 고 알렸다(RHIInitResult). */
        bool           _bVulkanValidationEnabled{ false }; /**< Vulkan 디바이스가 검증 레이어를 켜고 섰다(그래야 잘못된 사용이 [Error] 로 나온다). */
    };

    /**
     * @brief App 을 한 판 돌리고 종료 코드와 `[Error]` 줄 수를 돌려줍니다.
     * @details 작업 디렉터리는 **바꾸지 않는다** — 테스트의 작업 폴더가 이미 `Bin` 이고(ctest 가 그렇게
     *          돌린다) App 도 거기서 `Resource/` 를 찾아 올라간다. 배포본에서는 테스트 바이너리만
     *          `TestBin` 에 있고 작업 폴더는 여전히 `Bin` 이라 이 전제가 양쪽에서 같다.
     * @param pMarker nullptr 가 아니면 이 글이 든 줄을 표식부터 잘라 `_listMarkedLine` 에 모읍니다.
     */
    AppRunResult runApp( string_view arguments, const utf8* pMarker = nullptr )
    {
        AppRunResult result{};

        Process process;
        if ( test::AppTestUtil::launchApp( process, arguments ) == false )
            return result;
        result._bLaunched = true;

        string line;
        while ( process.readOutputLine( line ) )
        {
            ++result._lineCount;
            if ( line.find( "MissingComponent" ) != string::npos )
                ++result._missingComponentLineCount;
            if ( line.find( "(Validation Layers: ENABLED)" ) != string::npos )
                result._bVulkanValidationEnabled = true;

            const size_t markerIndex = pMarker == nullptr ? string::npos : line.find( pMarker );
            if ( markerIndex != string::npos )
            {
                string markedLine = line.substr( markerIndex );
                while ( markedLine.empty() == false && ( markedLine.back() == '\r' || markedLine.back() == ' ' ) )
                {
                    markedLine.pop_back();
                }
                result._listMarkedLine.push_back( std::move( markedLine ) );
            }

            if ( line.find( "[Error]" ) == string::npos )
                continue;

            ++result._errorCount;
            if ( result._firstErrorLine.empty() )
                result._firstErrorLine = line;
        }

        result._exitCode = process.waitForExit();
        // App 이 RHI 를 세우다 환경 탓(이 빌드에 없다 · 드라이버가 기능을 안 준다)으로 물러나면 이 코드로 끝난다. 로그 문구는 보지 않는다.
        result._bBackendUnusableHere = ( result._exitCode == kRHIUnusableHereExitCode );
        return result;
    }

    /**
     * @brief 한 판을 돌리고 계약(종료 코드 0 · 에러 0 · 출력 있음)을 검사합니다.
     * @return 실제로 검사했으면 true, 그 백엔드가 이 기계에 없어 건너뛰었으면 false.
     */
    bool expectCleanRun( string_view arguments )
    {
        const AppRunResult result = runApp( arguments );

        SW_EXPECT_TRUE_MSG( result._bLaunched, "App 을 띄우지 못했습니다 — 작업 폴더(Bin)나 테스트 바이너리 옆에 실행 파일이 있습니까?" );
        if ( result._bLaunched == false )
            return false;

        if ( result._bBackendUnusableHere )
        {
            // 어느 백엔드가 빠졌는지 **눈에 보이게** 남긴다. 조용히 건너뛰면 윈도우에서 백엔드 하나가
            // 진짜로 죽은 날에도 초록으로 보인다.
            SW_LOG_WARNING( "Skipping smoke run '%#' — no usable RHI backend on this machine.", string( arguments ).c_str() );
            return false;
        }

        SW_EXPECT_TRUE_MSG( result._exitCode == 0, "App 이 0 이 아닌 코드로 끝났습니다" );
        SW_EXPECT_TRUE_MSG( result._errorCount == 0, result._firstErrorLine.empty() ? "로그에 [Error] 가 있습니다" : result._firstErrorLine.c_str() );
#if !defined( SW_SHIPPING )
        // 출력이 한 줄도 없으면 "조용히 성공" 이 아니라 아무것도 안 한 것이다.
        // **배포본에서는 반대다** — Info 로그가 컴파일에서 빠져 깨끗한 실행이 곧 출력 0줄이다.
        SW_EXPECT_TRUE_MSG( result._lineCount > 0, "App 이 로그를 한 줄도 남기지 않았습니다 — 정말 돌았습니까?" );
#endif
        return true;
    }

#if !defined( SW_SHIPPING )
    /**
     * @brief `-gv_profileFrames` 보고의 태그 줄에서 @p tagName 줄의 KB 정수부를 읽습니다. 줄이 없으면(그 태그가 0 바이트) 0 입니다.
     * @details 줄 모양: `[Memory]   Editor  15538.7 KB  77.6%  2383 blocks | peak …`. `runApp` 에 표식 `"[Memory]   "` 을 주고 돌린 결과를 넘깁니다.
     */
    uint64 findMemoryTagKilobytes( const AppRunResult& result, string_view tagName )
    {
        for ( const string& line : result._listMarkedLine )
        {
            const string_view text{ line };
            const size_t      nameStart = text.find_first_not_of( ' ', string_view{ "[Memory]" }.size() );
            const size_t      nameEnd   = nameStart == string_view::npos ? string_view::npos : text.find( ' ', nameStart );
            if ( nameEnd == string_view::npos || text.substr( nameStart, nameEnd - nameStart ) != tagName )
                continue;
            const size_t numberStart = text.find_first_not_of( ' ', nameEnd );
            return numberStart == string_view::npos ? 0 : std::strtoull( line.c_str() + numberStart, nullptr, 10 );
        }
        return 0;
    }

    /** @brief 태그를 걸지 않은 할당(Unknown)의 상한(KB)입니다. 진입점이 빠지면 기동만으로 수십 KB 가 이 줄에 쌓입니다. */
    constexpr uint64 kMaxUnknownTagKilobytes = 8;

    /**
     * @brief 보고의 "sw 할당자 밖" 줄(`[Memory]   (outside the sw allocator — …)  19.8 KB`)의 KB 정수부입니다. 줄이 없으면(CRT 힙을 잴 수 없는 구성) 0 입니다.
     */
    uint64 findOutsideAllocatorKilobytes( const AppRunResult& result )
    {
        for ( const string& line : result._listMarkedLine )
        {
            if ( line.find( "(outside the sw allocator" ) == string::npos )
                continue;
            const size_t closeIndex = line.rfind( ')' );
            if ( closeIndex == string::npos )
                continue;
            const size_t numberStart = line.find_first_not_of( ' ', closeIndex + 1 );
            return numberStart == string::npos ? 0 : std::strtoull( line.c_str() + numberStart, nullptr, 10 );
        }
        return 0;
    }

    /**
     * @brief "sw 할당자 밖" 의 상한(KB)입니다. 프로파일러가 로거 · 이름 풀보다 늦게 서면 그 블록(~870 KB)이 이 줄로 갑니다.
     */
    constexpr uint64 kMaxOutsideAllocatorKilobytes = 128;
#endif

    /** @brief PPM(P6) 한 장 — 폭 · 높이와 RGB 8비트 픽셀. */
    struct PpmImage
    {
        vector<uint8> _rgbBytes{};
        uint32        _width{ 0 };
        uint32        _height{ 0 };
    };

    /** @brief 공백(스페이스 · 탭 · 줄바꿈)을 건너뛰고 10진수 하나를 읽습니다. 숫자가 없으면 false. */
    [[nodiscard]] bool readPpmNumber( const vector<uint8>& fileBytes, size_t& inoutOffset, uint32& outValue )
    {
        while ( inoutOffset < fileBytes.size() && ( fileBytes[inoutOffset] == ' ' || fileBytes[inoutOffset] == '\n' || fileBytes[inoutOffset] == '\r' || fileBytes[inoutOffset] == '\t' ) )
        {
            ++inoutOffset;
        }
        const size_t start = inoutOffset;
        outValue           = 0;
        while ( inoutOffset < fileBytes.size() && '0' <= fileBytes[inoutOffset] && fileBytes[inoutOffset] <= '9' )
        {
            outValue = outValue * 10 + static_cast<uint32>( fileBytes[inoutOffset] - '0' );
            ++inoutOffset;
        }
        return inoutOffset > start;
    }

    /** @brief `-gv_screenshot` 이 쓰는 PPM(P6, 최댓값 255)을 읽습니다. 모양이 다르면 false. */
    [[nodiscard]] bool parsePpm( const vector<uint8>& fileBytes, PpmImage& outImage )
    {
        if ( fileBytes.size() < 2 || fileBytes[0] != 'P' || fileBytes[1] != '6' )
            return false;
        size_t offset   = 2;
        uint32 maxValue = 0;
        if ( readPpmNumber( fileBytes, offset, outImage._width ) == false || readPpmNumber( fileBytes, offset, outImage._height ) == false ||
             readPpmNumber( fileBytes, offset, maxValue ) == false || maxValue != 255 )
            return false;
        ++offset; // 최댓값 뒤의 공백 한 글자
        const size_t pixelByteCount = static_cast<size_t>( outImage._width ) * outImage._height * 3;
        if ( fileBytes.size() < offset + pixelByteCount )
            return false;
        outImage._rgbBytes.assign( fileBytes.begin() + static_cast<ptrdiff_t>( offset ), fileBytes.begin() + static_cast<ptrdiff_t>( offset + pixelByteCount ) );
        return true;
    }

    /**
     * @brief 골든 이미지 폴더(`Test/AppTest/Golden`)의 절대 경로. 못 찾으면 빈 문자열.
     * @details 테스트의 작업 폴더는 `build/<프리셋>/Bin` 이다 — 거기서 위로 올라가며 저장소의 폴더를 찾는다.
     */
    string findGoldenDirectory()
    {
        string directory = FileUtil::getCurrentPath();
        for ( uint32 depth = 0; depth < 8 && directory.empty() == false; ++depth )
        {
            const string candidate = FileUtil::joinPath( directory, "Test/AppTest/Golden" );
            if ( FileUtil::isDirectory( candidate ) )
                return candidate;
            const string parent = FileUtil::getDirectoryPart( directory );
            if ( parent == directory )
                break;
            directory = parent;
        }
        return {};
    }

    /** @brief 골든 파일 압축 · 해제에 쓰는 코덱 표(Zlib 을 쓴다 — 배경이 대부분인 그림이 1 KB 남짓으로 준다). */
    struct GoldenCodecRegistry
    {
        GoldenCodecRegistry()
        {
            _registry.initialize();
            EngineCompressionCodecUtil::registerAll( _registry );
        }

        CompressionCodecRegistry _registry;
    };

    CompressionCodecRegistry& getGoldenCodecRegistry()
    {
        static GoldenCodecRegistry s_registry;
        return s_registry._registry;
    }

    /** @brief 골든과 비교한 결과 — 허용치를 넘은 픽셀 수와 가장 큰 채널 차이. */
    struct GoldenDifference
    {
        uint32 _pixelOverTolerance{ 0 };
        uint32 _maxChannelDifference{ 0 };
    };

    /** @brief 두 그림을 픽셀마다 비교합니다. 크기가 다르면 모든 픽셀이 넘은 것으로 셉니다. */
    GoldenDifference compareImage( const PpmImage& expected, const PpmImage& actual, uint32 channelTolerance )
    {
        GoldenDifference difference{};
        if ( expected._width != actual._width || expected._height != actual._height || expected._rgbBytes.size() != actual._rgbBytes.size() )
        {
            difference._pixelOverTolerance   = expected._width * expected._height;
            difference._maxChannelDifference = 255;
            return difference;
        }
        for ( size_t pixelOffset = 0; pixelOffset < expected._rgbBytes.size(); pixelOffset += 3 )
        {
            uint32 pixelMax = 0;
            for ( size_t channel = 0; channel < 3; ++channel )
            {
                const int32  delta    = static_cast<int32>( expected._rgbBytes[pixelOffset + channel] ) - static_cast<int32>( actual._rgbBytes[pixelOffset + channel] );
                const uint32 absDelta = static_cast<uint32>( delta < 0 ? -delta : delta );
                pixelMax              = absDelta > pixelMax ? absDelta : pixelMax;
            }
            difference._maxChannelDifference = pixelMax > difference._maxChannelDifference ? pixelMax : difference._maxChannelDifference;
            if ( pixelMax > channelTolerance )
                ++difference._pixelOverTolerance;
        }
        return difference;
    }

#if defined( SW_PLATFORM_WINDOWS )
    /** @brief 프로세스 @p processID 의 엔진 창을 찾습니다. 보일 때까지 @p timeoutSeconds 만큼 기다리고, 못 찾으면 nullptr 입니다. */
    HWND waitForEngineWindow( DWORD processID, int32 timeoutSeconds )
    {
        struct WindowSearch
        {
            DWORD _processID{ 0 };
            HWND  _hWnd{ nullptr };
        };
        const Deadline deadline = Deadline::afterMilliseconds( static_cast<int64>( timeoutSeconds ) * 1000 );
        while ( deadline.isExpired() == false )
        {
            WindowSearch search{ processID, nullptr };
            EnumWindows( []( HWND hWnd, LPARAM lParam ) -> BOOL
            {
                WindowSearch* pSearch = reinterpret_cast<WindowSearch*>( lParam );
                DWORD         ownerID = 0;
                GetWindowThreadProcessId( hWnd, &ownerID );
                utf16 className[64]{};
                GetClassNameW( hWnd, className, 64 );
                if ( ownerID == pSearch->_processID && IsWindowVisible( hWnd ) != FALSE && wcscmp( className, L"SWEngineWindowClass_OWNDC" ) == 0 )
                {
                    pSearch->_hWnd = hWnd;
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>( &search ) );
            if ( search._hWnd != nullptr )
                return search._hWnd;
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        }
        return nullptr;
    }
#endif
} // namespace

/**
 * @brief [AppSmokeTest] 창의 X(WM_CLOSE)가 도는 App 을 몇 초 안에 끝낸다
 * @details 게임 실행에는 닫기를 막는 처리기가 없다(에디터의 저장 확인만 막는다). 프레임 수로 스스로 끝나지 않게 띄워 닫기 하나로만 끝나는지 본다.
 */
SW_TEST_CASE( AppSmokeTest, WindowCloseEndsTheApp )
{
#if defined( SW_PLATFORM_WINDOWS )
    Process process;
    SW_ASSERT_TRUE( test::AppTestUtil::launchApp( process, "" ) );
    // 출력 파이프가 차면 App 이 쓰기에서 멈춘다 — 다른 스레드가 비운다.
    std::thread drainThread( [&process]()
    {
        string line;
        while ( process.readOutputLine( line ) )
        {
        }
    } );

    const HWND hWnd          = waitForEngineWindow( static_cast<DWORD>( process.getProcessID() ), 60 );
    bool       bExitedInTime = false;
    if ( hWnd != nullptr )
    {
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) ); // 메인 루프에 들어간 뒤 닫는다
        PostMessageW( hWnd, WM_CLOSE, 0, 0 );
        const Deadline deadline = Deadline::afterMilliseconds( 15000 );
        while ( process.isRunning() && deadline.isExpired() == false )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        }
        bExitedInTime = process.isRunning() == false;
    }
    if ( bExitedInTime == false )
        (void)process.terminate();
    const int32 exitCode = process.waitForExit();
    drainThread.join();

    SW_EXPECT_TRUE_MSG( hWnd != nullptr, "App 의 엔진 창을 60 초 안에 찾지 못했습니다" );
    SW_EXPECT_TRUE_MSG( bExitedInTime, "WM_CLOSE 뒤 15 초 안에 App 이 끝나지 않았습니다" );
    SW_EXPECT_EQUAL( 0, exitCode );
#else
    SW_TEST_SKIP( "엔진 창을 찾아 닫는 경로가 Win32 전용입니다" );
#endif
}

/**
 * @brief [AppSmokeTest] 네 백엔드에서 기동 → 프레임 → 종료가 깨끗한가
 * @details 백엔드마다 따로 본다 — 한 판에 묶으면 "어느 백엔드가 깨졌나" 를 로그에서 다시 찾아야 한다.
 *          백엔드는 `-dx12 / -dx11 / -vk / -gl` 스위치로 고른다(쿠킹 표의 별칭과 같은 철자).
 */
SW_TEST_CASE( AppSmokeTest, EveryBackendStartsRendersAndExitsCleanly )
{
#if defined( SW_SHIPPING )
    // **배포본은 백엔드를 하나만 링크한다**(`SW_SHIPPING_RHI_BACKEND`, 윈도우 기본 DX12 —
    // `Source/Engine/CMakeLists.txt`). 없는 백엔드를 요구하면 `RHIBackendRegistry` 가 거절하고
    // App 이 0 이 아닌 코드로 끝난다. 그래서 여기서는 스위치 없이 "이 빌드가 가진 것" 으로 돌린다.
    constexpr const utf8* kArrBackendSwitch[] = { "" };
#else
    // 스위치는 쿠킹 표의 줄마다 명령줄 이름이다(`-dx11` · `-dx12` · `-vk` · `-gl`) — 백엔드가 늘면 여기도 같이 는다.
    #define SW_APP_SMOKE_BACKEND_SWITCH( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) "-" CommandLineName,
    constexpr const utf8* kArrBackendSwitch[] = { SW_RHI_BACKEND_TABLE( SW_APP_SMOKE_BACKEND_SWITCH ) };
    #undef SW_APP_SMOKE_BACKEND_SWITCH
#endif
    uint32 checkedCount = 0;
    for ( const utf8* pSwitch : kArrBackendSwitch )
    {
        string arguments{ "-gv_profileFrames=20 " };
        arguments += pSwitch;
        if ( expectCleanRun( arguments ) )
            ++checkedCount;
    }

    // 하나라도 돌았으면 그것으로 계약을 확인한 것이다. **하나도 못 돌았으면 아무것도 검사하지 않았으므로**
    // 초록으로 두면 안 된다 — 건너뛴 것으로 남긴다.
    if ( checkedCount == 0 )
        SW_TEST_SKIP( "no usable RHI backend on this machine — run where a GPU and driver exist" );
}

#if !defined( SW_SHIPPING )
/**
 * @brief [AppSmokeTest] 에디터를 켠 기동도 깨끗한가
 * @details **에디터는 명시적으로 켜야 한다** — `-EnableEditor` 없이 돌린 것은 에디터 OFF 검증이다.
 *          배포본에는 에디터 모듈이 없으므로 이 케이스는 Dev 에만 있다.
 */
SW_TEST_CASE( AppSmokeTest, EditorModeStartsAndExitsCleanly )
{
    constexpr const utf8* kArrBackendSwitch[] = { "-dx12", "-gl" };

    uint32 checkedCount = 0;
    for ( const utf8* pSwitch : kArrBackendSwitch )
    {
        string arguments{ "-gv_profileFrames=20 -EnableEditor " };
        arguments += pSwitch;
        if ( expectCleanRun( arguments ) )
            ++checkedCount;
    }

    if ( checkedCount == 0 )
        SW_TEST_SKIP( "no usable RHI backend on this machine — run where a GPU and driver exist" );
}

/**
 * @brief [AppSmokeTest] 에디터의 시작 씬(`-gv_editorStartupScene`)이 실제로 열리는 씬이고, 그 씬의 GameFramework 컴포넌트가 제 타입으로 지어진다
 * @details 타입 공급자 모듈은 기동 단계 `ModuleTypes` 에서 오르고, 게임이 먼저 · 에디터가 나중에 선다. 순서가 뒤집히면 에디터가 시작 씬을
 *          GameFramework.dll 이 오르기 전에 읽어 그 컴포넌트가 MissingComponent 가 되고, 뒤에 선 게임이 요청한 처음 씬(실행 설정의 시작 씬)이
 *          마지막 요청을 남기는 씬 매니저에서 이긴다. 마지막으로 바뀐 활성 씬이 에디터의 시작 씬이어야 한다.
 */
SW_TEST_CASE( AppSmokeTest, EditorStartupSceneIsTheSceneThatOpens )
{
    const AppRunResult result = runApp( "-gv_profileFrames=20 -EnableEditor -dx12 \"-gv_editorStartupScene=game/empty/maps/spriteui.scene.xml\"",
                                        "Active scene swapped to" );
    SW_ASSERT_TRUE_MSG( result._bLaunched, "App 을 띄우지 못했습니다 — 작업 폴더(Bin)나 테스트 바이너리 옆에 실행 파일이 있습니까?" );
    if ( result._bBackendUnusableHere )
        SW_TEST_SKIP( "DX12 is not usable on this machine" );
    SW_EXPECT_EQUAL( 0, result._exitCode );
    SW_EXPECT_TRUE_MSG( result._errorCount == 0, result._firstErrorLine.c_str() );
    SW_EXPECT_EQUAL( 0u, result._missingComponentLineCount );
    SW_ASSERT_TRUE_MSG( result._listMarkedLine.empty() == false, "no scene became active" );
    SW_EXPECT_STREQ( "Active scene swapped to 'SpriteUI'", result._listMarkedLine.back().c_str() );
}

/**
 * @brief [AppSmokeTest] 에디터를 켠 기동의 메모리가 용도 태그로 나뉘어 보고되는가
 * @details `-gv_profileFrames` 보고의 태그 줄(`[Memory]   <태그>  <KB> KB  <몫>%  <블록> blocks | …`)을 읽는다. ImGui 의 할당(폰트 아틀라스 · 드로 리스트 ·
 *          도킹 상태)은 에디터 모듈이 sw 할당자로 보내야 Editor 줄로 세이고, 진입점이 빠진 몫인 Unknown 은 작아야 한다. 태그 스코프가 컴파일되는
 *          구성(Debug)에서만 본다.
 */
SW_TEST_CASE( AppSmokeTest, EditorMemoryIsAttributedByTag )
{
    if constexpr ( kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );

    const AppRunResult result = runApp( "-gv_profileFrames=5 -gv_memoryTracking=1 -EnableEditor -dx12", "[Memory]   " );
    SW_ASSERT_TRUE_MSG( result._bLaunched, "App 을 띄우지 못했습니다 — 작업 폴더(Bin)나 테스트 바이너리 옆에 실행 파일이 있습니까?" );
    if ( result._bBackendUnusableHere )
        SW_TEST_SKIP( "DX12 is not usable on this machine" );
    SW_EXPECT_EQUAL( 0, result._exitCode );

    const uint64 editorKilobytes  = findMemoryTagKilobytes( result, "Editor" );
    const uint64 unknownKilobytes = findMemoryTagKilobytes( result, "Unknown" );
    SW_EXPECT_TRUE_MSG( editorKilobytes > 4096, ( string( "Editor KB = " ) + to_string( editorKilobytes ) ).c_str() );
    SW_EXPECT_TRUE_MSG( unknownKilobytes < kMaxUnknownTagKilobytes, ( string( "Unknown KB = " ) + to_string( unknownKilobytes ) ).c_str() );
    const uint64 outsideKilobytes = findOutsideAllocatorKilobytes( result );
    SW_EXPECT_TRUE_MSG( outsideKilobytes < kMaxOutsideAllocatorKilobytes, ( string( "outside sw allocator KB = " ) + to_string( outsideKilobytes ) ).c_str() );
}

/**
 * @brief [AppSmokeTest] 에디터 없는 기동도 모든 할당이 용도 태그에 실리는가
 * @details 엔진 · 앱의 진입점(기동 단계 · 서비스 생성 · 로거 · 입력 · 모듈 호스트 · 엔진이 띄우는 스레드)이 태그를 걸면 Unknown 은 거의 0 이다.
 */
SW_TEST_CASE( AppSmokeTest, RuntimeMemoryIsAttributedByTag )
{
    if constexpr ( kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );

    const AppRunResult result = runApp( "-gv_profileFrames=5 -gv_memoryTracking=1 -dx12", "[Memory]   " );
    SW_ASSERT_TRUE_MSG( result._bLaunched, "App 을 띄우지 못했습니다 — 작업 폴더(Bin)나 테스트 바이너리 옆에 실행 파일이 있습니까?" );
    if ( result._bBackendUnusableHere )
        SW_TEST_SKIP( "DX12 is not usable on this machine" );
    SW_EXPECT_EQUAL( 0, result._exitCode );

    const uint64 unknownKilobytes = findMemoryTagKilobytes( result, "Unknown" );
    SW_EXPECT_TRUE_MSG( unknownKilobytes < kMaxUnknownTagKilobytes, ( string( "Unknown KB = " ) + to_string( unknownKilobytes ) ).c_str() );
    const uint64 outsideKilobytes = findOutsideAllocatorKilobytes( result );
    SW_EXPECT_TRUE_MSG( outsideKilobytes < kMaxOutsideAllocatorKilobytes, ( string( "outside sw allocator KB = " ) + to_string( outsideKilobytes ) ).c_str() );
}

/**
 * @brief [AppSmokeTest] `-gv_screenshot` 을 쓰지 못하면 그 경로를 경고로 알린다
 * @details 디버그 스크린샷은 실패해도 조용히 지나갔다(시나리오 스크린샷만 경고했다). 폴더를 파일 경로로 주어 쓰기를 실패시킨다.
 */
SW_TEST_CASE( AppSmokeTest, UnwritableScreenshotIsReported )
{
    const string blockedPath = test::makeTempDirectory( "screenshot_is_a_folder" ); // 이 경로는 폴더라 파일로 쓸 수 없다
    string       arguments{ "-W=128 -H=72 -gv_profileFrames=20 -dx12 \"-gv_screenshot=" };
    arguments += blockedPath;
    arguments += "\"";
    const AppRunResult result = runApp( arguments, "Screenshot was not written" );
    SW_ASSERT_TRUE_MSG( result._bLaunched, "App 을 띄우지 못했습니다 — 작업 폴더(Bin)나 테스트 바이너리 옆에 실행 파일이 있습니까?" );
    if ( result._bBackendUnusableHere )
        SW_TEST_SKIP( "DX12 is not usable on this machine" );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( result._listMarkedLine.size() ) );
}

/**
 * @brief [AppSmokeTest] 백엔드 교체 뒤 디바이스에 매인 설정이 새 디바이스를 따르는지 검증
 * @details 교체는 디바이스에 의존하는 기동 단계를 다시 세운다. 씬 스냅샷 빌더의 "머티리얼을 넘어 배치 합치기" 는 디바이스가
 *          텍스처를 인덱스로 고를 수 있을 때(DX12 · Vulkan 의 네이티브 bindless)만 켜져야 한다 — DX11 · GL 은 머티리얼 텍스처를
 *          고정 슬롯에 걸므로 합친 배치가 한 머티리얼의 텍스처로 그려진다. 교체 뒤 `Active backend is now` 줄의 두 값이 같아야 한다.
 */
SW_TEST_CASE( AppSmokeTest, BackendSwapFollowsTheNewDevice )
{
    // -gv_rhiSwapTo 는 RHIBackend 값이다(0 = DirectX11, 1 = DirectX12).
    constexpr const utf8* kArrSwapArgument[] = { "-dx12 -gv_rhiSwapTo=0", "-dx11 -gv_rhiSwapTo=1" };

    uint32 checkedCount = 0;
    for ( const utf8* pSwapArgument : kArrSwapArgument )
    {
        string arguments{ "-gv_profileFrames=30 -gv_rhiSwapAtFrame=10 " };
        arguments += pSwapArgument;
        const AppRunResult result = runApp( arguments, "Active backend is now" );
        SW_ASSERT_TRUE_MSG( result._bLaunched, "App 을 띄우지 못했습니다 — 작업 폴더(Bin)나 테스트 바이너리 옆에 실행 파일이 있습니까?" );
        if ( result._bBackendUnusableHere )
            continue;
        ++checkedCount;
        SW_EXPECT_TRUE_MSG( result._exitCode == 0, arguments.c_str() );
        SW_EXPECT_TRUE_MSG( result._errorCount == 0, result._firstErrorLine.empty() ? arguments.c_str() : result._firstErrorLine.c_str() );
        SW_ASSERT_TRUE_MSG( result._listMarkedLine.size() == 1, arguments.c_str() );

        const string&     line          = result._listMarkedLine[0];
        const string_view kMergeKey     = "across materials ";
        const string_view kBindlessKey  = "bindless sampling ";
        const size_t      mergeIndex    = line.find( kMergeKey.data() );
        const size_t      bindlessIndex = line.find( kBindlessKey.data() );
        SW_ASSERT_TRUE_MSG( mergeIndex != string::npos && bindlessIndex != string::npos, line.c_str() );
        const utf8 mergeValue    = line[mergeIndex + kMergeKey.size()];
        const utf8 bindlessValue = line[bindlessIndex + kBindlessKey.size()];
        SW_EXPECT_TRUE_MSG( mergeValue == bindlessValue, line.c_str() );
    }

    if ( checkedCount == 0 )
        SW_TEST_SKIP( "neither DX12 nor DX11 is usable on this machine" );
}

/**
 * @brief [AppSmokeTest] 백엔드 교체 뒤 인스턴스를 기동과 같은 순서(게임 → 에디터)로 다시 세운다
 * @details 에디터는 게임이 등록한 타입 · 서비스를 보며 선다. 기동은 게임 → 에디터인데 교체 경로만 에디터 → 게임이면 교체 뒤 에디터가 게임 없이
 *          선다. 인스턴스를 세울 때마다 찍는 `Module instance initialized: <모듈>` 줄의 순서를 본다.
 */
SW_TEST_CASE( AppSmokeTest, BackendSwapRebuildsGameBeforeEditor )
{
    // -gv_rhiSwapTo 는 RHIBackend 값이다(0 = DirectX11, 1 = DirectX12).
    constexpr const utf8* kArrSwapArgument[]  = { "-dx12 -gv_rhiSwapTo=0", "-dx11 -gv_rhiSwapTo=1" };
    constexpr const utf8* kArrExpectedLabel[] = { "Module instance initialized: Game", "Module instance initialized: Editor", "Module instance initialized: Game",
                                                  "Module instance initialized: Editor" };

    uint32 checkedCount = 0;
    for ( const utf8* pSwapArgument : kArrSwapArgument )
    {
        string arguments{ "-gv_profileFrames=30 -gv_rhiSwapAtFrame=10 -EnableEditor " };
        arguments += pSwapArgument;
        const AppRunResult result = runApp( arguments, "Module instance initialized: " );
        SW_ASSERT_TRUE_MSG( result._bLaunched, "App 을 띄우지 못했습니다 — 작업 폴더(Bin)나 테스트 바이너리 옆에 실행 파일이 있습니까?" );
        if ( result._bBackendUnusableHere )
            continue;
        ++checkedCount;
        SW_EXPECT_TRUE_MSG( result._exitCode == 0, arguments.c_str() );
        SW_EXPECT_TRUE_MSG( result._errorCount == 0, result._firstErrorLine.empty() ? arguments.c_str() : result._firstErrorLine.c_str() );
        SW_ASSERT_TRUE_MSG( result._listMarkedLine.size() == std::size( kArrExpectedLabel ), arguments.c_str() );
        for ( size_t lineIndex = 0; lineIndex < std::size( kArrExpectedLabel ); ++lineIndex )
        {
            const string& line = result._listMarkedLine[lineIndex];
            SW_EXPECT_TRUE_MSG( line.find( kArrExpectedLabel[lineIndex] ) != string::npos, line.c_str() );
        }
    }

    if ( checkedCount == 0 )
        SW_TEST_SKIP( "neither DX12 nor DX11 is usable on this machine" );
}

/**
 * @brief [AppSmokeTest] 에디터 확장의 정적 등록이 실제 EditorModule 에 다 실리고, 순서 · 메뉴 배치가 그대로인가
 * @details 패널 · 팝업 · 인스펙터 · 시각화는 각자 자기 .cpp 의 정적 등록자로 등록하고, 메뉴는 커맨드 표의 경로 · 순서 칸에서 나온다.
 *          그래서 등록이 빠지거나 순서 키 · 메뉴 칸이 바뀌어도 빌드는 그대로 통과한다. 실제 App 을 `-gv_editorRegistryDump=1` 로 띄워
 *          기대 줄이 **이 순서로** 모두 나오는지 본다(사이에 새 줄이 끼는 것은 괜찮다). 패널 제목은 기본 도킹 배치(`applyDefaultDockLayout`)가
 *          대조하는 이름이기도 하다.
 */
SW_TEST_CASE( AppSmokeTest, EditorRegistriesKeepTheirOrder )
{
    constexpr const utf8* kArrExpectedLine[] = {
        "EditorRegistry|panel|hierarchy|Core|Hierarchy",
        "EditorRegistry|panel|inspector|Core|Inspector",
        "EditorRegistry|panel|scene_view|Core|Scene",
        "EditorRegistry|panel|game_view|Core|Game",
        "EditorRegistry|panel|console|Core|Output Log",
        "EditorRegistry|panel|profiler|Core|Profiler",
        "EditorRegistry|panel|content_browser|Core|Content Browser",
        "EditorRegistry|panel|history|Tool|History",
        "EditorRegistry|panel|global_variables|Tool|Global Variables",
        "EditorRegistry|panel|render_targets|Tool|Render Targets",
        "EditorRegistry|panel|sequencer|Tool|Sequencer",
        "EditorRegistry|panel|animation_graph|Tool|Animation Graph",
        "EditorRegistry|panel|dialogue_graph|Tool|Dialogue Graph",
        "EditorRegistry|panel|material|Tool|Material",
        "EditorRegistry|panel|prefab_editor|Tool|Prefab Editor",
        "EditorRegistry|panel|tile_map|Tool|Tile Map Tool",
        "EditorRegistry|panel|sprite_clip|Tool|Sprite Clip",
        "EditorRegistry|panel|data_table|Tool|Data Table Editor",
        "EditorRegistry|panel|input_map|Tool|Input Map Editor",
        "EditorRegistry|popup|QuickLauncher",
        "EditorRegistry|popup|CommandPalette",
        "EditorRegistry|popup|BoneHierarchy",
        "EditorRegistry|inspector|CameraComponent",
        "EditorRegistry|inspector|SceneComponent",
        "EditorRegistry|inspector|SpriteComponent",
        "EditorRegistry|inspector|TagComponent",
        "EditorRegistry|visualizer|box_collider_2d",
        "EditorRegistry|visualizer|camera_frustum",
        "EditorRegistry|menu|Viewport/Align|transform.snapToGround,-,transform.alignX,transform.alignY,transform.alignZ,-,"
        "transform.distributeX,transform.distributeY,transform.distributeZ",
        "EditorRegistry|menu|MainMenu/File|scene.new,scene.open,asset.save,scene.saveScene,-,editor.quickOpen,editor.commandPalette,-,"
        "editor.exit",
        "EditorRegistry|menu|MainMenu/Edit|edit.undo,edit.redo,-,editor.themeSettings,editor.preferences,-,help.reportBug",
        "EditorRegistry|menu|MainMenu/Build|build.compileGame,build.compileEditor,build.compileAll,-,build.cancel",
    };

    const AppRunResult result = runApp( "-gv_profileFrames=5 -EnableEditor -dx12 -gv_editorRegistryDump=1", "EditorRegistry|" );
    SW_ASSERT_TRUE_MSG( result._bLaunched, "App 을 띄우지 못했습니다 — 작업 폴더(Bin)나 테스트 바이너리 옆에 실행 파일이 있습니까?" );
    if ( result._bBackendUnusableHere )
        SW_TEST_SKIP( "DX12 is not usable on this machine" );
    SW_EXPECT_TRUE_MSG( result._exitCode == 0, "App 이 0 이 아닌 코드로 끝났습니다" );
    SW_EXPECT_TRUE_MSG( result._errorCount == 0, result._firstErrorLine.empty() ? "로그에 [Error] 가 있습니다" : result._firstErrorLine.c_str() );

    // 기대 줄을 순서대로 찾는다. 하나라도 없거나 순서가 뒤바뀌면 그 줄을 이름으로 알린다.
    size_t searchIndex = 0;
    for ( const utf8* pExpected : kArrExpectedLine )
    {
        size_t foundIndex = searchIndex;
        while ( foundIndex < result._listMarkedLine.size() && result._listMarkedLine[foundIndex] != pExpected )
        {
            ++foundIndex;
        }
        SW_EXPECT_TRUE_MSG( foundIndex < result._listMarkedLine.size(), pExpected );
        if ( foundIndex < result._listMarkedLine.size() )
            searchIndex = foundIndex + 1;
    }
}

namespace
{
    /** @brief 작업 폴더에서 위로 올라가며 에디터 설정 폴더의 `imgui.ini` 경로를 찾습니다. 설정 폴더가 없으면 빈 문자열입니다. */
    string findEditorImguiIniPath()
    {
        string directory = FileUtil::getCurrentPath();
        for ( uint32 depth = 0; depth < 8 && directory.empty() == false; ++depth )
        {
            const string configDirectory = FileUtil::joinPath( directory, "Config/Editor" );
            if ( FileUtil::isDirectory( configDirectory ) )
                return FileUtil::joinPath( configDirectory, "imgui.ini" );
            const string parent = FileUtil::getDirectoryPart( directory );
            if ( parent == directory )
                break;
            directory = parent;
        }
        return {};
    }

    /** @brief 파일이 있으면 그 바이트를, 없으면 빈 값과 false 를 돌려줍니다. */
    [[nodiscard]] bool readOptionalFile( const string& path, vector<uint8>& outBytes )
    {
        outBytes.clear();
        return path.empty() == false && FileUtil::exists( path ) && FileUtil::readFile( path, outBytes );
    }
} // namespace

/**
 * @brief [AppSmokeTest] 에디터 자체 시험이 실제 에디터 안에서 모두 통과하고, 그 실행이 사용자의 레이아웃을 건드리지 않는다
 * @details 패널 · 위젯 · 도킹은 에디터 컨텍스트와 ImGui 프레임이 모두 서 있어야 재현된다(UE Automation · Unity EditMode 의 자리). App 을
 *          `-gv_editorSelfTest=*` 로 띄우면 에디터가 등록된 시험(`SW_EDITOR_SELF_TEST`)을 프레임마다 한 단계씩 돌리고 보고서를 쓴 뒤 스스로 닫힌다.
 *          알려진 시험이 모두 PASS 로 나와야 하고(빠지면 등록이 링크에서 빠진 것), 끝 줄의 실패 수가 0 이어야 한다. 시험 실행은 저장된 레이아웃을
 *          읽지도 쓰지도 않는다 — 끝난 뒤 `Config/Editor/imgui.ini` 가 실행 전과 바이트까지 같아야 한다.
 */
SW_TEST_CASE( AppSmokeTest, EditorSelfTestsPassInsideTheEditor )
{
    constexpr const utf8* kArrExpectedPass[] = {
        "EditorSelfTest|PASS|theme.palette",
        "EditorSelfTest|PASS|theme.reapplyKeepsSizes",
        "EditorSelfTest|PASS|font.iconGlyphs",
        "EditorSelfTest|PASS|widgets.helpMarker",
        "EditorSelfTest|PASS|widgets.propertyRow",
        "EditorSelfTest|PASS|dock.corePanelsAreDocked",
        "EditorSelfTest|PASS|dock.followsWindowSize",
        "EditorSelfTest|PASS|inspector.drawLeavesTheObjectAlone",
        "EditorSelfTest|PASS|preview.materialHoldsOneReference",
        "EditorSelfTest|PASS|hierarchy.tagFilter",
        "EditorSelfTest|PASS|hierarchy.offscreenRootsKeepTheirPlace",
        "EditorSelfTest|PASS|sceneView.resizeEveryFrame",
        "EditorSelfTest|PASS|sceneView.debugDraw",
        "EditorSelfTest|PASS|gameView.debugOverlay",
        "EditorSelfTest|PASS|toolbar.autoplayButton",
        "EditorSelfTest|PASS|console.tagFilter",
        "EditorSelfTest|PASS|console.devCommands",
        "EditorSelfTest|PASS|hierarchy.selectAllWith",
        "EditorSelfTest|PASS|layout.namedRoundTrip",
        "EditorSelfTest|PASS|themepark.extensionPanelDraws",
        "EditorSelfTest|PASS|themepark.layoutPreviewLoads",
        "EditorSelfTest|PASS|sceneView.gizmoMovesTheSelection",
        "EditorSelfTest|PASS|sceneView.gridAndGizmoDraw",
        "EditorSelfTest|PASS|gameView.hidesEditorOverlays",
        "EditorSelfTest|PASS|profiler.gpuMemoryTab",
        "EditorSelfTest|PASS|dpi.monitorScaleFollows",
        "EditorSelfTest|PASS|input.hierarchySearchTyping",
        "EditorSelfTest|PASS|input.tooltipOnHover",
        "EditorSelfTest|PASS|input.classicDarkSwatch",
        "EditorSelfTest|PASS|hierarchy.visibilityToggleFits",
        "EditorSelfTest|PASS|contentBrowser.deleteRefreshesTheList",
        "EditorSelfTest|PASS|contentBrowser.browsingWritesNoMeta",
        "EditorSelfTest|PASS|contentBrowser.treeDoesNotReadTheDiskEveryFrame",
        "EditorSelfTest|PASS|prefab.ignoresOtherFocusedAssets",
        "EditorSelfTest|PASS|globalVariables.groupsStack",
        "EditorSelfTest|PASS|panels.toolWindowsOpenAtAUsableSize",
        "EditorSelfTest|PASS|sceneView.overlaysStayInsideTheCanvas",
        "EditorSelfTest|PASS|hierarchy.selectedRowLeavesTheToggleVisible",
    };

    const string  imguiIniPath = findEditorImguiIniPath();
    vector<uint8> listIniBefore;
    const bool    bIniExistedBefore = readOptionalFile( imguiIniPath, listIniBefore );

    const string reportPath = test::makeTempPath( "editor_self_test.txt" );
    // 보고서 경로는 따옴표 없이 넘긴다 — 시험 임시 폴더에는 공백이 없다.
    string arguments{ "-gv_profileFrames=1200 -EnableEditor -dx12 -gv_editorSelfTest=* -gv_editorSelfTestReport=" };
    arguments += reportPath;
    const AppRunResult result = runApp( arguments, "EditorSelfTest|" );
    SW_ASSERT_TRUE_MSG( result._bLaunched, "App 을 띄우지 못했습니다 — 작업 폴더(Bin)나 테스트 바이너리 옆에 실행 파일이 있습니까?" );
    if ( result._bBackendUnusableHere )
        SW_TEST_SKIP( "DX12 is not usable on this machine" );
    SW_EXPECT_TRUE_MSG( result._exitCode == 0, "App 이 0 이 아닌 코드로 끝났습니다" );
    SW_EXPECT_TRUE_MSG( result._errorCount == 0, result._firstErrorLine.empty() ? "로그에 [Error] 가 있습니다" : result._firstErrorLine.c_str() );

    for ( const utf8* pExpected : kArrExpectedPass )
    {
        bool bFound{ false };
        for ( const string& line : result._listMarkedLine )
        {
            bFound = bFound || line == pExpected;
        }
        SW_EXPECT_TRUE_MSG( bFound, pExpected );
    }
    const bool bDoneWithoutFailure = result._listMarkedLine.empty() == false && sw::StringUtil::startsWith( result._listMarkedLine.back(), "EditorSelfTest|DONE|" ) &&
                                     result._listMarkedLine.back().size() >= 2 && result._listMarkedLine.back().substr( result._listMarkedLine.back().size() - 2 ) == "|0";
    SW_EXPECT_TRUE_MSG( bDoneWithoutFailure, result._listMarkedLine.empty() ? "no EditorSelfTest lines" : result._listMarkedLine.back().c_str() );

    string reportText;
    SW_EXPECT_TRUE_MSG( FileUtil::readTextFile( reportPath, reportText ) && reportText.find( "EditorSelfTest|DONE|" ) != string::npos,
                        "the report file was not written" );

    vector<uint8> listIniAfter;
    const bool    bIniExistsAfter = readOptionalFile( imguiIniPath, listIniAfter );
    SW_EXPECT_TRUE_MSG( bIniExistsAfter == bIniExistedBefore && listIniAfter == listIniBefore, "the self test run rewrote the user's imgui.ini" );
}

/**
 * @brief [AppSmokeTest] Vulkan 에디터에서 씬 뷰 RT 를 프레임마다 다시 만들어도 검증 레이어가 아무것도 남기지 않는다
 * @details 렌더 스레드는 UI 가 새 draw 스냅샷을 내기 전까지 옛 스냅샷을 여러 프레임에 다시 그린다. 뷰 RT 를 다시 만들 때 놓은 ImGui 디스크립터
 *          세트 · 렌더 타깃을 UI 스레드에서 곧바로 놓으면 그 프레임들이 놓인 세트를 쓰고(`vkFreeDescriptorSets ... in use`), 새 렌더 타깃을
 *          렌더러가 쓰기 전에 샘플링하면 UNDEFINED 레이아웃을 읽는다. 둘 다 검증 레이어가 [Error] 로 남긴다. `sceneView.resizeEveryFrame` 이
 *          90 프레임 동안 매 프레임 크기를 바꾼다. 검증 레이어는 Debug `Bin` 에 함께 놓인다 — 꺼진 채로 돌면 이 시험은 아무것도 보지 못하므로 진다.
 */
SW_TEST_CASE( AppSmokeTest, VulkanEditorSceneViewResizeLeavesNoValidationError )
{
    const AppRunResult result = runApp( "-gv_profileFrames=1200 -EnableEditor -vk -gv_editorSelfTest=sceneView.*", "EditorSelfTest|" );
    SW_ASSERT_TRUE_MSG( result._bLaunched, "App 을 띄우지 못했습니다 — 작업 폴더(Bin)나 테스트 바이너리 옆에 실행 파일이 있습니까?" );
    if ( result._bBackendUnusableHere )
        SW_TEST_SKIP( "Vulkan is not usable on this machine" );
    SW_EXPECT_TRUE_MSG( result._bVulkanValidationEnabled, "the Vulkan validation layer was not enabled - nothing would report a misuse" );
    SW_EXPECT_TRUE_MSG( result._exitCode == 0, "App 이 0 이 아닌 코드로 끝났습니다" );
    SW_EXPECT_TRUE_MSG( result._errorCount == 0, result._firstErrorLine.empty() ? "로그에 [Error] 가 있습니다" : result._firstErrorLine.c_str() );

    bool bPassed{ false };
    for ( const string& line : result._listMarkedLine )
    {
        bPassed = bPassed || line == "EditorSelfTest|PASS|sceneView.resizeEveryFrame";
    }
    SW_EXPECT_TRUE_MSG( bPassed, "sceneView.resizeEveryFrame did not pass" );
}
#endif

/**
 * @brief [AppSmokeTest] `--check-textures` 는 창 · RHI 없이 에디터 모듈만 올려 원본 텍스처와 DDS 를 대조하고 끝난다
 * @details 텍스처 임포트는 에디터 모듈(`TextureImporter`)의 일이고 엔진은 에디터를 모른다. 그래서 엔진은 헤드리스로 세우기만 하고 App 이
 *          모듈을 올려 `importEditorAssets` 를 부른다. 배포본에는 에디터 모듈이 없으므로 이유를 남기고 실패해야 한다.
 */
SW_TEST_CASE( AppSmokeTest, TextureCheckRunsHeadlessThroughTheEditorModule )
{
    const AppRunResult result = runApp( "--check-textures", "Texture check:" );
    SW_ASSERT_TRUE_MSG( result._bLaunched, "App 을 띄우지 못했습니다" );
#if defined( SW_SHIPPING )
    SW_EXPECT_TRUE( result._exitCode != 0 );
    SW_EXPECT_TRUE( result._listMarkedLine.empty() );
#else
    SW_EXPECT_TRUE_MSG( result._exitCode == 0, result._firstErrorLine.c_str() );
    SW_EXPECT_TRUE_MSG( result._errorCount == 0, result._firstErrorLine.c_str() );
    SW_ASSERT_EQUAL( size_t( 1 ), result._listMarkedLine.size() );
    SW_EXPECT_TRUE_MSG( result._listMarkedLine[0].find( "0 problems" ) != string::npos, result._listMarkedLine[0].c_str() );
#endif
}

/**
 * @brief [AppSmokeTest] `--check-models` 도 같은 길로 에디터 모듈의 모델 대조(`EditorImportKind::Model`)를 부르고 끝난다
 * @details 진입점이 종류를 인자로 받으므로 종류 번호가 어긋나면 텍스처를 대조하거나 "모르는 종류" 로 실패한다 — 표시 줄이 "Model check:" 인지 본다.
 */
SW_TEST_CASE( AppSmokeTest, ModelCheckRunsHeadlessThroughTheEditorModule )
{
    const AppRunResult result = runApp( "--check-models", "Model check:" );
    SW_ASSERT_TRUE_MSG( result._bLaunched, "App 을 띄우지 못했습니다" );
#if defined( SW_SHIPPING )
    SW_EXPECT_TRUE( result._exitCode != 0 );
    SW_EXPECT_TRUE( result._listMarkedLine.empty() );
#else
    SW_EXPECT_TRUE_MSG( result._exitCode == 0, result._firstErrorLine.c_str() );
    SW_EXPECT_TRUE_MSG( result._errorCount == 0, result._firstErrorLine.c_str() );
    SW_ASSERT_EQUAL( size_t( 1 ), result._listMarkedLine.size() );
    SW_EXPECT_TRUE_MSG( result._listMarkedLine[0].find( "0 problems" ) != string::npos, result._listMarkedLine[0].c_str() );
#endif
}

#if defined( SW_DEBUG ) && !defined( SW_SHIPPING )
/**
 * @brief [AppSmokeTest] 헤드리스 작업도 종료 때 누수를 기준선과 견준다(기준선 없는 통째 덤프가 아니다)
 * @details 보통 실행은 기동 끝에서 CRT 누수 기준선을 잡는다. 헤드리스 작업(셰이더 · 씬 쿠킹, 텍스처 임포트 · 대조)은 그 자리에 닿지 않고 끝나,
 *          기준선이 없으면 종료 때 살아 있는 블록 전부(수백 개)를 누수로 찍는다 — 진짜 누수가 그 소음에 묻힌다. 작업 직전에 기준선을 잡는다.
 */
SW_TEST_CASE( AppSmokeTest, HeadlessRunReportsLeaksAgainstABaseline )
{
    const AppRunResult result = runApp( "--check-textures", "[MemoryLeak]" );
    SW_ASSERT_TRUE_MSG( result._bLaunched, "App 을 띄우지 못했습니다" );
    SW_EXPECT_TRUE_MSG( result._exitCode == 0, result._firstErrorLine.c_str() );

    bool bBaselineCaptured = false;
    bool bComparedClean    = false;
    for ( const string& line : result._listMarkedLine )
    {
        SW_EXPECT_TRUE_MSG( line.find( "no baseline" ) == string::npos, line.c_str() );
        bBaselineCaptured = bBaselineCaptured || line.find( "baseline captured" ) != string::npos;
        bComparedClean    = bComparedClean || line.find( "no CRT leaks" ) != string::npos;
    }
    SW_EXPECT_TRUE_MSG( bBaselineCaptured, "헤드리스 실행이 누수 기준선을 잡지 않았다" );
    SW_EXPECT_TRUE_MSG( bComparedClean, "헤드리스 실행의 종료 누수 보고가 기준선 대비 깨끗하지 않다" );
}

/**
 * @brief [AppSmokeTest] 보통 실행을 끝내면 어느 메모리 태그도 기동 뒤 기준선보다 크지 않다
 * @details 종료 끝(`EngineBootstrap::shutdown`)이 태그별로 기준선과 견줘 `[MemoryLeak]` 줄을 남긴다. 프로세스 정적 저장소(이름 풀 · 트랜스폼 페이지 ·
 *          경로 캐시)가 기동 뒤 자란 몫을 돌려주지 않으면 "tag … grew by" 줄이 나온다. CRT 검사는 합계만 봐 이것을 "no CRT leaks" 라고 한다.
 */
SW_TEST_CASE( AppSmokeTest, ShutdownReturnsEveryTagToTheBaseline )
{
    const AppRunResult result = runApp( "-gv_profileFrames=5 -dx12", "[MemoryLeak]" );
    SW_ASSERT_TRUE_MSG( result._bLaunched, "App 을 띄우지 못했습니다" );
    if ( result._bBackendUnusableHere )
        SW_TEST_SKIP( "DX12 is not usable on this machine" );
    SW_EXPECT_EQUAL( 0, result._exitCode );

    bool bComparedTags = false;
    for ( const string& line : result._listMarkedLine )
    {
        SW_EXPECT_TRUE_MSG( line.find( "grew by" ) == string::npos, line.c_str() );
        bComparedTags = bComparedTags || line.find( "no memory tag grew" ) != string::npos;
    }
    SW_EXPECT_TRUE_MSG( bComparedTags, "종료 보고가 태그를 기준선과 견주지 않았다" );
}
#endif

/**
 * @brief [AppSmokeTest] 벤치 큐브 한 장면이 백엔드마다 골든 이미지와 같다(`Test/AppTest/Golden`)
 * @details `-gv_benchAnimate=0` 이면 프레임이 결정적이라 기준 이미지를 둘 수 있다. 기준은 **백엔드마다** 하나다 — 백엔드끼리 래스터 결과가
 *          조금씩 다를 수 있다(언리얼 · 유니티의 스크린샷 비교도 RHI 별 기준을 둔다). 작은 창(256×144)으로 그려 파일을 작게 둔다.
 *          채널 차이 2 까지는 같다고 본다. 기준을 새로 뜨려면 `SW_UPDATE_GOLDEN=1` 로 이 케이스를 돌린다 — 그 판은 비교 대신 기준을 쓴다.
 */
SW_TEST_CASE( AppSmokeTest, BenchFrameMatchesGoldenImage )
{
    constexpr uint32 kChannelTolerance = 2;

    struct GoldenScene
    {
        const utf8* _pName;
        const utf8* _pArgument;
    };
    constexpr GoldenScene kArrScene[] = {
        {     "opaque",  "-gv_benchTransparent=0"},
        {"transparent", "-gv_benchTransparent=25"},
    };

    struct GoldenBackend
    {
        const utf8* _pName;
        const utf8* _pSwitch;
    };
#if defined( SW_SHIPPING )
    // 배포본은 백엔드를 하나만 링크한다 — 스위치 없이 그 백엔드의 기준과 비교한다.
    #if defined( SW_RHI_TARGET_DX11 )
    constexpr GoldenBackend kArrBackend[] = {
        { "dx11", "" }
    };
    #elif defined( SW_RHI_TARGET_VULKAN )
    constexpr GoldenBackend kArrBackend[] = {
        { "vk", "" }
    };
    #elif defined( SW_RHI_TARGET_OPENGL )
    constexpr GoldenBackend kArrBackend[] = {
        { "gl", "" }
    };
    #else
    constexpr GoldenBackend kArrBackend[] = {
        { "dx12", "" }
    };
    #endif
#else
    #define SW_APP_GOLDEN_BACKEND( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) { CommandLineName, "-" CommandLineName },
    constexpr GoldenBackend kArrBackend[] = { SW_RHI_BACKEND_TABLE( SW_APP_GOLDEN_BACKEND ) };
    #undef SW_APP_GOLDEN_BACKEND
#endif

    // 기준은 그 플랫폼의 드라이버가 그린 것이다. 윈도우 밖은 이름에 플랫폼을 붙이고, 아직 뜬 적이 없으면 비교하지 않는다(경고만).
#if defined( SW_PLATFORM_WINDOWS )
    constexpr const utf8* kGoldenPlatformSuffix = "";
    constexpr bool        kMissingGoldenFails   = true;
#else
    constexpr const utf8* kGoldenPlatformSuffix = ".linux";
    constexpr bool        kMissingGoldenFails   = false;
#endif

    // 기준 그림은 Empty 게임의 벤치 장면이다 — 다른 게임 프리셋의 App 은 자기 첫 씬(아레나 · 공원 …)을 그려 비교할 것이 없다.
    if ( test::AppTestUtil::readActivePackRoot() != "game/empty" )
        SW_TEST_SKIP( "golden images are the Empty game's bench scene; this preset runs another game" );

    const string goldenDirectory = findGoldenDirectory();
    SW_ASSERT_TRUE_MSG( goldenDirectory.empty() == false, "Test/AppTest/Golden 을 찾지 못했습니다 — 작업 폴더가 build/<프리셋>/Bin 입니까?" );

    const utf8* pUpdate = std::getenv( "SW_UPDATE_GOLDEN" );
    const bool  bUpdate = pUpdate != nullptr && pUpdate[0] == '1';

    uint32 checkedCount = 0;
    for ( const GoldenBackend& backend : kArrBackend )
    {
        for ( const GoldenScene& scene : kArrScene )
        {
            string imageName{ scene._pName };
            imageName += "_";
            imageName += backend._pName;
            const string screenshotPath = test::makeTempPath( imageName + ".ppm" );

            string arguments{ "-W=256 -H=144 -gv_benchMeshes=8 -gv_benchAnimate=0 -gv_profileFrames=20 " };
            arguments += scene._pArgument;
            arguments += " \"-gv_screenshot=";
            arguments += screenshotPath;
            arguments += "\" ";
            arguments += backend._pSwitch;

            const AppRunResult result = runApp( arguments );
            SW_EXPECT_TRUE_MSG( result._bLaunched, "App 을 띄우지 못했습니다" );
            if ( result._bLaunched == false || result._bBackendUnusableHere )
                continue;
            SW_EXPECT_TRUE_MSG( result._exitCode == 0, imageName.c_str() );

            vector<uint8> screenshotBytes;
            PpmImage      actual{};
            const bool    bRead = FileUtil::readFile( screenshotPath, screenshotBytes ) && parsePpm( screenshotBytes, actual );
            SW_EXPECT_TRUE_MSG( bRead, ( "스크린샷을 읽지 못했습니다: " + imageName ).c_str() );
            if ( bRead == false )
                continue;

            const string goldenPath = FileUtil::joinPath( goldenDirectory, imageName + kGoldenPlatformSuffix + ".ppm.z" );
            if ( bUpdate )
            {
                vector<uint8> goldenBytes;
                SW_EXPECT_TRUE( CompressionStream::compressBuffer( screenshotBytes.data(), screenshotBytes.size(), goldenBytes, CompressionCodecType::Zlib, 9,
                                                                   &getGoldenCodecRegistry() ) );
                SW_EXPECT_TRUE_MSG( FileUtil::writeFile( goldenPath, goldenBytes.data(), goldenBytes.size() ), goldenPath.c_str() );
                SW_LOG_WARNING( "Golden image updated: %#", goldenPath.c_str() );
                ++checkedCount;
                continue;
            }

            if ( kMissingGoldenFails == false && FileUtil::exists( goldenPath ) == false )
            {
                SW_LOG_WARNING( "No golden image for this platform yet - record it with SW_UPDATE_GOLDEN=1: %#", goldenPath.c_str() );
                continue;
            }

            vector<uint8> goldenBytes;
            vector<uint8> goldenPpmBytes;
            PpmImage      expected{};
            const bool    bGolden = FileUtil::readFile( goldenPath, goldenBytes ) &&
                                 CompressionStream::decompressBuffer( goldenBytes.data(), goldenBytes.size(), goldenPpmBytes, &getGoldenCodecRegistry() ) &&
                                 parsePpm( goldenPpmBytes, expected );
            SW_EXPECT_TRUE_MSG( bGolden, ( "골든 이미지를 읽지 못했습니다(SW_UPDATE_GOLDEN=1 로 뜬다): " + goldenPath ).c_str() );
            if ( bGolden == false )
                continue;

            const GoldenDifference difference = compareImage( expected, actual, kChannelTolerance );
            string                 message    = imageName;
            message += ": ";
            message += sw::to_string( difference._pixelOverTolerance );
            message += " pixel(s) differ by more than the tolerance, max channel difference ";
            message += sw::to_string( difference._maxChannelDifference );
            SW_EXPECT_TRUE_MSG( difference._pixelOverTolerance == 0, message.c_str() );
            if ( difference._pixelOverTolerance != 0 )
            {
                // 케이스 임시 폴더는 케이스가 끝나면 지워진다 — 진 그림은 build/<프리셋>/GoldenDiff 에 남겨 열어 볼 수 있게 한다.
                const string diffDirectory = FileUtil::joinPath( FileUtil::getDirectoryPart( FileUtil::getCurrentPath() ), "GoldenDiff" );
                FileUtil::ensureDirectoryExists( diffDirectory );
                const string keptPath = FileUtil::joinPath( diffDirectory, imageName + ".ppm" );
                SW_EXPECT_TRUE( FileUtil::writeFile( keptPath, screenshotBytes.data(), screenshotBytes.size() ) );
                SW_LOG_WARNING( "Golden mismatch kept at %#", keptPath.c_str() );
            }
            ++checkedCount;
        }
    }

    if ( checkedCount == 0 )
        SW_TEST_SKIP( "no usable RHI backend on this machine — run where a GPU and driver exist" );
}
