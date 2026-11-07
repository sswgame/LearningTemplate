/**
 * @file FixedTypeAnalyzer.h
 * @brief 고정 배치형 코덱용으로 필드 타입을 인코딩 표(`Source/Engine/Serialization/Fixed/README.md` 3.4)에 맞춰 분류합니다.
 * @details 선언(clang 타입)에서 읽습니다. 다른 헤더의 열거형 · 구조체도 이 번역 단위 안에 선언이 있으므로 같은 길로 읽습니다.
 *          표 밖의 타입은 오류가 아닙니다. 그 타입의 코덱이 없을 뿐이고, 코덱이 필요한 등록 API 가 이유와 함께 멈춥니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/ParsedReflection.h"
#include "Programs/ReflectionParser/ParserSession.h"

#include <clang-c/Index.h>

namespace sw
{
    /** @brief 고정 배치형 코덱용으로 필드 타입을 인코딩 표(`Source/Engine/Serialization/Fixed/README.md` 3.4)에 맞춰 분류합니다. */
    struct FixedTypeAnalyzer
    {
        static shared_ptr<ParsedFixedType> makeIneligibleFixedType( shared_ptr<ParsedFixedType> type, string_view reason );

        /**
         * @brief 고정 배치형 코덱이 쓰는 선언 사실(줄 · 공개 여부 · 인코딩 분류)을 채웁니다.
         * @details 비트필드는 1 비트 플래그만 받으므로(위에서 막았습니다) 정수 · bool 비트필드는 bool 로 씁니다.
         */
        static void fillFixedLayoutFacts( CXCursor cursor, CXType fieldType, ParsedPropertyInfo& prop, const ParserSession& session );

    private:
        static constexpr int32 kMaxFixedTypeDepth = 4;
        static bool            isFixedIntegerKind( CXTypeKind kind ) noexcept;
        static bool            isFixedPointerKind( CXTypeKind kind ) noexcept;
        /** @brief 문자 하나가 8 비트인 문자열(`sw::string` · `std::string`)인지 봅니다. 넓은 문자열은 표 밖입니다. */
        static bool isFixedStringType( string_view normalizedName, string_view canonicalSpelling ) noexcept;
        /** @brief 열거형 선언에서 열거자 · `ENUM( Count, Flags )` 를 읽습니다. 다른 헤더의 열거형이면 그 헤더의 수집이 애노테이션 오류를 알립니다. */
        static shared_ptr<ParsedFixedEnum> collectFixedEnum( CXCursor declaration, const ParserSession& session );
        /**
         * @brief 필드(또는 원소) 타입 하나를 분류합니다. 컨테이너는 원소를 재귀로 분류합니다.
         * @param type 사용자 별칭을 벗긴 선언 타입
         */
        static shared_ptr<ParsedFixedType> classifyFixedType( CXType type, const ParserSession& session, int32 depth );
    };
} // namespace sw
