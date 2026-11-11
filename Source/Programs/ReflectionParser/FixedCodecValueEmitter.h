/**
 * @file FixedCodecValueEmitter.h
 * @brief 필드 값 하나의 쓰기 · 읽기 · 비교 코드와 정규 문자열을 같은 순서로 냅니다.
 * @details 값의 종류(`FixedValueKind`)마다 갈립니다. 코덱의 함수 틀은 `FixedCodecGenerator` 가 짭니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/FixedCodecEmitState.h"

namespace sw
{
    /** @brief 필드 값 하나의 쓰기 · 읽기 · 비교 코드와 정규 문자열을 같은 순서로 냅니다. */
    struct FixedCodecValueEmitter
    {
        /** @brief 타입의 프로퍼티를 선언 순서로 냅니다. 앞선 필드만 범위 이름으로 볼 수 있습니다. */
        static void emitProperties( FixedCodecEmitState& state, const ParsedTypeInfo& type, const string& objectExpr, const string& exprA, const string& exprB, int32 depth, string& outCanonical );

    private:
        static const FixedMathTypeDef* findMathType( string_view typeName );
        static vector<string>          splitMembers( string_view memberList );
        /** @brief 쓰기 · 읽기 · 비교 · 정규 문자열이 볼 원소 종류입니다. 컨테이너는 원소까지 내려갑니다. */
        static const ParsedFixedType& findLeafType( const ParsedFixedType& type );
        /** @brief 가변 길이 단계 수입니다(vector · 문자열). 중첩 REFLECT 타입 안은 그 타입의 키가 맡으므로 세지 않습니다. */
        static int32 countVariableLengthLevels( const ParsedFixedType& type );
        /** @brief 원소 타입까지 내려가 인코딩 표 밖의 것을 찾습니다. 없으면 빈 문자열입니다. */
        static string                findIneligibleReason( const ParsedFixedType& type );
        static void                  appendEnumCanonical( string& out, const ParsedFixedEnum& enumInfo );
        static void                  appendFieldHead( string& out, const ParsedPropertyInfo& prop );
        static void                  emitInteger( FixedCodecEmitState& state, const FixedCodecSite& site, const FixedCodecBound& minBound, const FixedCodecBound& maxBound );
        static void                  emitFloat( FixedCodecEmitState& state, const FixedCodecSite& site, FixedValueKind kind, const FixedCodecBound& minBound, const FixedCodecBound& maxBound );
        static void                  emitEnum( FixedCodecEmitState& state, const FixedCodecSite& site, const ParsedFixedEnum& enumInfo );
        static FixedCodecSite        makeChildSite( const FixedCodecSite& parent, const string& expr, const string& exprA, const string& exprB );
        static void                  emitContainer( FixedCodecEmitState& state, const FixedCodecSite& site, const ParsedFixedType& type, string& outCanonical );
        static const ParsedTypeInfo* findHeaderType( const FixedCodecEmitState& state, const string& fqn );
        /** @brief 같은 헤더의 중첩 타입을 부모 코덱 안에 펼칠 수 있는지 봅니다. 기반과 검증 함수가 없고 코덱 필드가 모두 공개여야 합니다. */
        static bool canInline( const FixedCodecEmitState& state, const ParsedTypeInfo* pNested );
        static void emitReflect( FixedCodecEmitState& state, const FixedCodecSite& site, const ParsedFixedType& type, string& outCanonical );
        static void emitValue( FixedCodecEmitState& state, const FixedCodecSite& site, const ParsedFixedType& type, string& outCanonical );
        /** @brief 열거형 값이 있는지 봅니다. `Count` 열거자는 값이 아닙니다. */
        static bool hasEnumValues( const ParsedFixedEnum& enumInfo );
        /**
         * @brief 프로퍼티 하나의 키를 검사하고 코드를 냅니다. 적격이 아니면 이유만 남기고 코드를 내지 않습니다.
         * @details 키를 잘못 적은 것은 타입이 적격이든 아니든 생성 오류입니다. 고칠 곳이 선언에 있기 때문입니다.
         */
        static void emitProperty( FixedCodecEmitState& state, const ParsedPropertyInfo& prop, const string& objectExpr, const string& exprA, const string& exprB, int32 depth, string& outCanonical );
    };
} // namespace sw
