/**
 * @file AnnotationApply.h
 * @brief REFLECT/PROPERTY/FUNCTION/ENUM 어노테이션 문자열을 Parsed* 필드에 적용합니다.
 * @details 토큰의 철자는 AnnotationMeta.txt 가 정규 필드명으로 바꾸고, 값을 넣는 일은 필드 표(`AnnotationFields`)가
 *          합니다. 스코프마다 따로 있던 적용 함수 넷은 표를 도는 루프 하나가 되었습니다 — 새 필드는
 *          PredefinedAnnotationField.xxx 에 한 줄을 더하면 됩니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{
    struct ParsedEnumInfo;
    struct ParsedFunctionInfo;
    struct ParsedPropertyInfo;
    struct ParsedTypeInfo;

    class AnnotationMeta;

    // ------------------------------------------------------------------------------
    // 1) AnnotationApply — 어노테이션 문자열 유틸 · Parsed* 채우기
    // ------------------------------------------------------------------------------
    /** @brief REFLECT/PROPERTY/FUNCTION/ENUM 어노테이션 문자열을 Parsed* 에 적용합니다. */
    struct AnnotationApply
    {
        /** @brief `PREFIX;args` 에서 접두사 이후 인자 텍스트만 반환합니다. */
        static string_view annotationArgumentText( string_view spelling, string_view prefix );

        /** @brief 따옴표를 존중하며 쉼표로 인자 토큰을 나눕니다. */
        static vector<string> splitAnnotationArgs( string_view args );

        /**
         * @brief 애노테이션 문자열의 토큰을 대상 DTO 에 넣습니다. 스코프는 DTO 타입이 정합니다.
         * @param outListUnknownToken AnnotationMeta.txt 에 없는 토큰을 받습니다(조용히 버리지 않습니다) —
         *        호출하는 쪽이 어느 타입 · 멤버인지 붙여 오류로 알립니다.
         */
        template <typename TParsed>
        static void apply( string_view annotationSpelling, TParsed& target, const AnnotationMeta& meta,
                           vector<string>& outListUnknownToken );
    };

    // 정의는 AnnotationApply.cpp 에 있고 네 DTO 로만 인스턴스화합니다.
    extern template void AnnotationApply::apply<ParsedTypeInfo>( string_view, ParsedTypeInfo&, const AnnotationMeta&, vector<string>& );
    extern template void AnnotationApply::apply<ParsedEnumInfo>( string_view, ParsedEnumInfo&, const AnnotationMeta&, vector<string>& );
    extern template void AnnotationApply::apply<ParsedPropertyInfo>( string_view, ParsedPropertyInfo&, const AnnotationMeta&, vector<string>& );
    extern template void AnnotationApply::apply<ParsedFunctionInfo>( string_view, ParsedFunctionInfo&, const AnnotationMeta&, vector<string>& );
} // namespace sw
