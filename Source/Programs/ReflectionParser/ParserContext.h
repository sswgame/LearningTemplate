/**
 * @file ParserContext.h
 * @brief libclang 번역 단위 하나의 수명과 파싱입니다(CXIndex · CXTranslationUnit). clang 인자는 `ParserConfig` 가 만듭니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include <clang-c/Index.h>

namespace sw
{
    struct ParserConfig;

    /** @brief 디스크 대신 넘길 파일 내용입니다. 이미 읽은 헤더, 또는 디스크에 없는 묶음 TU 원본에 씁니다. */
    struct ParserUnsavedFile
    {
        const string* _pPath;
        const string* _pContent;
    };

    /** @class ParserContext @brief libclang CXIndex / CXTranslationUnit 수명을 쥡니다. clang 인자는 `ParserConfig` 가 만듭니다. */
    class ParserContext
    {
    public:
        explicit ParserContext( const ParserConfig& config );
        ~ParserContext();

        ParserContext( const ParserContext& )            = delete;
        ParserContext& operator=( const ParserContext& ) = delete;

        /**
         * @brief 파일 하나를 파싱해 AST(CXTranslationUnit)를 만듭니다.
         * @param filePath 파싱할 주 파일 경로
         * @param listIncludePath 추가 include 경로 목록
         * @param listUnsaved 디스크 대신 넘길 내용(키워드 스캔과 이중 I/O 방지 · 디스크에 없는 주 파일)
         * @param bReportErrors 거짓이면 clang 오류를 오류 로그 대신 추적 로그로 남깁니다(실패하면 다시 해 볼 시도용)
         * @param listExtraArg 설정의 인자 뒤에 붙일 인자(PCH 굽기의 `-x c++-header`, 읽기의 `-include-pch <파일>`)
         * @param extraFlags 더 붙일 libclang 파싱 깃발(PCH 굽기의 `CXTranslationUnit_ForSerialization`)
         * @return 파싱 성공 여부(clang 오류가 하나라도 있으면 실패)
         */
        [[nodiscard]] bool parse( const string& filePath, const vector<string>& listIncludePath, const vector<ParserUnsavedFile>& listUnsaved,
                                  bool bReportErrors = true, const vector<string>& listExtraArg = {}, uint32 extraFlags = 0 );

        /** @brief 파싱한 번역 단위를 PCH 파일로 씁니다. `CXTranslationUnit_ForSerialization` 으로 파싱한 것이어야 합니다. */
        [[nodiscard]] bool save( const string& path ) const;

        /** @brief 생성된 clang TranslationUnit을 반환합니다. */
        CXTranslationUnit getTranslationUnit() const { return _translationUnit; }

    private:
        const ParserConfig* _pConfig;
        CXIndex             _index;
        CXTranslationUnit   _translationUnit;
    };
} // namespace sw
