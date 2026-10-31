/**
 * @file ParserOptions.h
 * @brief ReflectionParser 의 명령줄 인자입니다. 플래그 하나는 ParserOptions.cpp 의 표 한 줄입니다.
 * @details 표 한 줄이 읽기와 사용법을 함께 만듭니다 — 사용법 문구를 따로 손으로 적으면 플래그가 빠집니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{
    struct ParserOptions
    {
        vector<string> _listInputFile;         ///< --input (여러 번)
        vector<string> _listIncludePath;       ///< --include (여러 번)
        string         _outputDir;             ///< --output
        string         _builtinsPath;          ///< --builtins
        string         _annotationMetaPath;    ///< --annotation-meta
        string         _emitTemplatesDir;      ///< --emit-templates
        string         _moduleName;            ///< --module: 이 대상의 타입이 오르는 런타임 모듈 이름(CMake 가 정한다 - 경로로 추측하지 않는다)
        string         _emitBuiltinsGenPath;   ///< --emit-builtins-gen: ReflectBuiltins.gen.cpp 만 쓰는 모드
        string         _depfilePath;           ///< --depfile: 입력들이 include 한 헤더를 Makefile 꼴 의존 파일로 쓴다(빌드 체계의 DEPFILE)
        bool           _bDump{ false };        ///< --dump: 최신이어도 다시 파싱하고, 헤더마다 뽑은 것을 사람이 읽는 꼴로 찍는다
        bool           _bHelp{ false };        ///< --help: 사용법만 찍고 끝낸다
        bool           _bModuleLocal{ false }; ///< --module-local: 대상(실행 파일 · 게임 모듈)의 타입이 모듈 경계를 넘지 않는다. 버스 본문 타입에 내보내기 매크로를 요구하지 않는다

        /**
         * @brief argv 를 읽고 모드별 필수 인자를 확인합니다. `@파일` 은 그 파일의 줄마다 인자 하나로 펼칩니다(응답 파일).
         * @return 모르는 인자 · 값 없는 플래그 · 빠진 필수 인자가 있으면 이유를 알리고 false
         */
        [[nodiscard]] bool parse( int32 argc, utf8* argv[] );

        /**
         * @brief 표에서 만든 사용법을 표준 출력에 씁니다.
         * @details 로그가 아니라 `fwrite` 다 — 배포본은 Info 로그를 컴파일하지 않아(`SW_LOG_COMPILED_VERBOSITY`), 로그로 남기면
         *          Shipping 파서의 `--help` 가 아무것도 찍지 않는다.
         */
        static void printUsage();

        /** @brief ReflectBuiltins.gen.cpp 만 쓰는 모드인지 봅니다. */
        bool isBuiltinsGenMode() const noexcept { return _emitBuiltinsGenPath.empty() == false; }
    };
} // namespace sw
