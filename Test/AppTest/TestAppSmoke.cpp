#include "pch.h"

#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"

#include "TestFramework/TestFramework.h"

#include <chrono>

using namespace sw;

// SW_TEST_REQUIRES_HOST( AppSmokeTest ): 실제 App.exe 를 띄운다 — GPU · 창 · 셰이더가 필요하다. CI 러너엔 없다.
//
// ------------------------------------------------------------------------------
// 1) AppSmokeTest — "실기동 게이트" 를 자동화한 것
//
// 이 저장소의 실질적인 최종 검증은 **App 을 띄워 보는 것**이었다(백로그 0절: 네 백엔드 × 에디터
// 유무, 종료 코드 0, 로그에 `[Error]` 0건). 그런데 그 절차가 문서에만 있어서 사람이 기억해야
// 돌았고, `EngineLoop` 은 어떤 단위 테스트도 돌리지 않는다 — **엔진 기동 전체가 자동 그물 밖**에
// 있었다. 그 절차를 그대로 테스트로 옮긴다.
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
// 만들어지므로 그 앞에서 부른 `SW_LOG_ERROR` 는 아무 데도 남지 않는다(변이로 확인했다 — 그 자리에
// 심은 에러 줄은 출력에 나타나지 않았다). 그 구간의 실패는 **종료 코드로만** 드러난다.
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
        bool   _bLaunched{ false };
    };

    /** @brief 플랫폼별 실행 파일 이름. */
    const utf8* getAppExecutableName()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return "App.exe";
#else
        return "App";
#endif
    }

    /**
     * @brief 띄울 App 실행 파일의 **절대 경로**. 못 찾으면 빈 문자열.
     * @details 이름만 넘기면 안 된다 — 배포 구성에서는 테스트 바이너리가 `TestBin` 에 있고
     *          `CreateProcess` 는 **부르는 실행 파일의 폴더**부터 찾으므로 `Bin/App.exe` 를 못 본다
     *          (실제로 Shipping 에서 "띄우지 못했습니다" 로 걸렸다). 작업 폴더(`Bin`)와 테스트
     *          바이너리 폴더를 순서대로 본다.
     */
    string findAppExecutablePath()
    {
        const string workingCandidate = FileUtil::joinPath( FileUtil::getCurrentPath(), getAppExecutableName() );
        if ( FileUtil::fileExists( workingCandidate ) )
            return workingCandidate;

        const string executableFolder = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );
        const string siblingCandidate = FileUtil::joinPath( executableFolder, getAppExecutableName() );
        if ( FileUtil::fileExists( siblingCandidate ) )
            return siblingCandidate;

        return {};
    }

    /**
     * @brief App 을 한 판 돌리고 종료 코드와 `[Error]` 줄 수를 돌려줍니다.
     * @details 작업 디렉터리는 **바꾸지 않는다** — 테스트의 작업 폴더가 이미 `Bin` 이고(ctest 가 그렇게
     *          돌린다) App 도 거기서 `Resource/` 를 찾아 올라간다. 배포본에서는 테스트 바이너리만
     *          `TestBin` 에 있고 작업 폴더는 여전히 `Bin` 이라 이 전제가 양쪽에서 같다.
     */
    AppRunResult runApp( string_view arguments )
    {
        AppRunResult result{};

        const string executablePath = findAppExecutablePath();
        if ( executablePath.empty() )
            return result;

        // 경로에 공백이 있을 수 있다 — 첫 토큰은 따옴표로 감싼다.
        string command{ "\"" };
        command += executablePath;
        command += "\" ";
        command += arguments;

        Process process;
        if ( process.launch( command ) == false )
            return result;
        result._bLaunched = true;

        string line;
        while ( process.readOutputLine( line ) )
        {
            ++result._lineCount;
            if ( line.find( "[Error]" ) == string::npos )
                continue;

            ++result._errorCount;
            if ( result._firstErrorLine.empty() )
                result._firstErrorLine = line;
        }

        result._exitCode = process.waitForExit();
        return result;
    }

    /** @brief 한 판을 돌리고 계약(종료 코드 0 · 에러 0 · 출력 있음)을 검사합니다. */
    void expectCleanRun( string_view arguments )
    {
        const AppRunResult result = runApp( arguments );

        SW_EXPECT_TRUE_MSG( result._bLaunched, "App 을 띄우지 못했습니다 — 작업 폴더(Bin)나 테스트 바이너리 옆에 실행 파일이 있습니까?" );
        if ( result._bLaunched == false )
            return;

        SW_EXPECT_TRUE_MSG( result._exitCode == 0, "App 이 0 이 아닌 코드로 끝났습니다" );
        SW_EXPECT_TRUE_MSG( result._errorCount == 0, result._firstErrorLine.empty() ? "로그에 [Error] 가 있습니다" : result._firstErrorLine.c_str() );
#if !defined( SW_SHIPPING )
        // 출력이 한 줄도 없으면 "조용히 성공" 이 아니라 아무것도 안 한 것이다.
        // **배포본에서는 반대다** — Info 로그가 컴파일에서 빠져 깨끗한 실행이 곧 출력 0줄이다.
        SW_EXPECT_TRUE_MSG( result._lineCount > 0, "App 이 로그를 한 줄도 남기지 않았습니다 — 정말 돌았습니까?" );
#endif
    }
} // namespace

/**
 * @brief [AppSmokeTest] 네 백엔드에서 기동 → 프레임 → 종료가 깨끗한가
 * @details 백엔드마다 따로 본다 — 한 판에 묶으면 "어느 백엔드가 깨졌나" 를 로그에서 다시 찾아야 한다.
 *          `-gv_rhiBackend` 는 App 이 무시하므로 반드시 `-dx12 / -dx11 / -vk / -gl` 을 쓴다.
 */
SW_TEST_CASE( AppSmokeTest, EveryBackendStartsRendersAndExitsCleanly )
{
#if defined( SW_SHIPPING )
    // **배포본은 백엔드를 하나만 링크한다**(`SW_SHIPPING_RHI_BACKEND`, 윈도우 기본 DX12 —
    // `Source/Engine/CMakeLists.txt`). 없는 백엔드를 요구하면 `RHIBackendRegistry` 가 거절하고
    // App 이 0 이 아닌 코드로 끝난다. 그래서 여기서는 스위치 없이 "이 빌드가 가진 것" 으로 돌린다.
    constexpr const utf8* kArrBackendSwitch[] = { "" };
#else
    constexpr const utf8* kArrBackendSwitch[] = { "-dx12", "-dx11", "-vk", "-gl" };
#endif
    for ( const utf8* pSwitch : kArrBackendSwitch )
    {
        string arguments{ "-gv_profileFrames=20 " };
        arguments += pSwitch;
        expectCleanRun( arguments );
    }
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
    for ( const utf8* pSwitch : kArrBackendSwitch )
    {
        string arguments{ "-gv_profileFrames=20 -EnableEditor " };
        arguments += pSwitch;
        expectCleanRun( arguments );
    }
}
#endif
