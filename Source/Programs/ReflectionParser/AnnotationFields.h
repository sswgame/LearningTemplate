/**
 * @file AnnotationFields.h
 * @brief 애노테이션 필드 표입니다. `PredefinedAnnotationField.xxx` 한 줄이 필드 하나의 적용 · 코드젠 · 검증을 모두 정합니다.
 * @details 스코프(REFLECT · ENUM · PROPERTY · FUNCTION)마다 표가 하나 있고, 수집 DTO 타입으로 꺼냅니다
 *          (`getAnnotationScope<ParsedPropertyInfo>()`). 토큰을 넣는 쪽(`AnnotationApply`)과 생성 코드를 쓰는 쪽
 *          (`CodeGenerator`)이 같은 표를 돕니다 — 둘 다 필드 이름을 손으로 들지 않습니다. 그래서 `.xxx` 한 줄과 DTO 멤버만
 *          더하면 읽기와 코드젠이 함께 따라옵니다(코드젠이 필드를 손으로 나열하면 파서는 값을 읽고 코드젠은 아무 말 없이 버린다).
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/ParsedReflection.h"
#include "Programs/ReflectionParser/ParserDefines.h"

namespace sw
{
    /** @brief 필드 줄들이 모은 코드젠 값입니다 — 생성 코드의 상수 표(플래그 비트 · 글 줄)가 된다. 이름은 엔진 쪽 상수 이름이다(멤버 `_bX` · `_x` 의 X). */
    struct AnnotationOutput
    {
        vector<string>               _listFlag; ///< 켜진 플래그의 이름
        vector<pair<string, string>> _listText; ///< 글 칸의 이름 → 글
        vector<pair<string, string>> _listCustomMeta;
        FunctionNetRole              _netRole{ FunctionNetRole::Local };

        /** @brief Runtime 필드를 표 순서대로 모읍니다(등록 단위 .gen.cpp 의 상수 표). 프로퍼티 · 메서드만 Runtime 필드를 갖는다(AnnotationFields.cpp 의 `static_assert`). */
        template <typename TParsed>
        static AnnotationOutput collectRuntime( const TParsed& parsed );
        /** @brief Display · Editor 필드를 표 순서대로 모읍니다(개발 전용 단위 `<이름>.gen.meta.cpp` 의 줄 표). 배포 구성은 그 단위를 짓지 않는다 — 구조체 칸을 `#if` 로 가르지 않는다. */
        template <typename TParsed>
        static AnnotationOutput collectEditor( const TParsed& parsed );
    };

    // ------------------------------------------------------------------------------
    // 1) 필드 한 줄
    // ------------------------------------------------------------------------------
    /** @brief 필드 값이 생성 코드의 어디로 가는지입니다(PredefinedAnnotationField.xxx 의 Emit 열). */
    enum class AnnotationEmit : uint8
    {
        Display, ///< Editor 와 같은 자리에 쓰는 표시 메타 — 어느 멤버(이벤트 포함)에나 붙는다
        Editor,  ///< 개발 전용 단위(`<이름>.gen.meta.cpp`)의 에디터 메타데이터 줄(엔진 `EditorMetadataField`)
        Runtime, ///< 등록 단위(.gen.cpp)의 런타임 메타데이터 칸(엔진 `PropertyMetadataFlag` · `PropertyMetadataTextKind` · `FunctionMetadataFlag`)
        Manual,  ///< CodeGenerator 가 직접 쓴다(템플릿 변수 · 별칭 등록 · 범위처럼 메타데이터 멤버가 아닌 자리)
    };

    /** @brief 필드가 받는 값의 종류입니다. DTO 멤버 타입에서 나오며, 토큰의 꼴(단독 · `키 = 값`)을 정합니다. */
    enum class AnnotationValue : uint8
    {
        Bool,    ///< uint8 비트필드 플래그 ← flag · bool
        String,  ///< string · 목록 ← string
        Float,   ///< float32 ← float
        NetRole, ///< FunctionNetRole ← netrole
        Range,   ///< ParsedRangeExpr ← float. 숫자 하나, 또는 숫자 · 필드 이름(`-_halfSize`)을 쉼표로 나눈 목록입니다
        Count,   ///< uint32 ← count. 1 이상의 정수입니다
    };

    /** @brief 필드 한 줄입니다. 함수 포인터 둘은 `.xxx` 의 한 줄에서 만들어집니다. */
    template <typename TParsed>
    struct AnnotationField
    {
        string_view _id; ///< 소스에 적는 키 철자(`PredefinedAnnotationField.xxx` 의 Key)
        /** @brief 토큰 값을 대상에 넣습니다. 단독 토큰이면 값은 빈 문자열입니다(bool 로는 참). */
        void ( *_pApply )( TParsed& target, string_view value );
        /** @brief 값을 @p outOutput 에 모읍니다(값이 없으면 아무것도 안 모읍니다). Manual 이면 nullptr 입니다. */
        void ( *_pCollect )( AnnotationOutput& outOutput, const TParsed& parsed );
        AnnotationValue _value;
        AnnotationEmit  _emit;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) 스코프 — 애노테이션 계약 한 벌 + 필드 표
    // ------------------------------------------------------------------------------
    /** @brief 한 스코프(REFLECT · ENUM · PROPERTY · FUNCTION)의 애노테이션 계약과 필드 표입니다. */
    template <typename TParsed>
    struct AnnotationScope
    {
        const ReflectAnnotationDesc*    _pDesc; ///< 매크로 · 애노테이션 접두사 · 스코프 이름
        const AnnotationField<TParsed>* _pField;
        uint32                          _fieldCount;

        const AnnotationField<TParsed>* begin() const noexcept { return _pField; }
        const AnnotationField<TParsed>* end() const noexcept { return _pField + _fieldCount; }

        /** @brief 키 철자로 줄을 찾습니다. 없으면 nullptr 입니다. */
        const AnnotationField<TParsed>* findField( const string_view id ) const noexcept
        {
            for ( const AnnotationField<TParsed>& field : *this )
            {
                if ( field._id == id )
                    return &field;
            }
            return nullptr;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 수집 DTO 타입의 스코프를 돌려줍니다. 네 DTO 에 대해서만 정의됩니다. */
    template <typename TParsed>
    const AnnotationScope<TParsed>& getAnnotationScope();

    template <>
    const AnnotationScope<ParsedTypeInfo>& getAnnotationScope<ParsedTypeInfo>();
    template <>
    const AnnotationScope<ParsedEnumInfo>& getAnnotationScope<ParsedEnumInfo>();
    template <>
    const AnnotationScope<ParsedPropertyInfo>& getAnnotationScope<ParsedPropertyInfo>();
    template <>
    const AnnotationScope<ParsedFunctionInfo>& getAnnotationScope<ParsedFunctionInfo>();
    template <>
    const AnnotationScope<ParsedEnumeratorInfo>& getAnnotationScope<ParsedEnumeratorInfo>();

    // ------------------------------------------------------------------------------
    // 3) 표를 도는 일 — 코드젠 · 검증
    // ------------------------------------------------------------------------------
    struct AnnotationFields
    {
        /** @brief 이벤트 필드의 PROPERTY 가 받는 필드인지 봅니다 — 표에서 Display 인 PROPERTY 줄과 이름(`Name`)입니다. */
        static bool canAcceptOnEvent( string_view propertyFieldID );
        /** @brief 이벤트가 받는 필드 이름을 ", " 로 이은 글입니다(오류 글). */
        static string makeEventFieldList();
    };

    // 정의는 AnnotationFields.cpp 에 있고 네 DTO 로만 인스턴스화합니다.
    extern template AnnotationOutput AnnotationOutput::collectRuntime<ParsedPropertyInfo>( const ParsedPropertyInfo& );
    extern template AnnotationOutput AnnotationOutput::collectRuntime<ParsedFunctionInfo>( const ParsedFunctionInfo& );
    extern template AnnotationOutput AnnotationOutput::collectEditor<ParsedTypeInfo>( const ParsedTypeInfo& );
    extern template AnnotationOutput AnnotationOutput::collectEditor<ParsedEnumInfo>( const ParsedEnumInfo& );
    extern template AnnotationOutput AnnotationOutput::collectEditor<ParsedPropertyInfo>( const ParsedPropertyInfo& );
    extern template AnnotationOutput AnnotationOutput::collectEditor<ParsedFunctionInfo>( const ParsedFunctionInfo& );
    extern template AnnotationOutput AnnotationOutput::collectEditor<ParsedEnumeratorInfo>( const ParsedEnumeratorInfo& );
} // namespace sw
