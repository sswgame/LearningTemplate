/**
 * @file Test/AppTest/AppTestUtil.h
 * @brief AppTest 가 빌드된 App 실행 파일을 찾아 띄우는 도우미입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "TestFramework/Harness/TestProcessRun.h"

namespace test
{
    /**
     * @brief 백엔드를 고른 App 실행 한 판의 판정입니다. 종료 코드는 러너에 내주지 않는다 — 건너뜀 정책(요구 백엔드면 실패)을 러너가 다시 정할 수 없게 한다.
     * @details `Failed` 이면 실패가 이미 기록됐다(인자 · 종료 코드 · 출력 끝이나 시나리오 줄과 함께). 러너는 그 판의 다른 검사만 건너뛴다.
     */
    enum class AppRunOutcome : uint8
    {
        Passed,          ///< 종료 코드 0(시나리오는 허용하지 않은 로그 줄도 없다)
        Failed,          ///< 실패를 기록했다 — 0 이 아닌 종료, 시한 초과, 못 띄움, 요구 백엔드(`--required_backends=`)가 서지 못함
        BackendUnusable, ///< 요구하지 않은 백엔드가 이 기계에서 서지 못했다(종료 코드 77) — 그 판을 건너뛴다
        Skipped,         ///< 건너뛰었다 — 시나리오가 스스로(종료 코드 13: 전경 창을 못 얻음 · 배포본에 없는 개발 명령), 또는 배포본에서 개발 변수(`gv_`)가 필요한 실행(띄우지 않고 `[ PARTSKIP ]` 로 보고). 백엔드와 무관한 환경 탓이다
    };

    /** @brief 그 판이 이 기계에서 실제로 돌았으면(통과든 실패든) true 입니다. 모든 판이 false 일 때만 케이스가 `SW_TEST_SKIP` 으로 남는다. */
    constexpr bool hasRunHere( AppRunOutcome outcome )
    {
        return outcome == AppRunOutcome::Passed || outcome == AppRunOutcome::Failed;
    }

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
         * @brief App 을 `arguments` 로 띄우는 명령줄입니다. 실행 파일을 못 찾으면 빈 문자열입니다.
         * @details 경로에 공백이 있을 수 있어 첫 토큰(실행 파일 경로)은 따옴표로 감싸고, 사람이 지켜보지 않으므로 `-unattended` 를 붙인다
         *          (에디터를 켜도 단언 대화상자를 걸지 않는다 — Debug 단언은 멈춘다).
         */
        static sw::string makeAppCommand( sw::string_view arguments );
        // 이 구성에 없는 기능이 필요한 인자(`-EnableEditor` · `-game=`)는 케이스가 `SW_TEST_REQUIRES_FEATURE` 로 먼저 선언해야 한다 — 선언이 빠졌으면 실패를 기록하고 빈 명령을 돌려준다.

        /**
         * @brief App 을 `arguments` 로 한 판 돌리고 줄마다 @p onLine( process, line ) 을 부릅니다. 못 찾거나 못 띄우면 `_bLaunched` 가 false 다.
         * @details 작업 폴더는 바꾸지 않는다 — 테스트의 작업 폴더가 `Bin` 이고 App 도 거기서 `Resource/` 를 찾아 올라간다. 시한을 넘기면 죽인다.
         *          **렌더러를 세우지 않는 실행(쿠킹 · `--check-*`)만 쓴다.** 백엔드를 고르는 실행은 `runAppOnBackend` 로 돌린다 — 종료 코드를 직접 보면
         *          요구 백엔드가 서지 못한 판을 건너뜀으로 넘길 수 있다.
         */
        template <typename TOnLine>
        static ProcessRunResult runApp( sw::string_view arguments, uint32 timeoutSeconds, TOnLine&& onLine )
        {
            const sw::string command = makeAppCommand( arguments );
            if ( command.empty() )
                return ProcessRunResult{};
            return runProcessWithDeadline( command, sw::ProcessOptions{}, timeoutSeconds, onLine );
        }

        /**
         * @brief 백엔드 스위치(`-dx11` · `-dx12` · `-vk` · `-gl`, 없으면 빌드 기본)가 든 @p arguments 로 App 을 한 판 돌려 판정합니다. 줄마다 @p onLine 을 부른다.
         * @details 종료 코드 77 이면 `reportUnusableBackend` 를 지난다 — 그 백엔드가 요구 백엔드면 `Failed`, 아니면 `BackendUnusable` 이다.
         *          0 이 아닌 다른 종료 · 시한 초과 · 못 띄움은 실패를 기록하고 `Failed` 다(메시지에 인자와 출력 끝 몇 줄).
         */
        template <typename TOnLine>
        [[nodiscard]] static AppRunOutcome runAppOnBackend( sw::string_view arguments, uint32 timeoutSeconds, TOnLine&& onLine )
        {
            const ProcessRunResult run = runApp( arguments, timeoutSeconds, onLine );
            return judgeRun( run, arguments, run.getOutputTail() );
        }

        /**
         * @brief 게임 @p gameName 의 프리셋(`Config/Game/<게임>.json`)의 `_packRoot`(`game/<팩>`)입니다. 못 읽으면 빈 문자열입니다.
         * @details `-game=` 없이 띄운 App 은 빌드의 활성 게임(`GameSelection::getName`)을 그린다 — 게임에 매인 테스트(시나리오 · 골든 이미지)가 고른다.
         */
        static sw::string readPackRoot( sw::string_view gameName );

        /** @brief 이 빌드가 함께 지은 게임 팩(`Bin/Games/<게임>/`, `SW_GAMES`)의 이름입니다(이름순). 없으면 비어 있다(Shipping · `SW_GAMES` 비움). */
        static sw::vector<sw::string> collectBuiltGamePacks();
        // `SW_TEST_REQUIRES_FEATURE( SourceResources )` 를 선언한 케이스만 부른다 — Shipping 에서 부르면 실패를 기록한다.

        // 시나리오를 띄우는 App 인자의 글 — 정본은 이 아래 상수 줄들이다. `py -3 -m Scripts scenario`(`Scripts/qa/ScenarioRun.py`)가 이 줄들을 읽어 같은 인자를 만든다
        // (`Scripts/common/ScenarioArguments.py`). 이름이나 `static constexpr const utf8* kXxx = "…";` 줄 모양을 바꾸면 스크립트가 멈춘다. 상수를 더하면 그 모듈의 칸도 더한다.
        /** @brief 사람이 지켜보지 않는 실행(Debug 단언이 대화상자를 걸지 않는다). */
        static constexpr const utf8* kUnattendedArgument = "-unattended";
        /** @brief 시나리오 경로(쉼표 목록 가능)를 주는 인자의 앞머리. */
        static constexpr const utf8* kScenarioArgumentPrefix = "-scenario=";
        /** @brief 보고 JSON 경로를 주는 인자의 앞머리. */
        static constexpr const utf8* kScenarioReportArgumentPrefix = "-scenario-report=";
        /** @brief 에디터를 켜는 인자. */
        static constexpr const utf8* kEditorArgument = "-EnableEditor";
        /** @brief 에디터 상태 폴더(사용자의 `Saved/Editor` 대신 비운 폴더)를 주는 인자의 앞머리. */
        static constexpr const utf8* kEditorStateDirArgumentPrefix = "-gv_editorStateDir=";
        /** @brief 게임 팩을 고르는 인자의 앞머리. */
        static constexpr const utf8* kGameArgumentPrefix = "-game=";
        /** @brief 작업 폴더(`Bin`) 아래 보고 · 로그 · 산출물 폴더. */
        static constexpr const utf8* kAutomationFolder = "Saved/Automation";
        /** @brief `kAutomationFolder` 아래 에디터 상태 폴더들의 부모 이름. */
        static constexpr const utf8* kEditorStateSubfolder = "EditorState";

        /** @brief 시나리오가 아닌 App 한 판(스모크 · 쿠킹)의 프로세스 시한(초) — 멈춘 App 이 CTest 시한까지 붙잡지 않게 넉넉히 잡은 그물이다. */
        static constexpr uint32 kAppRunTimeoutSeconds = 600;
        /** @brief 시나리오 하나의 프로세스 시한(초) — 넘으면 죽이고 -1 입니다(시나리오의 프레임 시한이 먼저 끝내야 한다). */
        static constexpr uint32 kScenarioTimeoutSeconds = 180;
        /**
         * @brief 체인의 단계 하나에 주는 시한(초)입니다. 체인의 시한은 이것 × 단계 수와 `kScenarioTimeoutSeconds` 가운데 큰 쪽입니다.
         * @details 에디터 체인(125 단계, Debug)은 혼자 돌아도 2 분 남짓이라 고정 180 초로는 부하에 따라 끝 단계에서 죽었다(-1).
         */
        static constexpr uint32 kChainStageTimeoutSeconds = 5;

        /**
         * @brief 자동화 시나리오 @p scenarioPath 를 백엔드 스위치 @p pBackendSwitch(`-dx12` · 빈 글 = 빌드 기본)로 돌려 판정합니다(`runAppOnBackend` 와 같은 정책).
         * @details 로그는 `Saved/Automation/<시나리오 이름 조각>_<백엔드>.log`, 보고는 `.json`. `[Scenario]` 줄은 @p outScenarioLines 에 모읍니다.
         *          @p extraArguments 는 명령줄 끝에 붙는다(에디터 시나리오의 `-EnableEditor`). 실패 메시지와 건너뜀 로그에는 시나리오 · 백엔드 · 종료 코드 · `[Scenario]` 줄이 든다.
         *          시나리오가 `<Launch>` 로 `gv_` 개발 변수를 선언했거나 @p extraArguments 에 `-gv_` 가 있는데 이 빌드가 배포본이면 App 을 띄우지 않고
         *          `Skipped` 로 돌려준다(`test::skipPartIfFeatureMissing` 가 케이스 보고에 시나리오와 이유를 남긴다).
         */
        [[nodiscard]] static AppRunOutcome runScenario( const sw::string& scenarioPath, const utf8* pBackendSwitch, sw::string& outScenarioLines,
                                                        sw::string_view extraArguments = {} );
        /**
         * @brief 시나리오 여럿을 **한 프로세스**에서 차례로 돌립니다(`-scenario=a,b,c`). 앞 단계가 통과해야 다음으로 가고, 앞 단계가 만든 상태를 이어받습니다.
         * @param chainName 로그 · 보고 파일 이름(`Saved/Automation/chain_<이름>_<백엔드>.log` · `.<단계>.json`)
         * @return 체인의 판정 — 체인의 종료 코드(전부 통과면 0, 아니면 처음 통과하지 못한 단계의 코드)로 정한다. `[Scenario] chain stage n/m …` 줄이 @p outScenarioLines 에 모입니다.
         */
        [[nodiscard]] static AppRunOutcome runScenarioChain( const sw::vector<sw::string>& listScenarioPath, const sw::string& chainName, const utf8* pBackendSwitch,
                                                             sw::string& outScenarioLines, sw::string_view extraArguments = {} );
        /** @brief `runScenario` 가 그 시나리오 · 백엔드로 쓴 보고 경로(`Saved/Automation/<이름 조각>_<백엔드>.json`)입니다. */
        static sw::string makeReportPath( const sw::string& scenarioPath, const utf8* pBackendSwitch );
        /**
         * @brief 보고의 `records` 줄(`<Record>` — `frame 300 Scene.TransformHash = 12345`)을 읽습니다. 파일이 없거나 그 항목이 없으면 빈 목록입니다.
         * @details 같은 시나리오는 백엔드마다 같은 줄을 내야 한다(게임 상태는 CPU 에서만 정해진다) — `AppScenarioTest` 가 비교한다.
         */
        static sw::vector<sw::string> readReportRecords( const sw::string& reportPath );
        /**
         * @brief 프로세스 출력 @p output 에서 로그 게이트가 보는 줄(`[Warning]` · `[Error]`) 가운데 처리하지 않은 것을 돌려줍니다.
         * @details 처리는 `AutomationLogPolicy` 의 전역 목록에서 @p scenarioArgument(경로 하나 또는 쉼표 목록) 시나리오들 범위의 항목과 그 시나리오들의 `<ExpectLog>` 다.
         *          줄의 id 는 텍스트 줄의 `[id:…]` 꼬리표(`LogIDTag`)에서 읽으므로 id 없는 줄은 허용 목록으로 가릴 수 없다.
         *          실행기는 시나리오 구간만 보므로, 기동 · 종료 구간의 줄은 여기서 잡는다. 목록을 읽지 못하면 그 이유를 한 줄로 돌려준다.
         */
        static sw::vector<sw::string> collectUnexpectedLogLines( const sw::string& output, const sw::string& scenarioArgument );

    private:
        /**
         * @brief 실행 한 판 @p run 을 판정합니다 — 종료 코드를 해석하는 곳은 여기 하나다. 77 이면 @p arguments 의 백엔드 스위치로 `reportUnusableBackend` 를 부른다.
         * @param detail 실패 메시지 · 건너뜀 로그 끝에 붙일 글(출력 끝 몇 줄이나 `[Scenario]` 줄)
         */
        [[nodiscard]] static AppRunOutcome judgeRun( const ProcessRunResult& run, sw::string_view arguments, const sw::string& detail );
        /**
         * @brief `-scenario=` 인자(경로 하나 또는 쉼표 목록)를 받아 App 을 띄우고 판정합니다.
         * @details 종료 코드 0(이나 14)이어도 출력에 처리하지 않은 Warning · Error 가 있으면 그 줄을 @p outScenarioLines 에 붙이고 실패(15, `UnhandledLog`)로 판정한다.
         */
        [[nodiscard]] static AppRunOutcome runScenarioArgument( const sw::string& scenarioArgument, const sw::string& outputStem, const utf8* pBackendSwitch,
                                                                sw::string& outScenarioLines, sw::string_view extraArguments, uint32 timeoutSeconds );
    };
} // namespace test
