#include "pch.h"

#include "ReflectionParser/AnnotationApply.h"

#include "Core/Common/Types.h"
#include "Core/String/StringUtil.h"

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
            static string_view parseAnnotationStringValue( string_view token, size_t eqPos )
            {
                size_t valueStart = eqPos + 1;
                while ( valueStart < token.size() && ( token[valueStart] == ' ' || token[valueStart] == '\t' ) )
                    ++valueStart;

                if ( valueStart >= token.size() )
                    return {};

                if ( token[valueStart] == '"' )
                {
                    const size_t endQuote = token.find( '"', valueStart + 1 );
                    if ( endQuote == string_view::npos )
                        return {};
                    return token.substr( valueStart + 1, endQuote - valueStart - 1 );
                }

                size_t valueEnd = valueStart;
                while ( valueEnd < token.size() )
                {
                    const utf8 character = token[valueEnd];
                    if ( character == ' ' || character == '\t' )
                        break;
                    ++valueEnd;
                }
                return token.substr( valueStart, valueEnd - valueStart );
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
            args.remove_suffix( 1 );
        while ( args.empty() == false && ( args.front() == '(' || args.front() == ';' ) )
            args.remove_prefix( 1 );

        vector<string> listToken;
        bool           bInQuote   = false;
        size_t         tokenStart = 0;

        for ( size_t charIndex = 0; charIndex < args.size(); ++charIndex )
        {
            const utf8 character = args[charIndex];
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
     *          필드명으로 바꾸고, 그 이름의 줄이 값을 넣습니다. 예전에는 스코프마다 이 루프가 한 벌씩 있었고 서로 조금씩
     *          달랐습니다(`X = false` 를 PROPERTY 만 받고 나머지는 버렸다).
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
            string_view              value;
            if ( bBare )
            {
                pBinding = meta.findBare( scope._pDesc->_pScope, token );
            }
            else
            {
                pBinding = meta.findKey( scope._pDesc->_pScope, StringUtil::trim( string_view( token.data(), eqPos ) ) );
                value    = AnnotationApplyInternal::parseAnnotationStringValue( token, eqPos );
            }

            if ( pBinding == nullptr )
            {
                outListUnknownToken.push_back( token );
                continue;
            }

            // 넷 역할은 토큰 자체가 값이다 — `FUNCTION( Server )` 는 NetRole 필드에 "Server" 를 넣는다.
            string_view fieldId = pBinding->_field;
            if ( pBinding->_kind == AnnotationBinding::Kind::NetRole )
            {
                fieldId = annotationConstants::kNetRoleField;
                value   = pBinding->_field;
            }

            // 바인딩이 가리키는 줄은 파서가 시작할 때 `AnnotationFields::validateBindings` 가 보장한다.
            const AnnotationField<TParsed>* pField = scope.findField( fieldId );
            if ( pField == nullptr )
                continue;
            // 숫자를 받는 줄에 숫자가 아닌 값(`Min = 0.5f` · `Max = ten`)은 거절한다. 예전에는 변환 실패를 무시해 조용히 0 이 됐다.
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
