/**
 * @file AnnotationApply.h
 * @brief REFLECT/PROPERTY/FUNCTION/ENUM 어노테이션 문자열 → Parsed* 필드 적용
 * @details AnnotationMeta.txt 별칭을 정규 필드명으로 바꾼 뒤, 여기 테이블이 실제 멤버에 값을 넣습니다.
 *          새 PROPERTY 필드를 추가하면: AnnotationMeta.txt + 이 모듈의 apply 테이블을 함께 수정하세요.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "ReflectionParser/ParsedReflection.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) token — 어노테이션 문자열 유틸
	// ------------------------------------------------------------------------------
	/** @brief `PREFIX;args` 에서 접두사 이후 인자 텍스트만 반환합니다. */
	string_view annotationArgText( string_view spelling, string_view prefix );

	/** @brief 따옴표를 존중하며 쉼표로 인자 토큰을 나눕니다. */
	vector<string> splitAnnotationArgs( string_view args );

	// ------------------------------------------------------------------------------
	// 2) apply — 매크로별 Parsed* 채우기
	// ------------------------------------------------------------------------------
	void parseReflectAnnotation( string_view annotationSpelling, ParsedTypeInfo& typeInfo );
	void parseEnumAnnotation( string_view annotationSpelling, ParsedEnumInfo& enumInfo );
	void parsePropertyAnnotation( string_view annotationSpelling, ParsedPropertyInfo& prop );
	void parseFunctionAnnotation( string_view annotationSpelling, ParsedFunctionInfo& method );
} // namespace sw
