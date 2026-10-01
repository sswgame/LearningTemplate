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
            uint64         _inputWriteTime; /**< 내용을 **읽기 전에** 잰 입력의 쓰기 시각 — 스탬프에 적는다(`GeneratedFileUtil::writeStamp`) */
        };

        /** @brief 파싱할 입력들을 처리합니다 — 둘 이상이면 한 번역 단위로 묶고, 그것이 안 되면 하나씩. 실패한 수를 돌려줍니다. */
        int32 parsePending( const vector<PendingInput>& listPending ) const;
        /**
         * @brief 헤더 여럿을 **한 번역 단위**로 파싱해 헤더마다 코드젠합니다.
         * @param outErrorCount 수집 · 코드젠에 실패한 헤더 수
         * @return clang 이 묶음을 파싱하지 못했으면 false(헤더마다 다시 해 본다)
         */
        bool parseBatch( const vector<PendingInput>& listPending, int32& outErrorCount ) const;
        /** @brief 헤더 하나를 파싱 · 수집 · 코드젠합니다. */
        bool parseAndGenerate( const PendingInput& pending ) const;
        /** @brief 입력마다 따로 파싱합니다(워커 풀). 실패한 수를 돌려줍니다. */
        int32 parseEachInParallel( const vector<PendingInput>& listPending ) const;
        /**
         * @brief 모은 것을 산출물로 씁니다 — 이름 충돌 검사 → 내용이 다를 때만 쓰기 → 스탬프.
         * @param inputWriteTime 읽기 전에 잰 입력 시각.
         * @param listDependency 이 입력을 파싱한 번역 단위가 include 한 프로젝트 헤더(스탬프에 적는다).
         */
        bool writeOutputs( const string& inputFile, const GeneratedPaths& paths, const ParsedHeader& parsed, uint64 inputWriteTime,
                           const vector<StampDependency>& listDependency ) const;
        /**
         * @brief `--depfile` 이 있으면 모든 입력의 산출물을 목표로, 스탬프들의 의존을 합친 것을 Makefile 꼴로 씁니다. **실행마다** 씁니다.
         * @details ninja 는 이 파일로 "반사되지 않은 헤더가 바뀌면 이 단계를 다시 돌린다" 를 안다. 빠지면 단계가 늘 더럽다고 보거나(파일 없음)
         *          옛 의존으로 판단한다 — 그래서 모두 최신인 실행도 스탬프에서 모아 쓴다.
         */
        bool writeDepfile() const;
        /** @brief ENUM(Flags) 트레이트를 담은 .gen.h 들을 모으는 우산(FlagOps.gen.h)을 씁니다. */
        bool writeFlagOpsUmbrella() const;

    private:
        const ParserOptions* _pOptions;
        const ParserSession* _pSession;
        IncrementalCheck     _incrementalCheck;
        uint64               _runStartTime;    ///< 이번 실행의 시작(출력 폴더 표식 파일의 시각) — 그 뒤에 바뀐 의존은 0 으로 적는다
        vector<string>       _listIncludePath; ///< 출력 디렉터리가 맨 앞이다(생성 헤더를 include 하는 원본이 있다)
    };
} // namespace sw
