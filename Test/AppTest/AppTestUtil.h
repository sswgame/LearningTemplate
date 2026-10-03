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
    };
} // namespace test
