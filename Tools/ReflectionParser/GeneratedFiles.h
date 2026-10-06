/**
 * @file GeneratedFiles.h
 * @brief 입력 헤더 하나가 만드는 산출물(.gen.cpp · .gen.h · 스탬프)의 경로 · 쓰기 · 증분 판정입니다.
 * @details "내용이 같으면 쓰지 않기"(.gen.cpp · .gen.h · FlagOps 우산)와 머리말의 원본 경로 읽기(증분 판정 · 이름 충돌 검사)가
 *          여기 한 벌입니다 — 벌마다 두면 공백을 다르게 자르는 식으로 어긋납니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{
    struct ParserConfig;
    struct ParserOptions;

    /** @brief 입력 헤더 하나의 산출물 경로입니다. */
    struct GeneratedPaths
    {
        string _cppPath;
        string _headerPath;
        /**
         * @brief 마지막으로 **성공한 생성**의 시각을 담는 빈 파일(`<.gen.cpp>.stamp`)입니다.
         * @details 산출물은 내용이 같으면 다시 쓰지 않습니다 — 쓰면 그 파일을 include 하는 TU 가 전부 다시 컴파일됩니다. 그래서
         *          산출물의 시각은 "마지막 생성" 이 아니라 "내용이 마지막으로 바뀐 때" 이고, 그것으로 최신인지 물으면
         *          파서 · 템플릿 · 설정을 고친 뒤로 **모든 파서 실행이 내용이 안 바뀐 헤더를 매번 다시 파싱**합니다(실측: 아무것도
         *          안 바뀐 Engine 재실행이 Debug 에서 2.2 초). 스탬프 이름은 CMake 의 `ReflectBuiltins.gen.cpp.stamp` 와 같은 규칙입니다.
         */
        string _stampPath;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 산출물이 기대는 파일 하나 — 입력이 include 한 헤더와 그것을 본 쓰기 시각입니다(스탬프의 `dep <시각> <경로>` 줄).
     * @details 산출물은 입력 헤더만이 아니라 그 헤더가 include 한 헤더에도 기댄다 — `PROPERTY() ScoreList _scores;` 의 컨테이너 종류는 다른 헤더의
     *          `using ScoreList = …` 가 정한다. 이것을 적지 않으면 그 헤더가 바뀌어도 ninja 도 파서도 다시 돌지 않아 생성 코드가 옛 컨테이너
     *          래퍼로 직렬화한다.
     */
    struct StampDependency
    {
        string _path;      /**< 절대 경로(슬래시) */
        uint64 _writeTime; /**< 본 쓰기 시각. 이번 실행이 시작된 뒤에 바뀐 것은 0 — 다음 실행이 다시 본다 */
    };
} // namespace sw

namespace sw
{
    struct GeneratedFileUtil
    {
        /** @brief 출력 디렉터리 + 입력의 파일 이름으로 산출물 경로 셋을 만듭니다. */
        static GeneratedPaths makePaths( const string& outputDir, const string& inputFile, const ParserConfig& config );

        /**
         * @brief 내용이 다를 때만 씁니다. 디렉터리가 없으면 만듭니다.
         * @details 같은 내용을 다시 쓰면 시각만 바뀌어, 그 파일을 include 하는 번역 단위가 모두 다시 컴파일됩니다.
         */
        [[nodiscard]] static bool writeIfChanged( const string& path, const string_view content );

        /**
         * @brief 스탬프를 씁니다. 내용은 원본 경로 한 줄과 `input <쓰기 시각>` 한 줄입니다.
         * @details `inputWriteTime` 은 **파싱하려고 읽기 전에** 잰 입력의 쓰기 시각입니다. 다음 실행은 이 값이 지금 시각과 **같을 때만** 최신으로
         *          봅니다(`IncrementalCheck::isUpToDate`). 스탬프 파일의 시각(= 다 쓴 때)이 입력보다 새로운지만 보면 파싱하는 동안 저장한
         *          편집이 "스탬프보다 오래됐다" 며 다음 실행에서도 무시된다 — 그 헤더를 다시 저장할 때까지.
         */
        [[nodiscard]] static bool writeStamp( const string& stampPath, const string& inputFile, uint64 inputWriteTime, const vector<StampDependency>& listDependency );

        /** @brief 스탬프에 적힌 의존(`dep` 줄)의 경로를 `outListPath` 에 더합니다. 스탬프가 없거나 시각이 없는 꼴이면 false 입니다. depfile 을 모을 때 씁니다. */
        [[nodiscard]] static bool readStampDependencies( const string& stampPath, vector<string>& outListPath );

        /**
         * @brief 이번 실행의 시작을 출력 폴더의 표식 파일(`ReflectionParser.run`)에 적고 그 쓰기 시각을 돌려줍니다.
         * @details 의존의 시각은 파싱한 **뒤에야** 잴 수 있다(무엇을 include 했는지 그때 안다). 그 사이에 저장한 편집을 놓치지 않으려고, 이 시각
         *          이후에 바뀐 의존은 0 으로 적는다 — 다음 실행이 다시 파싱한다. 파일 시각과 같은 시계 · 단위라 비교가 맞다.
         */
        static uint64 markRunStart( const string& outputDir );

        /** @brief 파일의 마지막 쓰기 시각(플랫폼 단위 그대로)입니다. 없으면 0 입니다. 스탬프에 적는 값과 같은 단위입니다. */
        static uint64 getWriteTime( string_view path );

        /** @brief 산출물 머리말(`// Source: …`)에 적힌 원본 경로입니다. 없으면 빈 뷰입니다. */
        static string_view findRecordedSourcePath( const string_view generatedText, const ParserConfig& config );

        /** @brief CMake 가 구성 때 심어 둔 빈 자리 표시자(또는 등록이 하나도 없는 파서 산출물)인지 봅니다. */
        static bool isPlaceholder( const string_view generatedText, const ParserConfig& config );
    };
} // namespace sw

namespace sw
{
    /**
     * @class IncrementalCheck
     * @brief "이 입력의 산출물은 최신인가" 를 답합니다. 도구 쪽 시각은 시작할 때 한 번만 잽니다.
     * @details **도구도 입력입니다.** 산출물의 모양은 입력 헤더만이 아니라 파서 실행 파일 · builtins · 철자 표 · 템플릿 ·
     *          설정 파일이 함께 정합니다. 그중 가장 새것보다 스탬프가 오래됐으면 다시 만듭니다.
     */
    class IncrementalCheck
    {
    public:
        IncrementalCheck( const ParserOptions& options, const ParserConfig& config );

        /** @brief 스탬프가 입력 · 도구보다 새롭고, 산출물이 자리 표시자가 아니며, 머리말의 원본 경로가 지금 입력과 같으면 true */
        bool isUpToDate( const string& inputFile, const GeneratedPaths& paths ) const;

    private:
        const ParserConfig* _pConfig;
        uint64              _newestToolWriteTime;
        /** @brief 의존 파일의 쓰기 시각 캐시 — 같은 헤더를 입력마다 다시 재지 않는다(한 실행 안에서만, 한 스레드에서만 쓴다). */
        mutable unordered_map<string, uint64> _mapDependencyWriteTime;
    };
} // namespace sw
