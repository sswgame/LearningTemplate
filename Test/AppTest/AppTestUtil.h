/**
 * @file Test/AppTest/AppTestUtil.h
 * @brief AppTest 가 빌드된 App 실행 파일을 찾아 띄우는 도우미입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    class Process;
} // namespace sw

namespace test
{
    /** @brief 빌드된 App 실행 파일을 찾고 띄웁니다(`AppCookTest` · `AppSmokeTest`). */
    struct AppTestUtil
    {
        /**
         * @brief 띄울 App 실행 파일의 **절대 경로**입니다. 못 찾으면 빈 문자열입니다.
         * @details 이름만 넘기면 안 된다 — 배포 구성에서는 테스트 바이너리가 `TestBin` 에 있고 `CreateProcess` 는 **부르는 실행 파일의
         *          폴더**부터 찾으므로 `Bin/App.exe` 를 못 본다. 작업 폴더(`Bin`)와 테스트 바이너리 폴더를 순서대로 본다.
         */
        static sw::string findAppExecutablePath();

        /**
         * @brief App 을 `arguments` 로 띄웁니다. 실행 파일을 못 찾았거나 띄우지 못하면 false 입니다.
         * @details 작업 폴더는 바꾸지 않는다 — 테스트의 작업 폴더가 `Bin` 이고 App 도 거기서 `Resource/` 를 찾아 올라간다.
         *          경로에 공백이 있을 수 있어 첫 토큰(실행 파일 경로)은 따옴표로 감싼다.
         */
        [[nodiscard]] static bool launchApp( sw::Process& outProcess, sw::string_view arguments );

        /**
         * @brief 게임 @p gameName 의 프리셋(`Config/Game/<게임>.json`)의 `_packRoot`(`game/<팩>`)입니다. 못 읽으면 빈 문자열입니다.
         * @details `-game=` 없이 띄운 App 은 빌드의 활성 게임(`GameSelection::getName`)을 그린다 — 게임에 매인 테스트(시나리오 · 골든 이미지)가 고른다.
         */
        static sw::string readPackRoot( sw::string_view gameName );

        /** @brief 이 빌드가 함께 지은 게임 팩(`Bin/Games/<게임>/`, `SW_GAMES`)의 이름입니다(이름순). 없으면 비어 있다(Shipping · `SW_GAMES` 비움). */
        static sw::vector<sw::string> collectBuiltGamePacks();

        /** @brief 시나리오 하나의 프로세스 시한(초) — 넘으면 죽이고 -1 입니다(시나리오의 프레임 시한이 먼저 끝내야 한다). */
        static constexpr uint32 kScenarioTimeoutSeconds = 180;
        /**
         * @brief 체인의 단계 하나에 주는 시한(초)입니다. 체인의 시한은 이것 × 단계 수와 `kScenarioTimeoutSeconds` 가운데 큰 쪽입니다.
         * @details 에디터 체인(125 단계, Debug)은 혼자 돌아도 2 분 남짓이라 고정 180 초로는 부하에 따라 끝 단계에서 죽었다(-1).
         */
        static constexpr uint32 kChainStageTimeoutSeconds = 5;
        /** @brief 시나리오가 건너뜀으로 끝났다(전경 창을 못 얻음 등 — `AutomationResult::Skipped`). */
        static constexpr int32 kSkippedExitCode = 13;
        /** @brief App 을 띄우지 못했다(실행 파일이 없다). */
        static constexpr int32 kNotLaunchedExitCode = -1000;

        /**
         * @brief 자동화 시나리오 @p scenarioPath 를 백엔드 스위치 @p pBackendSwitch(`-dx12` · 빈 글 = 빌드 기본)로 돌려 종료 코드를 돌려줍니다.
         * @details 로그는 `Saved/Automation/<시나리오 이름 조각>_<백엔드>.log`, 보고는 `.json`. `[Scenario]` 줄은 @p outScenarioLines 에 모읍니다.
         *          @p extraArguments 는 명령줄 끝에 붙는다(에디터 시나리오의 `-EnableEditor`).
         * @return 띄우지 못하면 `kNotLaunchedExitCode`, 시한을 넘겨 죽였으면 -1
         */
        static int32 runScenario( const sw::string& scenarioPath, const utf8* pBackendSwitch, sw::string& outScenarioLines,
                                  sw::string_view extraArguments = {} );
        /**
         * @brief 시나리오 여럿을 **한 프로세스**에서 차례로 돌립니다(`-scenario=a,b,c`). 앞 단계가 통과해야 다음으로 가고, 앞 단계가 만든 상태를 이어받습니다.
         * @param chainName 로그 · 보고 파일 이름(`Saved/Automation/chain_<이름>_<백엔드>.log` · `.<단계>.json`)
         * @return 체인의 종료 코드 — 전부 통과면 0, 아니면 처음 통과하지 못한 단계의 코드입니다. `[Scenario] chain stage n/m …` 줄이 @p outScenarioLines 에 모입니다.
         */
        static int32 runScenarioChain( const sw::vector<sw::string>& listScenarioPath, const sw::string& chainName, const utf8* pBackendSwitch,
                                       sw::string& outScenarioLines, sw::string_view extraArguments = {} );
        /** @brief 이 기계에서 돌릴 수 없어 건너뛸 종료 코드(13 건너뜀 · 77 백엔드 없음 · 못 띄움)면 true 입니다. */
        static bool isSkippedExitCode( int32 exitCode );
        /** @brief `runScenario` 가 그 시나리오 · 백엔드로 쓴 보고 경로(`Saved/Automation/<이름 조각>_<백엔드>.json`)입니다. */
        static sw::string makeReportPath( const sw::string& scenarioPath, const utf8* pBackendSwitch );
        /**
         * @brief 보고의 `records` 줄(`<Record>` — `frame 300 Scene.TransformHash = 12345`)을 읽습니다. 파일이 없거나 칸이 없으면 빈 목록입니다.
         * @details 같은 시나리오는 백엔드마다 같은 줄을 내야 한다(게임 상태는 CPU 에서만 정해진다) — `AppScenarioTest` 가 견준다.
         */
        static sw::vector<sw::string> readReportRecords( const sw::string& reportPath );

    private:
        /** @brief `-scenario=` 인자(경로 하나 또는 쉼표 목록)를 받아 App 을 띄우고 종료 코드를 돌려줍니다. */
        static int32 runScenarioArgument( const sw::string& scenarioArgument, const sw::string& outputStem, const utf8* pBackendSwitch, sw::string& outScenarioLines,
                                          sw::string_view extraArguments, uint32 timeoutSeconds );
    };
} // namespace test
