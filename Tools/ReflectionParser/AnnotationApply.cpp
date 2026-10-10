#include "pch.h"

#include "ReflectionParser/AnnotationApply.h"

#include "Core/Common/Types.h"
#include "Core/Container/StringUtil.h"

#include "ReflectionParser/AnnotationFields.h"
#include "ReflectionParser/AnnotationMeta.h"
#include "ReflectionParser/ParsedReflection.h"
#include "ReflectionParser/ParserDefines.h"

namespace sw
{
    namespace
    {
        struct AnnotationApplyInternal
        {
            /**
             * @brief `key="value"` 또는 `key=value` 형태의 단일 토큰에서 따옴표를 고려하여 값 문자열을 추출합니다.
             */
            static string parseAnnotationStringValue( string_view token, size_t eqPos )
            {
                string value; // 반환은 모두 이 객체다(복사 없이 돌려준다)
                size_t valueStart = eqPos + 1;
                while ( valueStart < token.size() && ( token[valueStart] == ' ' || token[valueStart] == '\t' ) )
                {
                    ++valueStart;
                }

                if ( valueStart >= token.size() )
                    return value;

                if ( token[valueStart] == '"' )
                {
                    // 따옴표 안의 이스케이프를 푼다(`\"` · `\\` · `\n` · `\t` · `\r`) — 첫 안쪽 따옴표에서 값을 끝내면
                    // `Tooltip = "Say \"hi\""` 가 `Say \` 로 조용히 잘린다. 생성기는 값을 다시 C++ 문자열로 이스케이프한다(`CodeEmit::escapeCppString`).
                    for ( size_t charIndex = valueStart + 1; charIndex < token.size(); ++charIndex )
                    {
                        const utf8 character = token[charIndex];
                        if ( character == '"' )
                            return value;
                        if ( character == '\\' && charIndex + 1 < token.size() )
                        {
                            const utf8 escaped = token[++charIndex];
                            value.push_back( escaped == 'n' ? '\n' : escaped == 't' ? '\t'
                                                                 : escaped == 'r'   ? '\r'
                                                                                    : escaped );
                            continue;
                        }
                        value.push_back( character );
                    }
                    value.clear(); // 닫는 따옴표가 없다
                    return value;
                }

                size_t valueEnd = valueStart;
                while ( valueEnd < token.size() )
                {
                    const utf8 character = token[valueEnd];
                    if ( character == ' ' || character == '\t' )
                        break;
                    ++valueEnd;
                }
                value.assign( token.data() + valueStart, valueEnd - valueStart );
                return value;
            }

            /**
             * @brief 따옴표 없는 값이 공백으로 갈라졌는지 봅니다(`Units = m / s`).
             * @details 따옴표 없는 값은 첫 공백에서 끝나므로 뒷조각이 조용히 버려진다 — clang-format 은 `m/s` 를 `m / s` 로 띄우고, 그러면 `m` 이 단위 표를 통과한다.
             */
            static bool isUnquotedValueSplit( const string_view token, const size_t eqPos ) noexcept
            {
                const string_view value = StringUtil::trim( token.substr( eqPos + 1 ) );
                if ( value.empty() || value.front() == '"' )
                    return false;
                return value.find_first_of( " \t" ) != string_view::npos;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string_view AnnotationApply::annotationArgumentText( string_view spelling, string_view prefix )
    {
        const size_t pos = spelling.find( prefix );
        if ( pos == string_view::npos )
            return {};
        return spelling.substr( pos + prefix.size() );
    }

    vector<string> AnnotationApply::splitAnnotationArgs( string_view args )
    {
        while ( args.empty() == false && ( args.back() == ')' || args.back() == ';' ) )
        {
            args.remove_suffix( 1 );
        }
        while ( args.empty() == false && ( args.front() == '(' || args.front() == ';' ) )
        {
            args.remove_prefix( 1 );
        }

        vector<string> listToken;
        bool           bInQuote   = false;
        size_t         tokenStart = 0;

        for ( size_t charIndex = 0; charIndex < args.size(); ++charIndex )
        {
            const utf8 character = args[charIndex];
            // 따옴표 안의 `\"` 는 따옴표를 닫지 않는다 — 닫는 것으로 세면 그 뒤의 쉼표에서 토큰이 갈라진다(뒷조각은 "모르는 토큰").
            if ( character == '\\' && bInQuote )
            {
                ++charIndex;
                continue;
            }
            if ( character == '"' )
            {
                bInQuote = ( bInQuote == false );
                continue;
            }
            if ( character == ',' && bInQuote == false )
            {
                if ( charIndex > tokenStart )
                {
                    string_view token = StringUtil::trim( args.substr( tokenStart, charIndex - tokenStart ) );
                    if ( token.empty() == false )
                        listToken.emplace_back( token );
                }
                tokenStart = charIndex + 1;
            }
        }
        if ( args.size() > tokenStart )
        {
            string_view token = StringUtil::trim( args.substr( tokenStart ) );
            if ( token.empty() == false )
                listToken.emplace_back( token );
        }
        return listToken;
    }

    /**
     * @details 토큰 하나는 두 꼴입니다 — 단독 토큰 `X`(플래그 · 넷 역할)와 `key = value`. 철자는 AnnotationMeta.txt 가 정규
     *          필드명으로 바꾸고, 그 이름의 줄이 값을 넣습니다. 네 스코프가 이 루프 하나를 씁니다(같은 철자가 스코프마다 다르게 동작하지 않게).
     */
    template <typename TParsed>
    void AnnotationApply::apply( const string_view annotationSpelling, TParsed& target, const AnnotationMeta& meta,
                                 vector<string>& outListUnknownToken )
    {
        const AnnotationScope<TParsed>& scope  = getAnnotationScope<TParsed>();
        const string_view               prefix = scope._pDesc->_pPrefix;
        const size_t                    begin  = annotationSpelling.find( prefix );
        if ( begin == string_view::npos )
            return;

        for ( const string& token : splitAnnotationArgs( annotationSpelling.substr( begin + prefix.size() ) ) )
        {
            const size_t             eqPos    = token.find( '=' );
            const bool               bBare    = ( eqPos == string::npos );
            const AnnotationBinding* pBinding = nullptr;
            string                   valueText; // 이스케이프를 푼 값 — `value` 가 이것을 본다
            string_view              value;
            if ( bBare )
            {
                pBinding = meta.findBare( scope._pDesc->_pScope, token );
            }
            else
            {
                pBinding  = meta.findKey( scope._pDesc->_pScope, StringUtil::trim( string_view( token.data(), eqPos ) ) );
                valueText = AnnotationApplyInternal::parseAnnotationStringValue( token, eqPos );
                value     = valueText;
            }

            if ( pBinding == nullptr )
            {
                outListUnknownToken.push_back( token );
                continue;
            }
            if ( bBare == false && AnnotationApplyInternal::isUnquotedValueSplit( token, eqPos ) )
            {
                outListUnknownToken.push_back( token + "  (value has spaces - quote it)" );
                continue;
            }

            // 넷 역할은 토큰 자체가 값이다 — `FUNCTION( Server )` 는 NetRole 필드에 "Server" 를 넣는다.
            string_view fieldID = pBinding->_field;
            if ( pBinding->_kind == AnnotationBinding::Kind::NetRole )
            {
                fieldID = annotation::kNetRoleField;
                value   = pBinding->_field;
            }

            // 바인딩이 가리키는 줄은 파서가 시작할 때 `AnnotationFields::validateBindings` 가 보장한다.
            const AnnotationField<TParsed>* pField = scope.findField( fieldID );
            if ( pField == nullptr )
                continue;
            // 숫자를 받는 줄에 숫자가 아닌 값(`Min = 0.5f` · `Max = ten`)은 거절한다(조용히 0 이 되지 않게).
            float32 number{ 0.0f };
            if ( pField->_value == AnnotationValue::Float && StringUtil::parseFloat( value, number ) == false )
            {
                outListUnknownToken.push_back( token + "  (value is not a number)" );
                continue;
            }
            pField->_pApply( target, value );
        }
    }

    template void AnnotationApply::apply<ParsedTypeInfo>( string_view, ParsedTypeInfo&, const AnnotationMeta&, vector<string>& );
    template void AnnotationApply::apply<ParsedEnumInfo>( string_view, ParsedEnumInfo&, const AnnotationMeta&, vector<string>& );
    template void AnnotationApply::apply<ParsedPropertyInfo>( string_view, ParsedPropertyInfo&, const AnnotationMeta&, vector<string>& );
    template void AnnotationApply::apply<ParsedFunctionInfo>( string_view, ParsedFunctionInfo&, const AnnotationMeta&, vector<string>& );
} // namespace sw
