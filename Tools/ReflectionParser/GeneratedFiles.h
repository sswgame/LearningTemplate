/**
 * @file GeneratedFiles.h
 * @brief 입력 헤더 하나가 만드는 산출물(.gen.cpp · .gen.h · 스탬프)의 경로 · 쓰기 · 증분 판정입니다.
 * @details 예전에는 "내용이 같으면 쓰지 않기" 가 세 곳(.gen.cpp · .gen.h · FlagOps 우산)에, 머리말의 원본 경로 읽기가 두 곳
 *          (증분 판정 · 이름 충돌 검사)에 각자 있었고, 두 읽기는 공백을 다르게 잘랐습니다.
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
         *          산출물의 시각은 "마지막 생성" 이 아니라 "내용이 마지막으로 바뀐 때" 이고, 예전처럼 그것으로 최신인지 물으면
         *          파서 · 템플릿 · 설정을 고친 뒤로 **모든 파서 실행이 내용이 안 바뀐 헤더를 매번 다시 파싱**합니다(실측: 아무것도
         *          안 바뀐 Engine 재실행이 Debug 에서 2.2 초). 스탬프 이름은 CMake 의 `ReflectBuiltins.gen.cpp.stamp` 와 같은 규칙입니다.
         */
        string _stampPath;
    };

    struct GeneratedFileUtil
    {
        /** @brief 출력 디렉터리 + 입력의 파일 이름으로 산출물 경로 셋을 만듭니다. */
        static GeneratedPaths makePaths( const string& outputDir, const string& inputFile, const ParserConfig& config );

        /**
         * @brief 내용이 다를 때만 씁니다. 디렉터리가 없으면 만듭니다.
         * @details 같은 내용을 다시 쓰면 시각만 바뀌어, 그 파일을 include 하는 번역 단위가 모두 다시 컴파일됩니다.
         */
        static bool writeIfChanged( const string& path, const string_view content );

        /**
         * @brief 스탬프를 씁니다. 내용은 원본 경로 한 줄과 `input <쓰기 시각>` 한 줄입니다.
         * @details `inputWriteTime` 은 **파싱하려고 읽기 전에** 잰 입력의 쓰기 시각입니다. 다음 실행은 이 값이 지금 시각과 **같을 때만** 최신으로
         *          봅니다(`IncrementalCheck::isUpToDate`). 예전에는 스탬프 파일의 시각(= 다 쓴 때)이 입력보다 새로운지만 봐서, 파싱하는 동안
         *          저장한 편집이 "스탬프보다 오래됐다" 며 다음 실행에서도 무시됐다 — 그 헤더를 다시 저장할 때까지.
         */
        static bool writeStamp( const string& stampPath, const string& inputFile, uint64 inputWriteTime );

        /** @brief 파일의 마지막 쓰기 시각(플랫폼 단위 그대로)입니다. 없으면 0 입니다. 스탬프에 적는 값과 같은 단위입니다. */
        static uint64 getWriteTime( string_view path );

        /** @brief 산출물 머리말(`// Source: …`)에 적힌 원본 경로입니다. 없으면 빈 뷰입니다. */
        static string_view findRecordedSourcePath( const string_view generatedText, const ParserConfig& config );

        /** @brief CMake 가 구성 때 심어 둔 빈 자리 표시자(또는 등록이 하나도 없는 옛 파서 산출물)인지 봅니다. */
        static bool isPlaceholder( const string_view generatedText, const ParserConfig& config );
    };

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
    };
} // namespace sw
