/**
 * @file Test/AppTest/AppTestUtil.h
 * @brief AppTest 가 빌드된 App 실행 파일을 찾아 띄우는 도우미입니다.
 */
#pragma once
#include "Core/Container/string.h"

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
         * @details 이름만 넘기면 안 된다 — 배포 구성에서는 시험 바이너리가 `TestBin` 에 있고 `CreateProcess` 는 **부르는 실행 파일의
         *          폴더**부터 찾으므로 `Bin/App.exe` 를 못 본다. 작업 폴더(`Bin`)와 시험 바이너리 폴더를 순서대로 본다.
         */
        static sw::string findAppExecutablePath();

        /**
         * @brief App 을 `arguments` 로 띄웁니다. 실행 파일을 못 찾았거나 띄우지 못하면 false 입니다.
         * @details 작업 폴더는 바꾸지 않는다 — 시험의 작업 폴더가 `Bin` 이고 App 도 거기서 `Resource/` 를 찾아 올라간다.
         *          경로에 공백이 있을 수 있어 첫 토큰(실행 파일 경로)은 따옴표로 감싼다.
         */
        [[nodiscard]] static bool launchApp( sw::Process& outProcess, sw::string_view arguments );

        /**
         * @brief 이 빌드의 활성 게임 프리셋(`Config/Game/<게임>.json`)의 `_packRoot`(`game/<팩>`)입니다. 못 읽으면 빈 문자열입니다.
         * @details 게임 프리셋(`Ninja-Debug-<게임>`)마다 App 이 그리는 게임이 다르다 — 게임에 매인 시험(시나리오 · 골든 이미지)이 고른다.
         */
        static sw::string readActivePackRoot();

        /** @brief 시나리오 하나의 프로세스 시한(초) — 넘으면 죽이고 -1 입니다(시나리오의 프레임 시한이 먼저 끝내야 한다). */
        static constexpr uint32 kScenarioTimeoutSeconds = 180;
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
        /** @brief 이 기계에서 돌릴 수 없어 건너뛸 종료 코드(13 건너뜀 · 77 백엔드 없음 · 못 띄움)면 true 입니다. */
        static bool isSkippedExitCode( int32 exitCode );
    };
} // namespace test
