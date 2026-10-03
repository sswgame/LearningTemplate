/**
 * @file AnnotationFields.h
 * @brief 애노테이션 필드 표입니다. `PredefinedAnnotationField.xxx` 한 줄이 필드 하나의 적용 · 코드젠 · 검증을 모두 정합니다.
 * @details 스코프(REFLECT · ENUM · PROPERTY · FUNCTION)마다 표가 하나 있고, 수집 DTO 타입으로 꺼냅니다
 *          (`getAnnotationScope<ParsedPropertyInfo>()`). 토큰을 넣는 쪽(`AnnotationApply`)과 생성 코드를 쓰는 쪽
 *          (`CodeGenerator`)이 **같은 표를 돕니다** — 둘 다 필드 이름을 손으로 들지 않습니다. 그래서 `.xxx` 한 줄과 DTO 멤버만
 *          더하면 읽기와 코드젠이 함께 따라옵니다(코드젠이 필드를 손으로 나열하면 파서는 값을 읽고 코드젠은 아무 말 없이 버린다).
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "ReflectionParser/ParsedReflection.h"
#include "ReflectionParser/ParserDefines.h"

namespace sw
{
    class AnnotationMeta;
    class CodeEmit;

    // ------------------------------------------------------------------------------
    // 1) 필드 한 줄
    // ------------------------------------------------------------------------------
    /** @brief 필드 값이 생성 코드의 어디로 가는지입니다(PredefinedAnnotationField.xxx 의 Emit 열). */
    enum class AnnotationEmit : uint8
    {
        Editor,  ///< `#if !defined( SW_SHIPPING )` 블록 안의 `<접두>_metadata.<멤버>`
        Runtime, ///< 같은 꼴, 블록 밖
        Manual,  ///< CodeGenerator 가 직접 쓴다(템플릿 변수 · 별칭 등록 · 범위처럼 메타데이터 멤버가 아닌 자리)
    };

    /** @brief 필드가 받는 값의 종류입니다. DTO 멤버 타입에서 나오며, AnnotationMeta.txt 의 kind 와 대조합니다. */
    enum class AnnotationValue : uint8
    {
        Bool,    ///< uint8 비트필드 플래그 ← flag · bool
        String,  ///< string · 목록 ← string
        Float,   ///< float32 ← float
        NetRole, ///< FunctionNetRole ← netrole
    };

    /** @brief 필드 한 줄입니다. 함수 포인터 셋은 `.xxx` 의 한 줄에서 만들어집니다. */
    template <typename TParsed>
    struct AnnotationField
    {
        string_view _id; ///< 정규 필드명(AnnotationMeta.txt 의 `kind.Id`)
        /** @brief 토큰 값을 대상에 넣습니다. 단독 토큰이면 값은 빈 문자열입니다(bool 로는 참). */
        void ( *_pApply )( TParsed& target, string_view value );
        /** @brief 코드젠에 쓸 값이 있는지 봅니다. Manual 이면 nullptr 입니다. */
        bool ( *_pIsSet )( const TParsed& parsed );
        /** @brief `<접두><멤버> = 값;` 을 씁니다(값이 없으면 아무것도 안 씁니다). Manual 이면 nullptr 입니다. */
        void ( *_pEmit )( CodeEmit& emit, string_view prefix, const TParsed& parsed );
        AnnotationValue _value;
        AnnotationEmit  _emit;
    };

    // ------------------------------------------------------------------------------
    // 2) 스코프 — 애노테이션 계약 한 벌 + 필드 표
    // ------------------------------------------------------------------------------
    /** @brief 한 스코프(REFLECT · ENUM · PROPERTY · FUNCTION)의 애노테이션 계약과 필드 표입니다. */
    template <typename TParsed>
    struct AnnotationScope
    {
        const ReflectAnnotationDesc*    _pDesc; ///< 매크로 · annotate 접두사 · AnnotationMeta.txt 섹션 이름
        const AnnotationField<TParsed>* _pField;
        uint32                          _fieldCount;

        const AnnotationField<TParsed>* begin() const noexcept { return _pField; }
        const AnnotationField<TParsed>* end() const noexcept { return _pField + _fieldCount; }

        /** @brief 정규 필드명으로 줄을 찾습니다. 없으면 nullptr 입니다. */
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

    // ------------------------------------------------------------------------------
    // 3) 표를 도는 일 — 코드젠 · 검증
    // ------------------------------------------------------------------------------
    struct AnnotationFields
    {
        /**
         * @brief Editor · Runtime 필드를 표 순서대로 씁니다.
         * @details Editor 묶음은 쓸 것이 있을 때만 `#if !defined( SW_SHIPPING )` 로 감쌉니다(빈 블록을 남기지 않습니다).
         * @param prefix 생성 코드의 대상 식. 예: "p._metadata." · "info._metadata." · "info."
         */
        template <typename TParsed>
        static void emitMetadata( CodeEmit& emit, const TParsed& parsed, string_view prefix );

        /**
         * @brief AnnotationMeta.txt 의 바인딩과 필드 표가 서로를 빠짐없이 가리키는지 봅니다.
         * @details 한쪽에만 있으면 그 토큰은 경고 없이 사라지거나(표에 없는 필드) 적을 수 없는 필드(철자 없는 줄)가
         *          됩니다. 둘 다 애노테이션을 적는 자리에서는 보이지 않으므로, 파서가 시작할 때 막습니다.
         * @return 어긋난 곳이 없으면 true
         */
        static bool validateBindings( const AnnotationMeta& meta );
    };

    // 정의는 AnnotationFields.cpp 에 있고 네 DTO 로만 인스턴스화합니다.
    extern template void AnnotationFields::emitMetadata<ParsedTypeInfo>( CodeEmit&, const ParsedTypeInfo&, string_view );
    extern template void AnnotationFields::emitMetadata<ParsedEnumInfo>( CodeEmit&, const ParsedEnumInfo&, string_view );
    extern template void AnnotationFields::emitMetadata<ParsedPropertyInfo>( CodeEmit&, const ParsedPropertyInfo&, string_view );
    extern template void AnnotationFields::emitMetadata<ParsedFunctionInfo>( CodeEmit&, const ParsedFunctionInfo&, string_view );
} // namespace sw
