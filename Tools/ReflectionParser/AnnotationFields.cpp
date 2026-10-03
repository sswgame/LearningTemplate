#include "pch.h"

#include "ReflectionParser/AnnotationFields.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

#include "Engine/Reflection/ReflectionEnumNames.h"

#include "ReflectionParser/AnnotationMeta.h"
#include "ReflectionParser/CodeEmit.h"

SW_LOG_CALLER( "AnnotationFields" );
namespace sw
{
    namespace
    {
        struct AnnotationFieldsInternal
        {
            // ------------------------------------------------------------------------------
            // A) 값의 종류 — DTO 멤버 타입이 정한다
            // ------------------------------------------------------------------------------
            /** @brief 멤버 타입이 받는 값의 종류입니다. 목록(vector)은 문자열 하나를 나눠 담으므로 String 입니다. */
            template <typename TMember>
            static constexpr AnnotationValue getValueKind() noexcept
            {
                if constexpr ( std::is_same_v<TMember, uint8> )
                    return AnnotationValue::Bool;
                else if constexpr ( std::is_same_v<TMember, float32> )
                    return AnnotationValue::Float;
                else if constexpr ( std::is_same_v<TMember, FunctionNetRole> )
                    return AnnotationValue::NetRole;
                else
                    return AnnotationValue::String;
            }

            /**
             * @brief 그대로 넣는 줄(`REGISTER_ANNOTATION_FIELD`)의 값 변환입니다.
             * @details 단독 토큰은 값이 비어 있고, 빈 값은 참입니다(`PROPERTY( ReadOnly )`). `X = false` 는 거짓을 넣습니다 — 네 스코프
             *          모두 같은 규칙입니다.
             */
            template <typename TMember>
            static TMember convertValue( const string_view value )
            {
                if constexpr ( std::is_same_v<TMember, uint8> )
                {
                    return static_cast<uint8>( StringUtil::parseBool( value, true ) ? SW_TRUE : SW_FALSE );
                }
                else
                {
                    static_assert( std::is_same_v<TMember, string>,
                                   "REGISTER_ANNOTATION_FIELD stores uint8 flags and strings only - use REGISTER_ANNOTATION_FIELD_FN" );
                    return TMember( value );
                }
            }

            // ------------------------------------------------------------------------------
            // B) 코드젠 — 멤버 타입마다 한 벌
            // ------------------------------------------------------------------------------
            static bool isValueSet( const uint8 value ) noexcept { return value == SW_TRUE; }
            static bool isValueSet( const string& value ) noexcept { return value.empty() == false; }
            static bool isValueSet( const vector<pair<string, string>>& listPair ) noexcept { return listPair.empty() == false; }
            static bool isValueSet( const FunctionNetRole role ) noexcept { return role != FunctionNetRole::Local; }

            static void emitValue( CodeEmit& emit, const string_view prefix, const string_view member, const uint8 value )
            {
                if ( isValueSet( value ) )
                    emit.linef( "%#%# = SW_TRUE;", prefix, member );
            }

            static void emitValue( CodeEmit& emit, const string_view prefix, const string_view member, const string& value )
            {
                if ( isValueSet( value ) )
                    emit.linef( "%#%# = \"%#\";", prefix, member, CodeEmit::escapeCppString( value ) );
            }

            /** @brief 커스텀 메타는 엔진에서 `_mapCustomMeta` 입니다(DTO 는 적은 순서를 지키는 목록). */
            static void emitValue( CodeEmit& emit, const string_view prefix, const string_view, const vector<pair<string, string>>& listPair )
            {
                if ( isValueSet( listPair ) == false )
                    return;
                emit.linef( "%#_mapCustomMeta = {", prefix );
                emit.push();
                for ( const auto& [key, val] : listPair )
                    emit.linef( "{ %#, %# },", CodeEmit::hs( key ), CodeEmit::quoted( val ) );
                emit.pop();
                emit.line( "};" );
            }

            static void emitValue( CodeEmit& emit, const string_view prefix, const string_view member, const FunctionNetRole role )
            {
                if ( isValueSet( role ) )
                    emit.linef( "%#%# = %#;", prefix, member, toCppExpr( role ) );
            }

            // ------------------------------------------------------------------------------
            // C) 손으로 넣는 줄(`REGISTER_ANNOTATION_FIELD_FN`)의 Fn
            // ------------------------------------------------------------------------------
            /** @brief 쉼표 · 세미콜론으로 나눈 별칭(이름을 바꾸기 전의 이름)들을 붙입니다. `Alias = "OldA, OldB"`. */
            template <typename TParsed>
            static void appendAlias( TParsed& target, const string_view value )
            {
                const string_splitter parts( value, { ",", ";" } );
                for ( const string_view token : parts.getSplitList() )
                {
                    const string_view trimmed = StringUtil::trim( token );
                    if ( trimmed.empty() == false )
                        target._listAlias.emplace_back( trimmed );
                }
            }

            /** @brief `Key=Value, Key2` 를 커스텀 메타 쌍으로 붙입니다. 값이 없는 키는 "1" 입니다. */
            template <typename TParsed>
            static void appendCustomMeta( TParsed& target, const string_view value )
            {
                const string_splitter parts( value, { ",", ";" } );
                for ( const string_view tokenView : parts.getSplitList() )
                {
                    const string_view token = StringUtil::trim( tokenView );
                    if ( token.empty() )
                        continue;
                    const size_t eqPos = token.find( '=' );
                    if ( eqPos == string_view::npos )
                    {
                        target._listCustomMeta.emplace_back( string( token ), "1" );
                        continue;
                    }
                    const string_view key = StringUtil::trim( token.substr( 0, eqPos ) );
                    const string_view val = StringUtil::trim( token.substr( eqPos + 1 ) );
                    if ( key.empty() == false )
                        target._listCustomMeta.emplace_back( string( key ), string( val ) );
                }
            }

            /** @brief `Old:Current` 목록을 열거자 별칭으로 붙입니다. `ValueAlias = "OldIdle:Idle"`. */
            static void appendValueAlias( ParsedEnumInfo& target, const string_view value )
            {
                const string_splitter parts( value, { ",", ";" } );
                for ( const string_view tokenView : parts.getSplitList() )
                {
                    const string_view token = StringUtil::trim( tokenView );
                    if ( token.empty() )
                        continue;
                    const size_t colon = token.find( ':' );
                    if ( colon == string_view::npos || colon == 0 || colon + 1 >= token.size() )
                    {
                        SW_LOG_WARNING( "ENUM ValueAlias expected Old:Current, got '%#'", token );
                        continue;
                    }
                    const string_view alias     = StringUtil::trim( token.substr( 0, colon ) );
                    const string_view canonical = StringUtil::trim( token.substr( colon + 1 ) );
                    if ( alias.empty() == false && canonical.empty() == false )
                        target._listValueAlias.emplace_back( string( alias ), string( canonical ) );
                }
            }

            /** @brief 에셋 타입을 적으면 그 프로퍼티는 에셋 경로이기도 합니다. */
            static void applyAssetType( ParsedPropertyInfo& target, const string_view value )
            {
                target._assetType  = string( value );
                target._bAssetPath = SW_TRUE;
            }

            /** @brief 아래 경계만 적는다 — 위 경계는 따로다(한쪽만 적은 범위는 그쪽만 막는다). 숫자가 아니면 `AnnotationApply` 가 이미 거절했다. */
            static void applyMinRange( ParsedPropertyInfo& target, const string_view value )
            {
                float32 parsed{ 0.0f };
                if ( StringUtil::parseFloat( value, parsed ) == false )
                    return;
                target._minRange     = parsed;
                target._bHasMinRange = SW_TRUE;
            }

            static void applyMaxRange( ParsedPropertyInfo& target, const string_view value )
            {
                float32 parsed{ 0.0f };
                if ( StringUtil::parseFloat( value, parsed ) == false )
                    return;
                target._maxRange     = parsed;
                target._bHasMaxRange = SW_TRUE;
            }

            /** @brief 넷 역할은 토큰 자체가 값입니다(`FUNCTION( Server )` → "Server"). */
            static void applyNetRole( ParsedFunctionInfo& target, const string_view value )
            {
                FunctionNetRole role = FunctionNetRole::Local;
                if ( tryParseFunctionNetRole( value, role ) )
                    target._netRole = role;
            }

            // ------------------------------------------------------------------------------
            // D) 검증 — AnnotationMeta.txt ↔ 필드 표
            // ------------------------------------------------------------------------------
            /** @brief 필드가 받는 값의 종류가 AnnotationMeta.txt 의 kind 와 맞는지 봅니다. */
            static bool acceptsKind( const AnnotationValue value, const AnnotationBinding::Kind kind ) noexcept
            {
                switch ( value )
                {
                    case AnnotationValue::Bool:
                        return kind == AnnotationBinding::Kind::Flag || kind == AnnotationBinding::Kind::Bool;
                    case AnnotationValue::String:
                        return kind == AnnotationBinding::Kind::String;
                    case AnnotationValue::Float:
                        return kind == AnnotationBinding::Kind::Float;
                    case AnnotationValue::NetRole:
                        return kind == AnnotationBinding::Kind::NetRole;
                }
                return false;
            }

            /** @brief 바인딩이 가리키는 필드 이름입니다. 넷 역할은 필드 이름 자리에 역할을 적으므로 NetRole 필드입니다. */
            static string_view getBoundFieldId( const AnnotationBinding& binding ) noexcept
            {
                if ( binding._kind == AnnotationBinding::Kind::NetRole )
                    return annotationConstants::kNetRoleField;
                return binding._field;
            }

            template <typename TParsed>
            static bool isInScope( const AnnotationMetaEntry& entry )
            {
                return entry._scope == getAnnotationScope<TParsed>()._pDesc->_pScope;
            }

            /** @brief 바인딩 한 줄이 이 스코프의 필드를 알맞은 종류로 가리키는지 봅니다. */
            template <typename TParsed>
            static bool isBindingValid( const AnnotationMetaEntry& entry )
            {
                const AnnotationScope<TParsed>& scope   = getAnnotationScope<TParsed>();
                const AnnotationBinding&        binding = entry._binding;
                const AnnotationField<TParsed>* pField  = scope.findField( getBoundFieldId( binding ) );
                if ( pField == nullptr )
                {
                    SW_LOG_ERROR( "AnnotationMeta.txt [%#] binds '%#', but PredefinedAnnotationField.xxx has no %# row for it "
                                  "- every token spelled that way would be dropped.",
                                  entry._scope, binding._field, getBoundFieldId( binding ) );
                    return false;
                }
                if ( acceptsKind( pField->_value, binding._kind ) == false )
                {
                    SW_LOG_ERROR( "AnnotationMeta.txt [%#] declares '%#' as %#, but its field member in ParsedReflection.h "
                                  "holds another kind of value.",
                                  entry._scope, binding._field, toString( binding._kind ) );
                    return false;
                }

                FunctionNetRole role = FunctionNetRole::Local;
                if ( binding._kind == AnnotationBinding::Kind::NetRole && tryParseFunctionNetRole( binding._field, role ) == false )
                {
                    SW_LOG_ERROR( "AnnotationMeta.txt [%#] names net role '%#', which PredefinedFunctionNetRole.xxx does not define.",
                                  entry._scope, binding._field );
                    return false;
                }
                return true;
            }

            /** @brief 이 스코프의 필드마다 적을 철자가 하나 이상 있는지 봅니다. 없으면 그 줄은 아무도 켤 수 없습니다. */
            template <typename TParsed>
            static bool isEveryFieldBound( const AnnotationMeta& meta )
            {
                bool bValid = true;
                for ( const AnnotationField<TParsed>& field : getAnnotationScope<TParsed>() )
                {
                    bool bBound = false;
                    for ( const AnnotationMetaEntry& entry : meta.getEntries() )
                    {
                        if ( isInScope<TParsed>( entry ) && getBoundFieldId( entry._binding ) == field._id )
                        {
                            bBound = true;
                            break;
                        }
                    }
                    if ( bBound == false )
                    {
                        SW_LOG_ERROR( "PredefinedAnnotationField.xxx has a %# '%#' row, but AnnotationMeta.txt gives it no spelling "
                                      "- nothing can set it.",
                                      getAnnotationScope<TParsed>()._pDesc->_pScope, field._id );
                        bValid = false;
                    }
                }
                return bValid;
            }
        };

        // ------------------------------------------------------------------------------
        // 필드 표 — PredefinedAnnotationField.xxx 를 스코프마다 한 번씩 전개한다.
        //
        // 플래그 멤버가 `uint8 : 1` 비트필드라 멤버 포인터를 만들 수 없다. 그래서 줄마다 대입 · 조회 · 출력 코드를 매크로가
        // 만든다. 표가 구조체 밖에 있는 까닭: 정적 constexpr 멤버의 초기화식에서는 같은 구조체의 constexpr 함수
        // (`getValueKind`)를 아직 부를 수 없다(구조체가 완성되기 전이다).
        // ------------------------------------------------------------------------------
#define SW_ANNOTATION_TARGET_Reflect  ParsedTypeInfo
#define SW_ANNOTATION_TARGET_Enum     ParsedEnumInfo
#define SW_ANNOTATION_TARGET_Property ParsedPropertyInfo
#define SW_ANNOTATION_TARGET_Function ParsedFunctionInfo

#define SW_ANNOTATION_OUTPUT_Editor( TParsed, Member )                                             \
    []( const TParsed& parsed ) { return AnnotationFieldsInternal::isValueSet( parsed.Member ); }, \
        []( CodeEmit& emit, string_view prefix, const TParsed& parsed ) { AnnotationFieldsInternal::emitValue( emit, prefix, #Member, parsed.Member ); }
#define SW_ANNOTATION_OUTPUT_Runtime                   SW_ANNOTATION_OUTPUT_Editor
#define SW_ANNOTATION_OUTPUT_Manual( TParsed, Member ) nullptr, nullptr

#define SW_ANNOTATION_ROW( TParsed, Id, Member, ApplyStatement, Emit )                                                   \
    { #Id, []( TParsed& target, string_view value ) { ApplyStatement; }, SW_ANNOTATION_OUTPUT_##Emit( TParsed, Member ), \
      AnnotationFieldsInternal::getValueKind<decltype( TParsed::Member )>(), AnnotationEmit::Emit },

#define REGISTER_ANNOTATION_FIELD( Scope, Id, Member, Emit )                                                                                 \
    SW_ANNOTATION_IN_##Scope( SW_ANNOTATION_ROW( SW_ANNOTATION_TARGET_##Scope, Id, Member,                                                   \
                                                 target.Member = AnnotationFieldsInternal::convertValue<decltype( target.Member )>( value ), \
                                                 Emit ) )
#define REGISTER_ANNOTATION_FIELD_FN( Scope, Id, Member, Fn, Emit ) \
    SW_ANNOTATION_IN_##Scope( SW_ANNOTATION_ROW( SW_ANNOTATION_TARGET_##Scope, Id, Member, AnnotationFieldsInternal::Fn( target, value ), Emit ) )

        // ── REFLECT ──
#define SW_ANNOTATION_IN_Reflect( Row ) Row
#define SW_ANNOTATION_IN_Enum( Row )
#define SW_ANNOTATION_IN_Property( Row )
#define SW_ANNOTATION_IN_Function( Row )
        constexpr AnnotationField<ParsedTypeInfo> kArrReflectField[] = {
#include "PredefinedAnnotationField.xxx"
        };
#undef SW_ANNOTATION_IN_Reflect
#undef SW_ANNOTATION_IN_Enum

        // ── ENUM ──
#define SW_ANNOTATION_IN_Reflect( Row )
#define SW_ANNOTATION_IN_Enum( Row ) Row
        constexpr AnnotationField<ParsedEnumInfo> kArrEnumField[] = {
#include "PredefinedAnnotationField.xxx"
        };
#undef SW_ANNOTATION_IN_Enum
#undef SW_ANNOTATION_IN_Property

        // ── PROPERTY ──
#define SW_ANNOTATION_IN_Enum( Row )
#define SW_ANNOTATION_IN_Property( Row ) Row
        constexpr AnnotationField<ParsedPropertyInfo> kArrPropertyField[] = {
#include "PredefinedAnnotationField.xxx"
        };
#undef SW_ANNOTATION_IN_Property
#undef SW_ANNOTATION_IN_Function

        // ── FUNCTION ──
#define SW_ANNOTATION_IN_Property( Row )
#define SW_ANNOTATION_IN_Function( Row ) Row
        constexpr AnnotationField<ParsedFunctionInfo> kArrFunctionField[] = {
#include "PredefinedAnnotationField.xxx"
        };

#undef SW_ANNOTATION_IN_Reflect
#undef SW_ANNOTATION_IN_Enum
#undef SW_ANNOTATION_IN_Property
#undef SW_ANNOTATION_IN_Function
#undef REGISTER_ANNOTATION_FIELD
#undef REGISTER_ANNOTATION_FIELD_FN
#undef SW_ANNOTATION_ROW
#undef SW_ANNOTATION_OUTPUT_Editor
#undef SW_ANNOTATION_OUTPUT_Runtime
#undef SW_ANNOTATION_OUTPUT_Manual
#undef SW_ANNOTATION_TARGET_Reflect
#undef SW_ANNOTATION_TARGET_Enum
#undef SW_ANNOTATION_TARGET_Property
#undef SW_ANNOTATION_TARGET_Function

        constexpr AnnotationScope<ParsedTypeInfo> kReflectScope{
            &annotationConstants::kReflect, kArrReflectField, static_cast<uint32>( std::size( kArrReflectField ) ) };
        constexpr AnnotationScope<ParsedEnumInfo> kEnumScope{
            &annotationConstants::kEnum, kArrEnumField, static_cast<uint32>( std::size( kArrEnumField ) ) };
        constexpr AnnotationScope<ParsedPropertyInfo> kPropertyScope{
            &annotationConstants::kProperty, kArrPropertyField, static_cast<uint32>( std::size( kArrPropertyField ) ) };
        constexpr AnnotationScope<ParsedFunctionInfo> kFunctionScope{
            &annotationConstants::kFunction, kArrFunctionField, static_cast<uint32>( std::size( kArrFunctionField ) ) };
    } // namespace
} // namespace sw

namespace sw
{
    template <>
    const AnnotationScope<ParsedTypeInfo>& getAnnotationScope<ParsedTypeInfo>()
    {
        return kReflectScope;
    }

    template <>
    const AnnotationScope<ParsedEnumInfo>& getAnnotationScope<ParsedEnumInfo>()
    {
        return kEnumScope;
    }

    template <>
    const AnnotationScope<ParsedPropertyInfo>& getAnnotationScope<ParsedPropertyInfo>()
    {
        return kPropertyScope;
    }

    template <>
    const AnnotationScope<ParsedFunctionInfo>& getAnnotationScope<ParsedFunctionInfo>()
    {
        return kFunctionScope;
    }

    template <typename TParsed>
    void AnnotationFields::emitMetadata( CodeEmit& emit, const TParsed& parsed, const string_view prefix )
    {
        const AnnotationScope<TParsed>& scope = getAnnotationScope<TParsed>();

        bool bHasEditorField = false;
        for ( const AnnotationField<TParsed>& field : scope )
        {
            if ( field._emit == AnnotationEmit::Editor && field._pIsSet( parsed ) )
            {
                bHasEditorField = true;
                break;
            }
        }

        if ( bHasEditorField )
        {
            emit.line( "#if !defined( SW_SHIPPING )" );
            for ( const AnnotationField<TParsed>& field : scope )
            {
                if ( field._emit == AnnotationEmit::Editor )
                    field._pEmit( emit, prefix, parsed );
            }
            emit.line( "#endif" );
        }

        for ( const AnnotationField<TParsed>& field : scope )
        {
            if ( field._emit == AnnotationEmit::Runtime )
                field._pEmit( emit, prefix, parsed );
        }
    }

    template void AnnotationFields::emitMetadata<ParsedTypeInfo>( CodeEmit&, const ParsedTypeInfo&, string_view );
    template void AnnotationFields::emitMetadata<ParsedEnumInfo>( CodeEmit&, const ParsedEnumInfo&, string_view );
    template void AnnotationFields::emitMetadata<ParsedPropertyInfo>( CodeEmit&, const ParsedPropertyInfo&, string_view );
    template void AnnotationFields::emitMetadata<ParsedFunctionInfo>( CodeEmit&, const ParsedFunctionInfo&, string_view );

    bool AnnotationFields::validateBindings( const AnnotationMeta& meta )
    {
        bool bValid = true;
        for ( const AnnotationMetaEntry& entry : meta.getEntries() )
        {
            if ( AnnotationFieldsInternal::isInScope<ParsedTypeInfo>( entry ) )
            {
                bValid = AnnotationFieldsInternal::isBindingValid<ParsedTypeInfo>( entry ) && bValid;
            }
            else if ( AnnotationFieldsInternal::isInScope<ParsedEnumInfo>( entry ) )
            {
                bValid = AnnotationFieldsInternal::isBindingValid<ParsedEnumInfo>( entry ) && bValid;
            }
            else if ( AnnotationFieldsInternal::isInScope<ParsedPropertyInfo>( entry ) )
            {
                bValid = AnnotationFieldsInternal::isBindingValid<ParsedPropertyInfo>( entry ) && bValid;
            }
            else if ( AnnotationFieldsInternal::isInScope<ParsedFunctionInfo>( entry ) )
            {
                bValid = AnnotationFieldsInternal::isBindingValid<ParsedFunctionInfo>( entry ) && bValid;
            }
            else
            {
                SW_LOG_ERROR( "AnnotationMeta.txt section [%#] is not an annotation scope (REFLECT / ENUM / PROPERTY / FUNCTION).",
                              entry._scope );
                bValid = false;
            }
        }

        bValid = AnnotationFieldsInternal::isEveryFieldBound<ParsedTypeInfo>( meta ) && bValid;
        bValid = AnnotationFieldsInternal::isEveryFieldBound<ParsedEnumInfo>( meta ) && bValid;
        bValid = AnnotationFieldsInternal::isEveryFieldBound<ParsedPropertyInfo>( meta ) && bValid;
        bValid = AnnotationFieldsInternal::isEveryFieldBound<ParsedFunctionInfo>( meta ) && bValid;
        return bValid;
    }
} // namespace sw
