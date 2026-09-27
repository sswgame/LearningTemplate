/**
 * @file ReflectionPipeline.h
 * @brief 입력 헤더 목록 → 산출물까지, 파서 실행 한 번의 흐름입니다.
 * @details 증분 판정 → 키워드 거르기 → 파싱 · 수집 → 코드젠 · 쓰기 → FlagOps 우산. 예전에는 이 흐름이 `main` 곁의 도우미
 *          묶음에 섞여 있었고, CLI · 증분 판정 · 쓰기가 한 파일에 붙어 있어 어느 하나만 다시 쓸 수 없었습니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "ReflectionParser/GeneratedFiles.h"
#include "ReflectionParser/ParsedReflection.h"

namespace sw
{
    struct ParserOptions;
    struct ParserSession;

    class ReflectionPipeline
    {
    public:
        ReflectionPipeline( const ParserOptions& options, const ParserSession& session );

        /**
         * @brief 입력을 모두 처리하고 FlagOps 우산을 씁니다.
         * @return 실패한 수(우산 쓰기 실패 포함). 0 이면 성공입니다.
         */
        int32 run();

    private:
        /** @brief 파싱해야 하는 입력 하나입니다. 키워드를 거를 때 읽은 내용을 파서에 그대로 넘깁니다(이중 I/O 방지). */
        struct PendingInput
        {
            const string*  _pInputFile;
            string         _content;
            GeneratedPaths _paths;
        };

        /** @brief 헤더 하나를 파싱 · 수집 · 코드젠합니다. */
        bool parseAndGenerate( const PendingInput& pending ) const;
        /** @brief 입력마다 따로 파싱합니다(워커 풀). 실패한 수를 돌려줍니다. */
        int32 parseEachInParallel( const vector<PendingInput>& listPending ) const;
        /** @brief 모은 것을 산출물로 씁니다 — 이름 충돌 검사 → 내용이 다를 때만 쓰기 → 스탬프. */
        bool writeOutputs( const string& inputFile, const GeneratedPaths& paths, const ParsedHeader& parsed ) const;
        /** @brief ENUM(Flags) 트레이트를 담은 .gen.h 들을 모으는 우산(FlagOps.gen.h)을 씁니다. */
        bool writeFlagOpsUmbrella() const;

    private:
        const ParserOptions* _pOptions;
        const ParserSession* _pSession;
        IncrementalCheck     _incrementalCheck;
        vector<string>       _listIncludePath; ///< 출력 디렉터리가 맨 앞이다(생성 헤더를 include 하는 원본이 있다)
    };
} // namespace sw
