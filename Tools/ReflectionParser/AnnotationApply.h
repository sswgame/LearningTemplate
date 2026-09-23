/**
 * @file AnnotationApply.h
 * @brief REFLECT/PROPERTY/FUNCTION/ENUM 어노테이션 문자열을 Parsed* 필드에 적용합니다.
 * @details AnnotationMeta.txt 의 별칭을 정규 필드명으로 바꾼 뒤, 필드 테이블이 실제 멤버에 값을 넣습니다.
 *          새 필드를 추가하려면 PredefinedAnnotationField.xxx 에 한 줄을 더하고(대상 멤버는 ParsedReflection.h),
 *          별칭이 필요하면 AnnotationMeta.txt 에 적으십시오.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "ReflectionParser/AnnotationMeta.h"

namespace sw
{
    struct ParsedEnumInfo;
    struct ParsedFunctionInfo;
    struct ParsedPropertyInfo;
    struct ParsedTypeInfo;

    // ------------------------------------------------------------------------------
    // 1) AnnotationApply — 어노테이션 문자열 유틸 · 매크로별 Parsed* 채우기
    // ------------------------------------------------------------------------------
    /** @brief REFLECT/PROPERTY/FUNCTION/ENUM 어노테이션 문자열을 Parsed* 에 적용합니다. */
    struct AnnotationApply
    {
        /** @brief `PREFIX;args` 에서 접두사 이후 인자 텍스트만 반환합니다. */
        static string_view annotationArgumentText( string_view spelling, string_view prefix );

        /** @brief 따옴표를 존중하며 쉼표로 인자 토큰을 나눕니다. */
        static vector<string> splitAnnotationArgs( string_view args );

        static void parseReflectAnnotation( string_view annotationSpelling, ParsedTypeInfo& typeInfo, const AnnotationMeta& meta );
        static void parseEnumAnnotation( string_view annotationSpelling, ParsedEnumInfo& enumInfo, const AnnotationMeta& meta );
        static void parsePropertyAnnotation( string_view annotationSpelling, ParsedPropertyInfo& prop, const AnnotationMeta& meta );
        static void parseFunctionAnnotation( string_view annotationSpelling, ParsedFunctionInfo& method, const AnnotationMeta& meta );
    };
} // namespace sw
