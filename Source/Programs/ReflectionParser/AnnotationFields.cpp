#include "pch.h"

#include "Programs/ReflectionParser/AnnotationFields.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

#include "Engine/Reflection/ReflectionEnumNames.h"

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
                else if constexpr ( std::is_same_v<TMember, ParsedRangeExpr> )
                    return AnnotationValue::Range;
                else if constexpr ( std::is_same_v<TMember, uint32> )
                    return AnnotationValue::Count;
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
                    bool bFlag = true; // 값을 적지 않은 어노테이션(`Flag`)은 켠다는 뜻이다 — 읽을 수 없는 글도 같은 규칙(도구 입력이라 오류로 멈추지 않는다)
                    if ( StringUtil::tryParseBool( value, bFlag ) == false )
                        bFlag = true;
                    return static_cast<uint8>( bFlag ? SW_TRUE : SW_FALSE );
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
            /** @brief 개발 전용 단위(`<이름>.gen.meta.cpp`)에 쓰는 에디터 메타데이터 줄인지 봅니다(Display · Editor). */
            static constexpr bool isEditorBlock( const AnnotationEmit emit ) { return emit == AnnotationEmit::Display || emit == AnnotationEmit::Editor; }

            /** @brief 멤버 이름(`_bReadOnly` · `_tooltip`)에서 접두어를 떼어 엔진 쪽 상수 이름(`ReadOnly` · `Tooltip`)을 만듭니다. */
            static string makeOutputName( const string_view member, const string_view memberPrefix )
            {
                string name{ member.substr( memberPrefix.size() ) };
                if ( name.empty() == false && 'a' <= name.front() && name.front() <= 'z' )
                    name.front() = static_cast<utf8>( name.front() - 'a' + 'A' );
                return name;
            }

            static void collectValue( AnnotationOutput& outOutput, const string_view member, const uint8 value )
            {
                if ( value == SW_TRUE )
                    outOutput._listFlag.push_back( makeOutputName( member, "_b" ) );
            }

            static void collectValue( AnnotationOutput& outOutput, const string_view member, const string& value )
            {
                if ( value.empty() == false )
                    outOutput._listText.emplace_back( makeOutputName( member, "_" ), value );
            }

            static void collectValue( AnnotationOutput& outOutput, const string_view, const vector<pair<string, string>>& listPair )
            {
                outOutput._listCustomMeta.insert( outOutput._listCustomMeta.end(), listPair.begin(), listPair.end() );
            }

            static void collectValue( AnnotationOutput& outOutput, const string_view, const FunctionNetRole role ) { outOutput._netRole = role; }

            // ------------------------------------------------------------------------------
            // C) 손으로 넣는 줄(`REGISTER_ANNOTATION_FIELD_FN`)의 Fn
            // ------------------------------------------------------------------------------
            /** @brief 쉼표 · 세미콜론으로 나눈 이름들을 @p outList 에 붙입니다. */
            static void appendNameList( vector<string>& outList, const string_view value )
            {
                const string_splitter parts( value, { ",", ";" } );
                for ( const string_view token : parts.getSplitList() )
                {
                    const string_view trimmed = StringUtil::trim( token );
                    if ( trimmed.empty() == false )
                        outList.emplace_back( trimmed );
                }
            }

            /** @brief 쉼표 · 세미콜론으로 나눈 별칭(이름을 바꾸기 전의 이름)들을 붙입니다. `Alias = "OldA, OldB"`. */
            template <typename TParsed>
            static void appendAlias( TParsed& target, const string_view value )
            {
                appendNameList( target._listAlias, value );
            }

            /** @brief 지운 필드 이름입니다. `RemovedField = "_oldA, _oldB"`. */
            static void appendRemovedField( ParsedTypeInfo& target, const string_view value )
            {
                appendNameList( target._listRemovedField, value );
            }

            /** @brief 지운 타입 이름입니다. `RemovedType = "sw::OldThing"`. */
            static void appendRemovedType( ParsedTypeInfo& target, const string_view value )
            {
                appendNameList( target._listRemovedType, value );
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

            /**
             * @brief `Old:Target` 목록을 (옛 이름, 대상 열거자) 쌍으로 붙입니다. 꼴이 틀린 항목도 빈 칸을 남겨 그대로 붙입니다.
             * @details 여기서 버리면 선언한 리다이렉트가 말없이 사라집니다. 꼴과 대상은 `DeclarationValidator` 가 열거자 목록과 함께 검사해 그 헤더를 실패로 끝냅니다.
             */
            static void appendEnumeratorRedirect( vector<pair<string, string>>& outListRedirect, const string_view value )
            {
                const string_splitter parts( value, { ",", ";" } );
                for ( const string_view tokenView : parts.getSplitList() )
                {
                    const string_view token = StringUtil::trim( tokenView );
                    if ( token.empty() )
                        continue;
                    const size_t colon = token.find( ':' );
                    if ( colon == string_view::npos )
                    {
                        outListRedirect.emplace_back( string( token ), string() );
                        continue;
                    }
                    outListRedirect.emplace_back( string( StringUtil::trim( token.substr( 0, colon ) ) ), string( StringUtil::trim( token.substr( colon + 1 ) ) ) );
                }
            }

            /** @brief 이름을 바꾼 열거자입니다. `ValueAlias = "OldIdle:Idle"`. */
            static void appendValueAlias( ParsedEnumInfo& target, const string_view value )
            {
                appendEnumeratorRedirect( target._listValueAlias, value );
            }

            /** @brief 지운 열거자와 대신 읽을 열거자입니다. `Removed = "OldRunning:Moving"`. */
            static void appendRemovedEnumerator( ParsedEnumInfo& target, const string_view value )
            {
                appendEnumeratorRedirect( target._listRemovedEnumerator, value );
            }

            /** @brief 에셋 타입을 적으면 그 프로퍼티는 에셋 경로이기도 합니다. */
            static void applyAssetType( ParsedPropertyInfo& target, const string_view value )
            {
                target._assetType  = string( value );
                target._bAssetPath = SW_TRUE;
            }

            /** @brief 받은 값의 알림 함수를 적으면 복제 대상이기도 합니다. */
            static void applyRepNotify( ParsedPropertyInfo& target, const string_view value )
            {
                target._repNotify   = string( value );
                target._bReplicated = SW_TRUE;
            }

            /**
             * @brief 아래 경계만 적습니다. 위 경계는 따로이고, 한쪽만 적은 범위는 그쪽만 막습니다. 꼴이 틀리면 `AnnotationApply` 가 이미 거절했습니다.
             * @details 글은 늘 남깁니다(고정 배치형 코덱이 읽습니다). 숫자 하나면 여기서 숫자 경계가 됩니다. 같은 타입의 필드 하나면 생성기가
             *          그 필드를 읽는 함수를 냅니다(`CodeGenerator` 의 `emitRangeReader`). 바깥 타입의 필드 · 목록 꼴은 코덱만 씁니다.
             */
            static void applyMinRange( ParsedPropertyInfo& target, const string_view value )
            {
                target._minRangeExpr._text = string( StringUtil::trim( value ) );
                float32 parsed{ 0.0f };
                if ( StringUtil::parseFloat( value, parsed ) == false )
                    return;
                target._minRange     = parsed;
                target._bHasMinRange = SW_TRUE;
            }

            static void applyMaxRange( ParsedPropertyInfo& target, const string_view value )
            {
                target._maxRangeExpr._text = string( StringUtil::trim( value ) );
                float32 parsed{ 0.0f };
                if ( StringUtil::parseFloat( value, parsed ) == false )
                    return;
                target._maxRange     = parsed;
                target._bHasMaxRange = SW_TRUE;
            }

            /** @brief 고정 배치형 코덱의 양자화 간격입니다. 숫자인지는 `AnnotationApply` 가 이미 봤습니다. */
            static void applyStep( ParsedPropertyInfo& target, const string_view value )
            {
                float32 parsed{ 0.0f };
                if ( StringUtil::parseFloat( value, parsed ) == false )
                    return;
                target._step     = parsed;
                target._bHasStep = SW_TRUE;
            }

            /**
             * @brief 문자열 · 컨테이너 최대 길이입니다. 꼴(1 이상의 32 비트 정수 또는 상수 이름)은 `AnnotationApply` 가 이미 봤습니다.
             * @details 상수 이름이면 이름만 남깁니다. 값은 선언을 아는 `PropertyResolver` 가 그 상수를 찾아 채웁니다(`_maxCountName`).
             */
            static void applyMaxCount( ParsedPropertyInfo& target, const string_view value )
            {
                const string_view trimmed = StringUtil::trim( value );
                uint64            parsed  = 0;
                if ( StringUtil::parseUint64( trimmed, parsed ) == false )
                {
                    target._maxCountName = string( trimmed );
                    return;
                }
                target._maxCount     = static_cast<uint32>( parsed );
                target._bHasMaxCount = SW_TRUE;
            }

            /** @brief 타입의 저장 판입니다(`REFLECT( Version = 3 )`). 꼴(1 이상의 32 비트 정수 또는 상수 이름)은 `AnnotationApply` 가 이미 봤습니다. 이름이면 이름만 남깁니다. */
            static void applyTypeVersion( ParsedTypeInfo& target, const string_view value )
            {
                const string_view trimmed = StringUtil::trim( value );
                uint64            parsed  = 0;
                if ( StringUtil::parseUint64( trimmed, parsed ) == false )
                {
                    target._versionName = string( trimmed );
                    return;
                }
                target._version = static_cast<uint32>( parsed );
            }

            /** @brief 슬라이더 아래 경계입니다(허용 범위 `Min` 과 따로). */
            static void applyUIMinRange( ParsedPropertyInfo& target, const string_view value )
            {
                float32 parsed{ 0.0f };
                if ( StringUtil::parseFloat( value, parsed ) == false )
                    return;
                target._uiMinRange     = parsed;
                target._bHasUIMinRange = SW_TRUE;
            }

            static void applyUIMaxRange( ParsedPropertyInfo& target, const string_view value )
            {
                float32 parsed{ 0.0f };
                if ( StringUtil::parseFloat( value, parsed ) == false )
                    return;
                target._uiMaxRange     = parsed;
                target._bHasUIMaxRange = SW_TRUE;
            }

            /** @brief 원격 호출의 함수별 초당 상한입니다. 꼴(1 이상의 32 비트 정수 또는 상수 이름)은 `AnnotationApply` 가 이미 봤습니다. 이름이면 이름만 남깁니다. */
            static void applyMaxCallsPerSecond( ParsedFunctionInfo& target, const string_view value )
            {
                const string_view trimmed = StringUtil::trim( value );
                uint64            parsed  = 0;
                if ( StringUtil::parseUint64( trimmed, parsed ) == false )
                {
                    target._maxCallsPerSecondName = string( trimmed );
                    return;
                }
                target._maxCallsPerSecond = static_cast<uint32>( parsed );
            }

            /** @brief 넷 역할은 토큰 자체가 값입니다(`FUNCTION( Server )` → "Server"). */
            static void applyNetRole( ParsedFunctionInfo& target, const string_view value )
            {
                FunctionNetRole role = FunctionNetRole::Local;
                if ( tryParseFunctionNetRole( value, role ) )
                    target._netRole = role;
            }
        };

        // ------------------------------------------------------------------------------
        // 필드 표 — PredefinedAnnotationField.xxx 를 스코프마다 한 번씩 전개한다.
        //
        // 플래그 멤버가 `uint8 : 1` 비트필드라 멤버 포인터를 만들 수 없다. 그래서 줄마다 대입 · 조회 · 출력 코드를 매크로가
        // 만든다. 표가 구조체 밖에 있는 까닭: 정적 constexpr 멤버의 초기화식에서는 같은 구조체의 constexpr 함수
        // (`getValueKind`)를 아직 부를 수 없다(구조체가 완성되기 전이다).
        // ------------------------------------------------------------------------------
#define SW_ANNOTATION_TARGET_Reflect    ParsedTypeInfo
#define SW_ANNOTATION_TARGET_Enum       ParsedEnumInfo
#define SW_ANNOTATION_TARGET_Property   ParsedPropertyInfo
#define SW_ANNOTATION_TARGET_Function   ParsedFunctionInfo
#define SW_ANNOTATION_TARGET_Enumerator ParsedEnumeratorInfo

#define SW_ANNOTATION_OUTPUT_Editor( TParsed, Member ) \
    []( AnnotationOutput& outOutput, const TParsed& parsed ) { AnnotationFieldsInternal::collectValue( outOutput, #Member, parsed.Member ); }
#define SW_ANNOTATION_OUTPUT_Runtime                   SW_ANNOTATION_OUTPUT_Editor
#define SW_ANNOTATION_OUTPUT_Display                   SW_ANNOTATION_OUTPUT_Editor
#define SW_ANNOTATION_OUTPUT_Manual( TParsed, Member ) nullptr

#define SW_ANNOTATION_ROW( TParsed, ID, Member, ApplyStatement, Emit )                                                   \
    { #ID, []( TParsed& target, string_view value ) { ApplyStatement; }, SW_ANNOTATION_OUTPUT_##Emit( TParsed, Member ), \
      AnnotationFieldsInternal::getValueKind<decltype( TParsed::Member )>(), AnnotationEmit::Emit },

#define REGISTER_ANNOTATION_FIELD( Scope, ID, Member, Emit )                                                                                 \
    SW_ANNOTATION_IN_##Scope( SW_ANNOTATION_ROW( SW_ANNOTATION_TARGET_##Scope, ID, Member,                                                   \
                                                 target.Member = AnnotationFieldsInternal::convertValue<decltype( target.Member )>( value ), \
                                                 Emit ) )
#define REGISTER_ANNOTATION_FIELD_FN( Scope, ID, Member, Fn, Emit ) \
    SW_ANNOTATION_IN_##Scope( SW_ANNOTATION_ROW( SW_ANNOTATION_TARGET_##Scope, ID, Member, AnnotationFieldsInternal::Fn( target, value ), Emit ) )

        // ── REFLECT ──
#define SW_ANNOTATION_IN_Enumerator( Row )
#define SW_ANNOTATION_IN_Reflect( Row ) Row
#define SW_ANNOTATION_IN_Enum( Row )
#define SW_ANNOTATION_IN_Property( Row )
#define SW_ANNOTATION_IN_Function( Row )
        constexpr AnnotationField<ParsedTypeInfo> kArrReflectField[] = {
#include "Core/Predefined/PredefinedAnnotationField.xxx"
        };
#undef SW_ANNOTATION_IN_Reflect
#undef SW_ANNOTATION_IN_Enum

        // ── ENUM ──
#define SW_ANNOTATION_IN_Reflect( Row )
#define SW_ANNOTATION_IN_Enum( Row ) Row
        constexpr AnnotationField<ParsedEnumInfo> kArrEnumField[] = {
#include "Core/Predefined/PredefinedAnnotationField.xxx"
        };
#undef SW_ANNOTATION_IN_Enum
#undef SW_ANNOTATION_IN_Property

        // ── PROPERTY ──
#define SW_ANNOTATION_IN_Enum( Row )
#define SW_ANNOTATION_IN_Property( Row ) Row
        constexpr AnnotationField<ParsedPropertyInfo> kArrPropertyField[] = {
#include "Core/Predefined/PredefinedAnnotationField.xxx"
        };
#undef SW_ANNOTATION_IN_Property
#undef SW_ANNOTATION_IN_Function

        // ── FUNCTION ──
#define SW_ANNOTATION_IN_Property( Row )
#define SW_ANNOTATION_IN_Function( Row ) Row
        constexpr AnnotationField<ParsedFunctionInfo> kArrFunctionField[] = {
#include "Core/Predefined/PredefinedAnnotationField.xxx"
        };
#undef SW_ANNOTATION_IN_Function
#undef SW_ANNOTATION_IN_Enumerator

        // ── ENUMERATOR ──
#define SW_ANNOTATION_IN_Function( Row )
#define SW_ANNOTATION_IN_Enumerator( Row ) Row
        constexpr AnnotationField<ParsedEnumeratorInfo> kArrEnumeratorField[] = {
#include "Core/Predefined/PredefinedAnnotationField.xxx"
        };

#undef SW_ANNOTATION_IN_Reflect
#undef SW_ANNOTATION_IN_Enum
#undef SW_ANNOTATION_IN_Property
#undef SW_ANNOTATION_IN_Function
#undef SW_ANNOTATION_IN_Enumerator
#undef REGISTER_ANNOTATION_FIELD
#undef REGISTER_ANNOTATION_FIELD_FN
#undef SW_ANNOTATION_ROW
#undef SW_ANNOTATION_OUTPUT_Editor
#undef SW_ANNOTATION_OUTPUT_Runtime
#undef SW_ANNOTATION_OUTPUT_Display
#undef SW_ANNOTATION_OUTPUT_Manual
#undef SW_ANNOTATION_TARGET_Reflect
#undef SW_ANNOTATION_TARGET_Enum
#undef SW_ANNOTATION_TARGET_Property
#undef SW_ANNOTATION_TARGET_Function
#undef SW_ANNOTATION_TARGET_Enumerator

        /** @brief 표의 Runtime 줄이 모두 @p arrKind 의 값 종류인지 봅니다. 생성 코드의 상수 표가 실을 칸이 없는 값은 조용히 빠지지 않고 여기서 멈춘다. */
        template <typename TParsed, size_t N, size_t KindCount>
        constexpr bool hasOnlyRuntimeKinds( const AnnotationField<TParsed> ( &arrField )[N], const AnnotationValue ( &arrKind )[KindCount] )
        {
            for ( const AnnotationField<TParsed>& field : arrField )
            {
                bool bCarried = field._emit != AnnotationEmit::Runtime;
                for ( const AnnotationValue kind : arrKind )
                {
                    bCarried = bCarried || field._value == kind;
                }
                if ( bCarried == false )
                    return false;
            }
            return true;
        }

        /** @brief 표에 Runtime 줄이 없는지 봅니다 — 타입 · 열거형 · 열거자의 서술자는 애노테이션 표에서 오는 런타임 칸이 없다. */
        template <typename TParsed, size_t N>
        constexpr bool hasNoRuntimeField( const AnnotationField<TParsed> ( &arrField )[N] )
        {
            for ( const AnnotationField<TParsed>& field : arrField )
            {
                if ( field._emit == AnnotationEmit::Runtime )
                    return false;
            }
            return true;
        }

        constexpr AnnotationValue kArrPropertyRuntimeKind[] = { AnnotationValue::Bool, AnnotationValue::String };
        constexpr AnnotationValue kArrFunctionRuntimeKind[] = { AnnotationValue::Bool, AnnotationValue::NetRole };
        static_assert( hasOnlyRuntimeKinds( kArrPropertyField, kArrPropertyRuntimeKind ), "PropertyMetadataDesc carries flags and texts only - add a column before adding a Runtime PROPERTY row" );
        static_assert( hasOnlyRuntimeKinds( kArrFunctionField, kArrFunctionRuntimeKind ), "FunctionDesc carries flags and a net role only - add a column before adding a Runtime FUNCTION row" );
        static_assert( hasNoRuntimeField( kArrReflectField ) && hasNoRuntimeField( kArrEnumField ) && hasNoRuntimeField( kArrEnumeratorField ),
                       "REFLECT, ENUM and ENUMERATOR rows are Display, Editor or Manual - the type and enum descriptors have no runtime annotation column" );

        constexpr AnnotationScope<ParsedTypeInfo> kReflectScope{
            &annotation::kReflect, kArrReflectField, static_cast<uint32>( SW_COUNT_OF( kArrReflectField ) ) };
        constexpr AnnotationScope<ParsedEnumInfo> kEnumScope{
            &annotation::kEnum, kArrEnumField, static_cast<uint32>( SW_COUNT_OF( kArrEnumField ) ) };
        constexpr AnnotationScope<ParsedPropertyInfo> kPropertyScope{
            &annotation::kProperty, kArrPropertyField, static_cast<uint32>( SW_COUNT_OF( kArrPropertyField ) ) };
        constexpr AnnotationScope<ParsedFunctionInfo> kFunctionScope{
            &annotation::kFunction, kArrFunctionField, static_cast<uint32>( SW_COUNT_OF( kArrFunctionField ) ) };
        constexpr AnnotationScope<ParsedEnumeratorInfo> kEnumeratorScope{
            &annotation::kEnumerator, kArrEnumeratorField, static_cast<uint32>( SW_COUNT_OF( kArrEnumeratorField ) ) };
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

    template <>
    const AnnotationScope<ParsedEnumeratorInfo>& getAnnotationScope<ParsedEnumeratorInfo>()
    {
        return kEnumeratorScope;
    }

    template <typename TParsed>
    AnnotationOutput AnnotationOutput::collectRuntime( const TParsed& parsed )
    {
        AnnotationOutput output;
        for ( const AnnotationField<TParsed>& field : getAnnotationScope<TParsed>() )
        {
            if ( field._emit == AnnotationEmit::Runtime )
                field._pCollect( output, parsed );
        }
        return output;
    }

    template <typename TParsed>
    AnnotationOutput AnnotationOutput::collectEditor( const TParsed& parsed )
    {
        AnnotationOutput output;
        for ( const AnnotationField<TParsed>& field : getAnnotationScope<TParsed>() )
        {
            if ( AnnotationFieldsInternal::isEditorBlock( field._emit ) )
                field._pCollect( output, parsed );
        }
        return output;
    }

    template AnnotationOutput AnnotationOutput::collectRuntime<ParsedPropertyInfo>( const ParsedPropertyInfo& );
    template AnnotationOutput AnnotationOutput::collectRuntime<ParsedFunctionInfo>( const ParsedFunctionInfo& );
    template AnnotationOutput AnnotationOutput::collectEditor<ParsedTypeInfo>( const ParsedTypeInfo& );
    template AnnotationOutput AnnotationOutput::collectEditor<ParsedEnumInfo>( const ParsedEnumInfo& );
    template AnnotationOutput AnnotationOutput::collectEditor<ParsedPropertyInfo>( const ParsedPropertyInfo& );
    template AnnotationOutput AnnotationOutput::collectEditor<ParsedFunctionInfo>( const ParsedFunctionInfo& );
    template AnnotationOutput AnnotationOutput::collectEditor<ParsedEnumeratorInfo>( const ParsedEnumeratorInfo& );

    bool AnnotationFields::canAcceptOnEvent( const string_view propertyFieldID )
    {
        if ( propertyFieldID == "Name" )
            return true;
        const AnnotationField<ParsedPropertyInfo>* pField = kPropertyScope.findField( propertyFieldID );
        return pField != nullptr && pField->_emit == AnnotationEmit::Display;
    }

    string AnnotationFields::makeEventFieldList()
    {
        StringBuilder<constant::buffer::kMax256> text;
        for ( const AnnotationField<ParsedPropertyInfo>& field : kPropertyScope )
        {
            if ( field._emit != AnnotationEmit::Display )
                continue;
            text.append( string_view( field._id.data(), field._id.size() ) );
            text.append( ", " );
        }
        text.append( "Name" );
        return string( text.view() );
    }
} // namespace sw
