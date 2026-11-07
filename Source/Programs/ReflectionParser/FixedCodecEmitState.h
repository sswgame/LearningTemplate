/**
 * @file FixedCodecEmitState.h
 * @brief 고정 배치형 코덱 하나를 만드는 동안의 상태와, 값 하나를 쓰는 자리 · 범위 끝을 담는 형식입니다.
 * @details 쓰기 · 읽기 · 비교 세 버퍼와 진단을 이 상태가 소유합니다. 범위 풀기(`FixedCodecBoundResolver`)와 값 코드(`FixedCodecValueEmitter`)가 함께 씁니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/CodeEmit.h"
#include "Programs/ReflectionParser/ParsedReflection.h"

namespace sw
{
    /** @brief 범위 한쪽 끝입니다. 숫자면 `_value` 이고, 필드 이름이면 `_expr`(쓰기 · 읽기 함수의 `object` 기준 식)입니다. */
    struct FixedCodecBound
    {
        string  _text; ///< 적힌 글입니다(진단용)
        string  _expr; ///< 필드 이름이면 C++ 식입니다(`( -object._halfSize )`)
        float64 _value;
        uint8   _bPresent;
        uint8   _bLiteral;

        FixedCodecBound() noexcept;
    };
} // namespace sw

namespace sw
{
    /** @brief 값 하나를 쓰는 자리입니다. 쓰기 · 읽기 식, 비교의 두 식, 원소마다의 범위를 담습니다. */
    struct FixedCodecSite
    {
        const ParsedPropertyInfo* _pProp;
        string                    _expr;
        string                    _exprA;
        string                    _exprB;
        vector<FixedCodecBound>   _listMin; ///< 하나면 모든 원소에, 수학 타입 원소 수만큼이면 원소마다 씁니다
        vector<FixedCodecBound>   _listMax;
        int32                     _depth;
        uint8                     _bBitField;
    };
} // namespace sw

namespace sw
{
    /** @brief 범위 이름을 찾는 범위 하나입니다. 타입, 그 객체의 식, 볼 수 있는 필드 수(앞선 필드만)를 담습니다. */
    struct FixedCodecScopeFrame
    {
        const ParsedTypeInfo* _pType;
        string                _objectExpr;
        size_t                _limit;
    };
} // namespace sw

namespace sw
{
    /** @brief 최상위 타입 하나의 코덱을 만드는 동안의 상태입니다. */
    struct FixedCodecEmitState
    {
        static constexpr int32       kMemberIndent   = 2;
        static constexpr const utf8* kReadOk         = "::sw::FixedReadResult::Ok";
        static constexpr const utf8* kReadOutOfRange = "::sw::FixedReadResult::OutOfRange";

        const ParsedHeader*            _pHeader;
        const string*                  _pSourcePath;
        const ParsedTypeInfo*          _pRoot;
        CodeEmitBuffer                 _writeBuffer;
        CodeEmitBuffer                 _readBuffer;
        CodeEmitBuffer                 _equalsBuffer;
        CodeEmit                       _write;
        CodeEmit                       _read;
        CodeEmit                       _equals;
        string                         _canonical;
        vector<pair<string, string>>   _listDependency;
        vector<const ParsedFixedEnum*> _listEnum;
        vector<FixedCodecScopeFrame>   _listScope;
        vector<string>                 _listInlineFqn;   ///< 지금 펼치는 중첩 타입들입니다(순환을 막습니다)
        vector<string>                 _listResolvedFqn; ///< 펼치며 바깥 범위 이름을 푼 중첩 타입입니다
        string                         _reason;
        string                         _unresolvedName;
        string                         _unresolvedMember;
        uint32                         _unresolvedLine;
        uint8                          _bError;

        FixedCodecEmitState( const ParsedHeader& header, const string& sourcePath, const ParsedTypeInfo& root );

        void reportError( const ParsedPropertyInfo& prop, string_view message );
        /** @brief 첫 이유만 남깁니다. 타입이 적격이 아닌 까닭은 하나면 충분합니다. 오류와 달리 생성은 계속됩니다. */
        void markIneligible( const ParsedPropertyInfo& prop, string_view reason );
        void addDependency( const string& fieldName, const string& fqn );
        void addEnum( const ParsedFixedEnum& enumInfo );
        void emitReadCheck( const string& call );
        void emitWriteCheck( const string& call );
        void emitEqualsCheck( const string& condition );

        static string formatFloat( float64 value );
        static string formatInteger( int64 value );
        static string makeEnumHelperName( const ParsedFixedEnum& enumInfo );
        static string makeCodecAccessorName( const string& fqn );
    };
} // namespace sw
