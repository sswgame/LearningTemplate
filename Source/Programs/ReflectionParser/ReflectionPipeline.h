/**
 * @file ReflectionPipeline.h
 * @brief 입력 헤더 목록 → 산출물까지, 파서 실행 한 번의 흐름입니다.
 * @details 증분 판정 → 키워드 거르기 → 파싱 · 수집 → 코드젠 · 쓰기 → FlagOps 우산. CLI(`ParserOptions`) · 증분 판정(`GeneratedFiles`)은
 *          따로 있어 어느 하나만 다시 쓸 수 있습니다.
 */
#pragma once
#include "Core/Path/AbsolutePath.h"

#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/CodeEmit.h"
#include "Programs/ReflectionParser/GeneratedFiles.h"
#include "Programs/ReflectionParser/ParsedReflection.h"

namespace sw
{
    struct ParserOptions;
    struct ParserSession;

    /** @brief 파서 실행의 단계입니다(단계별 시간 로그 `ReflectionParser.PhaseTime`). */
    enum class ParsePhase : uint8
    {
        Parse, ///< clang 이 번역 단위를 만든다(공통 include 가 거의 전부)
        Visit, ///< AST 를 돌며 리플렉션 사실을 모은다
        Emit,  ///< 코드젠 · 산출물 쓰기 · 스탬프
        Count,
    };

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
            const AbsolutePath* _pInputFile;
            string              _content;
            GeneratedPaths      _paths;
            uint64              _inputWriteTime; /**< 내용을 **읽기 전에** 측정한 입력의 쓰기 시각 — 스탬프에 적는다(`GeneratedFileUtil::writeStamp`) */
        };

        /** @brief 파싱할 입력들을 처리합니다 — 둘 이상이면 한 번역 단위로 묶고, 그것이 안 되면 하나씩. 실패한 수를 돌려줍니다. */
        int32 parsePending( const vector<PendingInput>& listPending ) const;
        /**
         * @brief 헤더 여럿을 **한 번역 단위**로 파싱해 헤더마다 코드젠합니다.
         * @param outErrorCount 수집 · 코드젠에 실패한 헤더 수
         * @return clang 이 묶음을 파싱하지 못했으면 false(헤더마다 다시 해 본다)
         */
        [[nodiscard]] bool parseBatch( const vector<PendingInput>& listPending, int32& outErrorCount ) const;
        /** @brief 헤더 하나를 파싱 · 수집 · 코드젠합니다. */
        [[nodiscard]] bool parseAndGenerate( const PendingInput& pending ) const;
        /** @brief 입력마다 따로 파싱합니다(워커 풀). 실패한 수를 돌려줍니다. */
        int32 parseEachInParallel( const vector<PendingInput>& listPending ) const;
        /**
         * @brief 모은 것을 산출물로 씁니다 — 이름 충돌 검사 → 내용이 다를 때만 쓰기 → 스탬프.
         * @param inputWriteTime 읽기 전에 측정한 입력 시각.
         * @param listDependency 이 입력을 파싱한 번역 단위가 include 한 프로젝트 헤더(스탬프에 적는다).
         */
        [[nodiscard]] bool writeOutputs( const AbsolutePath& inputFile, const GeneratedPaths& paths, const ParsedHeader& parsed, uint64 inputWriteTime,
                                         const vector<StampDependency>& listDependency ) const;
        /**
         * @brief `--depfile` 이 있으면 모든 입력의 산출물을 목표로, 스탬프들의 의존을 합친 것을 Makefile 꼴로 씁니다. **실행마다** 씁니다.
         * @details ninja 는 이 파일로 "반사되지 않은 헤더가 바뀌면 이 단계를 다시 돌린다" 를 안다. 빠지면 단계가 늘 더럽다고 보거나(파일 없음)
         *          옛 의존으로 판단한다 — 그래서 모두 최신인 실행도 스탬프에서 모아 쓴다.
         */
        [[nodiscard]] bool writeDepfile() const;
        /** @brief ENUM(Flags) 트레이트를 담은 .gen.h 들을 모으는 우산(FlagOps.gen.h)을 씁니다. */
        [[nodiscard]] bool writeFlagOpsUmbrella() const;
        /**
         * @brief 대상의 타입 · 열거형을 `<FQN>\t<모듈>` 줄로 `ReflectedTypes.list` 에 씁니다(정렬, 내용이 같으면 쓰지 않음).
         * @details 타입이 어느 모듈인지를 아는 곳은 CMake(`--module`) 하나다 - 콘텐츠 검사(`Scripts/generate/content_modules.py`)가 이 목록을 읽는다.
         */
        [[nodiscard]] bool writeReflectedTypesList() const;
        /**
         * @brief 대상의 경로 칸(`AssetRef<T>` · `DataPath` · `PROPERTY( AssetPath )`)과 경로가 아닌 글 칸을 `<이름>\t<대상>\t<ref|data|path|text>` 줄로 `AssetReferenceFields.list` 에 씁니다(정렬, 내용이 같으면 쓰지 않음).
         * @details 끊어진 경로 검사 · 옮기기 도구가 어느 칸이 경로인지 아는 근거다. 손으로 맞추던 이름 표를 대신한다(`Scripts/generate/asset_reference_field_list.py` 가 모은다).
         *          셋째 열은 저장 꼴이다 — `ref`(`AssetRef<T>`)는 GUID 참조(`guid:<32자>`)로 쓰고, `data`(`DataPath`)도 같으며, `path`(`AssetPath` 글)는 경로 글이다.
         *          `text` 는 경로 칸이 아닌 글 칸(`string` · `hashed_string`)이다 — 같은 이름이 다른 주인에서 `ref` 면 이름만으로 칸을 가를 수 없어, 이전 도구가 그 이름의 경로 값을 오류로 멈춘다.
         */
        [[nodiscard]] bool writeAssetFieldList() const;
        /** @brief 이번 실행이 만든 `.gen.cpp` 바이트를 방출 함수 구역마다 로그 `ReflectionParser.EmitBytes` 한 줄로 남깁니다(만든 것이 없으면 남기지 않는다). */
        void logEmitBytes() const;
        /**
         * @brief 단계마다 걸린 시간을 로그 `ReflectionParser.PhaseTime` 한 줄로 남깁니다(파싱한 입력이 없으면 남기지 않는다).
         * @details 재는 장치만이다 — 공통 include 의 PCH(파서 시간이 문턱을 넘을 때)를 판정할 근거다. 헤더마다 다시 파싱하는 길은 워커 시간을 더한 값이다.
         */
        void logPhaseTimes( size_t parsedCount ) const;
        /** @brief @p phase 에 걸린 시간을 더합니다(워커가 함께 부른다). */
        void addPhaseTime( ParsePhase phase, float32 seconds ) const;
        /** @brief 대상의 `.gen.cpp` 들에서 고정 배치형 정규 문자열 줄을 모아 `FixedSchemas.gen.txt` 를 씁니다. */
        [[nodiscard]] bool writeFixedSchemaList() const;

    private:
        const ParserOptions* _pOptions;
        const ParserSession* _pSession;
        IncrementalCheck     _incrementalCheck;
        uint64               _runStartTime;    ///< 이번 실행의 시작(출력 폴더 표식 파일의 시각) — 그 뒤에 바뀐 의존은 0 으로 적는다
        vector<string>       _listIncludePath; ///< 출력 디렉터리가 맨 앞이다(생성 헤더를 include 하는 원본이 있다)
        /// @brief 방출 함수 구역마다 낸 바이트의 합입니다. 헤더마다 워커가 더한다(`parseEachInParallel`).
        mutable std::array<atomic<uint64>, static_cast<size_t>( CodeEmitSection::Count )> _arrEmitBytes;
        /// @brief 단계마다 걸린 시간의 합(마이크로초)입니다.
        mutable std::array<atomic<uint64>, static_cast<size_t>( ParsePhase::Count )> _arrPhaseMicroseconds;
    };
} // namespace sw
