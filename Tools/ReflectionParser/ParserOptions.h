/**
 * @file ParserOptions.h
 * @brief ReflectionParser 의 명령줄 인자입니다. 플래그 하나는 ParserOptions.cpp 의 표 한 줄입니다.
 * @details 예전에는 `main` 곁의 if 사슬이 플래그마다 같은 세 줄을 되풀이했고, 사용법 문구는 따로 손으로 적어
 *          `--include` · `--source-root` 가 빠져 있었습니다. 이제 표 한 줄이 읽기와 사용법을 함께 만듭니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{
    struct ParserOptions
    {
        vector<string> _listInputFile;       ///< --input (여러 번)
        vector<string> _listIncludePath;     ///< --include (여러 번)
        string         _outputDir;           ///< --output
        string         _builtinsPath;        ///< --builtins
        string         _annotationMetaPath;  ///< --annotation-meta
        string         _emitTemplatesDir;    ///< --emit-templates
        string         _sourceRoot;          ///< --source-root: 모듈 판별을 이 경로 기준 상대 경로로 한다
        string         _emitBuiltinsGenPath; ///< --emit-builtins-gen: ReflectBuiltins.gen.cpp 만 쓰는 모드

        /**
         * @brief argv 를 읽고 모드별 필수 인자를 확인합니다.
         * @return 모르는 인자 · 값 없는 플래그 · 빠진 필수 인자가 있으면 이유를 알리고 false
         */
        bool parse( int32 argc, utf8* argv[] );

        /** @brief 표에서 만든 사용법을 로그로 남깁니다. */
        static void logUsage();

        /** @brief ReflectBuiltins.gen.cpp 만 쓰는 모드인지 봅니다. */
        bool isBuiltinsGenMode() const noexcept { return _emitBuiltinsGenPath.empty() == false; }
    };
} // namespace sw
