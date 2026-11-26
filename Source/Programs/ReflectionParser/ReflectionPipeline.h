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
        Setup, ///< 명령줄 · 설정 · 표 · 템플릿 읽기와 도구 시각 측정(파이프라인을 세우기까지, `main` 이 잰다)
        Check, ///< 입력마다 증분 판정 · 내용 읽기 · 키워드 거르기
        Pch,   ///< 모듈 공통 앞부분(`pch.h`)의 PCH 굽기. 번역 단위가 둘 이상이고 .cpp 입력이 있을 때만 한다
        Parse, ///< clang 이 번역 단위를 만든다(공통 include 가 거의 전부)
        Visit, ///< AST 를 돌며 리플렉션 사실을 모은다
        Emit,  ///< 코드젠 · 산출물 쓰기 · 스탬프
        Lists, ///< 대상 전체의 목록(FlagOps 우산 · 형식 · 경로 칸 · 스키마 결합형 · depfile)
        Count,
    };

    class ReflectionPipeline
    {
    public:
        ReflectionPipeline( const ParserOptions& options, const ParserSession& session );

        /**
         * @brief 입력을 모두 처리하고 FlagOps 우산을 씁니다.
         * @param setupSeconds 프로세스 시작부터 이 파이프라인을 세우기까지 걸린 시간(단계 시간 로그의 `setup`)
         * @return 실패한 수(우산 쓰기 실패 포함). 0 이면 성공입니다.
         */
        int32 run( float32 setupSeconds );

    private:
        /**
         * @brief 입력 하나(겹치지 않게 한 번씩)의 산출물 경로와 스탬프입니다. 스탬프는 실행마다 입력당 한 번만 읽는다.
         * @details 증분 판정 · 이름 충돌 검사 · 목록 다섯(FlagOps 우산 · 형식 · 경로 칸 · 스키마 결합형 · depfile)이 같은 스탬프를 본다. 예전에는 저마다
         *          디스크에서 다시 읽어 입력 하나에 여섯 번 읽었다. 이번 실행이 새로 쓴 입력은 쓴 스탬프로 바꾼다(디스크와 같다).
         */
        struct InputRecord
        {
            const AbsolutePath*    _pInputFile;
            GeneratedPaths         _paths;
            GeneratedStamp         _stamp;         ///< `_bHasStamp` 일 때만 뜻이 있다
            uint8                  _bHasStamp : 1; ///< 디스크에 읽을 수 있는 스탬프가 있다(없으면 자리 표시자이거나 이번 실행에 실패했다)
            [[maybe_unused]] uint8 _reserved  : 7;
        };

        /** @brief 파싱해야 하는 입력 하나입니다. 키워드를 거를 때 읽은 내용을 파서에 그대로 넘깁니다(이중 I/O 방지). */
        struct PendingInput
        {
            InputRecord* _pRecord; ///< 이 입력의 기록. 산출물을 쓰면 스탬프를 바꾼다(입력마다 한 워커만 쓴다)
            string       _content;
            uint64       _inputWriteTime; /**< 내용을 읽기 전에 측정한 입력의 쓰기 시각 — 스탬프에 적는다(`GeneratedStamp::write`) */
        };

        /**
         * @brief 이번 실행의 번역 단위들이 함께 읽는 PCH 입니다. 원본은 출력 폴더의 `ReflectionParser.pch.h`(`pch.h` 하나를 include)다.
         * @details 언리얼 UBT 가 모듈의 공유 PCH 를 쓰는 자리다. 실행마다 새로 굽고 실행 끝에 지운다(다음 실행의 인자 · 헤더와 맞는다는 보장이 없다).
         */
        struct ModulePch
        {
            AbsolutePath            _pchPath;
            vector<StampDependency> _listDependency; ///< PCH 를 구울 때 include 한 프로젝트 헤더. PCH 를 읽은 번역 단위의 의존에 더한다
        };

        /**
         * @brief 파싱할 헤더 입력들을 처리합니다. 둘 이상이거나 PCH 를 쓰면 한 번역 단위로 묶고, 묶음이 깨지면 하나씩(PCH 없이). 실패한 수를 돌려줍니다.
         * @param pPch 함께 읽을 PCH(없으면 nullptr)
         */
        int32 parsePending( const vector<PendingInput>& listPending, const ModulePch* pPch ) const;
        /**
         * @brief 헤더 여럿을 한 번역 단위로 파싱해 헤더마다 코드젠합니다.
         * @param pPch 함께 읽을 PCH(없으면 nullptr). 있으면 입력 헤더를 내용으로 넘기지 않고 디스크에서 읽는다(PCH 안의 파일을 덮으면 clang 이 거절한다)
         * @param outErrorCount 수집 · 코드젠에 실패한 헤더 수
         * @return clang 이 묶음을 파싱하지 못했으면 false(헤더마다 다시 해 본다)
         */
        [[nodiscard]] bool parseBatch( const vector<PendingInput>& listPending, const ModulePch* pPch, int32& outErrorCount ) const;
        /** @brief 입력 하나를 주 파일로 파싱 · 수집 · 코드젠합니다. @p pPch 는 .cpp 입력만 받는다(헤더를 주 파일로 읽으면 PCH 안의 같은 헤더와 겹친다). */
        [[nodiscard]] bool parseAndGenerate( const PendingInput& pending, const ModulePch* pPch ) const;
        /** @brief 입력마다 따로 파싱합니다(워커 풀). 실패한 수를 돌려줍니다. */
        int32 parseEachInParallel( const vector<PendingInput>& listPending, const ModulePch* pPch ) const;
        /** @brief `pch.h` 를 PCH 로 굽습니다. 굽지 못하면 오류를 남기고 false 입니다(PCH 없이 다시 하지 않는다 — .cpp 입력도 같은 `pch.h` 로 시작한다). */
        [[nodiscard]] bool bakeModulePch( ModulePch& outPch ) const;
        /**
         * @brief 모은 것을 산출물로 씁니다 — 이름 충돌 검사 → 내용이 다를 때만 쓰기 → 스탬프.
         * @param inputWriteTime 읽기 전에 측정한 입력 시각.
         * @param listDependency 이 입력을 파싱한 번역 단위가 include 한 프로젝트 헤더(스탬프에 적는다).
         */
        [[nodiscard]] bool writeOutputs( InputRecord& record, const ParsedHeader& parsed, uint64 inputWriteTime, const vector<StampDependency>& listDependency ) const;
        /**
         * @brief `--depfile` 이 있으면 모든 입력의 산출물을 목표로, 스탬프들의 의존을 합친 것을 Makefile 꼴로 씁니다. 실행마다 씁니다.
         * @details ninja 는 이 파일로 "반사되지 않은 헤더가 바뀌면 이 단계를 다시 돌린다" 를 안다. 빠지면 단계가 늘 더럽다고 보거나(파일 없음)
         *          옛 의존으로 판단한다 — 그래서 모두 최신인 실행도 스탬프에서 모아 쓴다.
         */
        [[nodiscard]] bool writeDepfile( const vector<InputRecord>& listInput ) const;
        /** @brief ENUM(Flags) 트레이트를 담은 .gen.h 들을 모으는 우산(FlagOps.gen.h)을 씁니다. */
        [[nodiscard]] bool writeFlagOpsUmbrella( const vector<InputRecord>& listInput ) const;
        /**
         * @brief 대상의 타입 · 열거형을 `<FQN>\t<모듈>` 줄로 `ReflectedTypes.list` 에 씁니다(정렬, 내용이 같으면 쓰지 않음).
         * @details 타입이 어느 모듈인지를 아는 곳은 CMake(`--module`) 하나다 - 콘텐츠 검사(`Scripts/generate/content_modules.py`)가 이 목록을 읽는다.
         */
        [[nodiscard]] bool writeReflectedTypesList( const vector<InputRecord>& listInput ) const;
        /**
         * @brief 대상의 경로 칸(`AssetRef<T>` · `DataPath`)을 `<이름>\t<대상>` 줄로 `AssetReferenceFields.list` 에 씁니다(정렬, 내용이 같으면 쓰지 않음).
         * @details 끊어진 경로 검사 · 옮기기 도구가 어느 칸이 경로인지 아는 근거다. 손으로 맞추던 이름 표를 대신한다(`Scripts/generate/asset_reference_field_list.py` 가 모은다).
         */
        [[nodiscard]] bool writeAssetFieldList( const vector<InputRecord>& listInput ) const;
        /** @brief 이번 실행이 만든 `.gen.cpp` 바이트를 방출 함수 구역마다 로그 `ReflectionParser.EmitBytes` 한 줄로 남깁니다(만든 것이 없으면 남기지 않는다). */
        void logEmitBytes() const;
        /**
         * @brief 단계마다 걸린 시간을 로그 `ReflectionParser.PhaseTime` 한 줄로 남깁니다. 파싱한 입력이 없는 실행도 남긴다(모두 최신인 실행의 비용도 잰다).
         * @details 재는 장치만이다 — 통합이 Release 빌드 로그에서 최적화마다 단계별로 견준다. parse · visit · emit 은 워커 시간을 더한 값이라
         *          병렬로 돌면 벽시계 합(`total`)보다 클 수 있다.
         */
        void logPhaseTimes( size_t parsedCount, float64 totalSeconds ) const;
        /** @brief @p phase 에 걸린 시간을 더합니다(워커가 함께 부른다). */
        void addPhaseTime( ParsePhase phase, float32 seconds ) const;
        /** @brief 대상의 `.gen.cpp` 들에서 스키마 결합형 정규 문자열 줄을 모아 `LayoutSchemas.gen.txt` 를 씁니다. */
        [[nodiscard]] bool writeLayoutSchemaList( const vector<InputRecord>& listInput ) const;

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
