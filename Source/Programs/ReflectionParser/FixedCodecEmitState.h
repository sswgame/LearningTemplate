/**
 * @file FixedCodecEmitState.h
 * @brief 고정 배치형 코덱 하나를 만드는 동안의 상태와, 값 하나를 쓰는 자리 · 범위 끝 · 서술자 칸을 담는 형식입니다.
 * @details 서술자 칸(`FixedCodecNode`)과 진단을 이 상태가 소유합니다. 범위 풀기(`FixedCodecBoundResolver`)와 값 칸(`FixedCodecValueEmitter`)이 함께 씁니다.
 *          생성 코드는 본문 없이 칸 표(`::sw::FixedFieldDesc`)만 낸다 — 해석은 런타임 `FixedLayout` 하나가 한다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/ParsedReflection.h"

namespace sw
{
    /** @brief 범위 한쪽 끝입니다. 숫자면 `_value` 와 적힌 글이고, 필드 이름이면 그 필드가 사는 범위 깊이 · 타입 · 이름입니다. */
    struct FixedCodecBound
    {
        string  _text;      ///< 적힌 글입니다(숫자면 C++ 리터럴로 그대로 낸다)
        string  _ownerFqn;  ///< 필드 이름이면 그 필드를 가진 타입입니다
        string  _fieldName; ///< 필드 이름이면 그 C++ 멤버 이름입니다
        float64 _value;
        uint32  _scopeDepth; ///< 필드 이름이면 그 범위 깊이입니다(0 이 최상위 타입)
        uint8   _bPresent;
        uint8   _bLiteral;
        uint8   _bNegate;

        FixedCodecBound() noexcept;
    };
} // namespace sw

namespace sw
{
    /** @brief 값 하나를 쓰는 자리입니다. 서술자 칸의 오프셋 · 값 타입 식 · 원소마다의 범위를 담습니다. */
    struct FixedCodecSite
    {
        const ParsedPropertyInfo* _pProp;
        string                    _ownerFqn;   ///< 이 값을 멤버로 가진 타입입니다(비트필드 접근 함수가 씁니다). 원소면 비어 있습니다
        string                    _offsetExpr; ///< 바로 위 묶음 기준 오프셋 식입니다(`offsetof( ::sw::Foo, _x )`, 원소는 `0`)
        string                    _typeExpr;   ///< 값의 C++ 타입 식입니다(`decltype( ::sw::Foo::_x )`, 원소는 `::sw::FixedElementType< … >`)
        vector<FixedCodecBound>   _listMin;    ///< 하나면 모든 원소에, 수학 타입 원소 수만큼이면 원소마다 씁니다
        vector<FixedCodecBound>   _listMax;
        uint8                     _bBitField;
    };
} // namespace sw

namespace sw
{
    /** @brief 서술자 칸 하나입니다. 자식이 있으면 표를 펼 때 그 첫 칸 번호(묶음이면 개수도)를 `_call` 뒤에 붙입니다. */
    struct FixedCodecNode
    {
        /** @brief 자식 번호를 붙이는 꼴입니다. */
        enum class ChildArgs : uint8
        {
            None,    ///< 자식이 없습니다
            Element, ///< 원소 한 칸입니다(`, first`)
            Group,   ///< 이어진 칸들입니다(`, first, count`)
        };

        string                 _call; ///< 닫는 괄호 없는 `::sw::FixedFieldDesc::make…( …` 글입니다
        vector<FixedCodecNode> _listChild;
        ChildArgs              _childArgs;

        FixedCodecNode() noexcept;
        FixedCodecNode( string call, ChildArgs childArgs ) noexcept;
    };
} // namespace sw

namespace sw
{
    /** @brief 범위 이름을 찾는 범위 하나입니다. 타입과 볼 수 있는 필드 수(앞선 필드만)를 담습니다. 범위 깊이는 목록의 자리입니다. */
    struct FixedCodecScopeFrame
    {
        const ParsedTypeInfo* _pType;
        size_t                _limit;
    };
} // namespace sw

namespace sw
{
    /** @brief 최상위 타입 하나의 코덱을 만드는 동안의 상태입니다. */
    struct FixedCodecEmitState
    {
        const ParsedHeader*            _pHeader;
        const string*                  _pSourcePath;
        const ParsedTypeInfo*          _pRoot;
        vector<FixedCodecNode>         _listRootNode; ///< 최상위 필드 칸입니다
        vector<pair<string, string>>   _listDependency;
        vector<const ParsedFixedEnum*> _listEnum; ///< 열거자 값 표를 낼 열거형입니다(플래그가 아닌 것)
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
        /** @brief 의존을 더하고(같은 타입은 한 번) 그 번호를 돌려줍니다. 런타임 코덱의 의존 표 차례와 같습니다. */
        uint32 addDependency( const string& fieldName, const string& fqn );
        void   addEnum( const ParsedFixedEnum& enumInfo );

        static string formatFloat( float64 value );
        static string formatInteger( int64 value );
        /** @brief 플래그가 아닌 열거형의 등록 값 표 이름입니다(`static constexpr int64 …[]`). */
        static string makeEnumValueTableName( const ParsedFixedEnum& enumInfo );
    };
} // namespace sw
