#include "pch.h"

#include "Engine/Localization/LocText.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/TextFormatter.h"

namespace sw
{
    namespace
    {
        struct LocTextInternal
        {
            // 화면에 나오지 않는 제어 문자다. 메시지는 겹칠 수 있어(인자가 메시지) 여는 · 닫는 문자를 따로 둔다.
            static constexpr utf8 kBegin           = 0x02;
            static constexpr utf8 kEnd             = 0x03;
            static constexpr utf8 kSeparator       = 0x1F;
            static constexpr utf8 kKindMessage     = 'M';
            static constexpr utf8 kKindList        = 'L';
            static constexpr utf8 kKindLiteral     = 'T';
            static constexpr utf8 kArgumentText    = 't';
            static constexpr utf8 kArgumentKey     = 'k';
            static constexpr utf8 kArgumentInteger = 'i';
            static constexpr utf8 kArgumentDecimal = 'd';

            /** @brief 구분 문자를 뺀 글을 잇습니다. */
            static void appendPlain( string& inoutText, string_view value )
            {
                for ( const utf8 character : value )
                {
                    if ( character != kBegin && character != kEnd && character != kSeparator )
                        inoutText.push_back( character );
                }
            }

            /** @brief 다른 메시지 안에 넣을 글입니다 — 메시지면 그대로, 아니면 구분 문자를 뺀다. */
            static string makeNested( string_view text )
            {
                if ( LocalizedMessage::isMessage( text ) )
                    return string( text );
                string plain;
                appendPlain( plain, text );
                return plain;
            }

            /** @brief 겹친 메시지를 건너뛰며 구분 문자로 나눕니다. */
            static void split( string_view body, vector<string_view>& outListField )
            {
                uint32 depth = 0;
                size_t start = 0;
                for ( size_t index = 0; index < body.size(); ++index )
                {
                    const utf8 character = body[index];
                    if ( character == kBegin )
                        ++depth;
                    else if ( character == kEnd && depth > 0 )
                        --depth;
                    else if ( character == kSeparator && depth == 0 )
                    {
                        outListField.push_back( body.substr( start, index - start ) );
                        start = index + 1;
                    }
                }
                outListField.push_back( body.substr( start ) );
            }

            static void addArgument( const LocalizationManager* pLocalization, TextArgumentList& inoutArguments, string_view name, string_view value )
            {
                if ( value.empty() )
                    return;
                const utf8        kind = value.front();
                const string_view body = value.substr( 1 );
                if ( kind == kArgumentInteger )
                {
                    int64 number = 0;
                    if ( StringUtil::parseInt64( body, number ) )
                        inoutArguments.addInteger( name, number );
                    return;
                }
                if ( kind == kArgumentDecimal )
                {
                    const size_t colon  = body.find( ':' );
                    int32        digits = 0;
                    float64      number = 0.0;
                    if ( colon == string_view::npos || StringUtil::parseDouble( body.substr( colon + 1 ), number ) == false )
                        return;
                    if ( colon > 0 && StringUtil::parseInt( body.substr( 0, colon ), digits ) )
                        inoutArguments.addDecimal( name, number, static_cast<uint32>( digits ) );
                    else
                        inoutArguments.addDecimal( name, number );
                    return;
                }
                inoutArguments.addText( name, kind == kArgumentKey ? LocalizedText::resolve( pLocalization, body ) : string( body ) );
            }

            static string resolveMessage( const LocalizationManager* pLocalization, string_view text )
            {
                const utf8          kind = text[1];
                vector<string_view> listField;
                split( text.substr( 2, text.size() - 3 ), listField );
                if ( kind == kKindLiteral )
                    return string( listField[0] );
                if ( kind == kKindList )
                {
                    string joined;
                    for ( size_t index = 1; index < listField.size(); ++index )
                    {
                        if ( index > 1 )
                            joined.append( listField[0].data(), listField[0].size() );
                        joined.append( LocalizedText::resolve( pLocalization, listField[index] ) );
                    }
                    return joined;
                }
                TextArgumentList arguments;
                for ( size_t index = 1; index + 1 < listField.size(); index += 2 )
                {
                    addArgument( pLocalization, arguments, listField[index], listField[index + 1] );
                }
                const string_view key = listField[0];
                if ( pLocalization == nullptr )
                {
                    string formatted;
                    (void)TextFormatter::format( key, arguments, CultureInfo{}, formatted ); // 표가 없다 — 키를 보인다
                    return formatted;
                }
                const utf8* pPattern = pLocalization->findStringByText( key );
                if ( pPattern == nullptr )
                    pLocalization->reportMissingKey( key );
                if ( listField.size() == 1 )
                    return pPattern != nullptr ? string( pPattern ) : string( key ); // 인자가 없으면 패턴이 아니라 글이다(중괄호 · 따옴표가 그대로 보인다)
                return pLocalization->formatText( pPattern != nullptr ? string_view( pPattern ) : key, arguments );
            }
        };
    } // namespace

    const utf8* LocalizedText::find( string_view fullKey, const utf8* pSourceText )
    {
        if ( engine::areEngineServicesBound() == false )
            return pSourceText;
        const LocalizationManager& localization = engine::getSubsystem<LocalizationManager>();
        const utf8*                pFound       = localization.findStringByText( fullKey );
        if ( pFound != nullptr )
            return pFound;
        localization.reportMissingKey( fullKey );
        return pSourceText;
    }

    string LocalizedText::format( string_view fullKey, const utf8* pSourceText, const TextArgumentList& arguments )
    {
        const utf8* pPattern = find( fullKey, pSourceText );
        if ( engine::areEngineServicesBound() == false )
        {
            string text;
            (void)TextFormatter::format( pPattern != nullptr ? string_view( pPattern ) : string_view{}, arguments, CultureInfo{}, text );
            return text;
        }
        return engine::getSubsystem<LocalizationManager>().formatText( pPattern != nullptr ? string_view( pPattern ) : string_view{}, arguments );
    }

    string LocalizedText::resolve( const LocalizationManager* pLocalization, string_view text )
    {
        using Internal = LocTextInternal;
        if ( LocalizedMessage::isMessage( text ) )
            return Internal::resolveMessage( pLocalization, text );
        const utf8* pFound = pLocalization != nullptr && text.empty() == false ? pLocalization->findStringByText( text ) : nullptr;
        return pFound != nullptr ? string( pFound ) : string( text ); // 표에 없으면 글 그대로다(키가 아닌 글일 수 있다)
    }

    string LocalizedText::resolveNow( string_view text )
    {
        return resolve( engine::areEngineServicesBound() ? &engine::getSubsystem<LocalizationManager>() : nullptr, text );
    }

    LocalizedMessage::LocalizedMessage( string_view fullKey )
        : _body{}
    {
        _body.push_back( LocTextInternal::kBegin );
        _body.push_back( LocTextInternal::kKindMessage );
        LocTextInternal::appendPlain( _body, fullKey );
    }

    LocalizedMessage& LocalizedMessage::addText( string_view name, string_view value )
    {
        string plain;
        LocTextInternal::appendPlain( plain, value );
        return addValue( name, LocTextInternal::kArgumentText, plain );
    }

    LocalizedMessage& LocalizedMessage::addKey( string_view name, string_view keyOrText )
    {
        return addValue( name, LocTextInternal::kArgumentKey, LocTextInternal::makeNested( keyOrText ) );
    }

    LocalizedMessage& LocalizedMessage::addInteger( string_view name, int64 value )
    {
        return addValue( name, LocTextInternal::kArgumentInteger, to_string( value ) );
    }

    LocalizedMessage& LocalizedMessage::addDecimal( string_view name, float64 value, uint32 fractionDigits )
    {
        StringBuilder<constant::buffer::kMax64> number;
        number.appendFormat( "%#:%#", fractionDigits, Fmt( value, Format().precision( 9 ) ) );
        return addValue( name, LocTextInternal::kArgumentDecimal, number.view() );
    }

    LocalizedMessage& LocalizedMessage::addDecimal( string_view name, float64 value )
    {
        StringBuilder<constant::buffer::kMax64> number;
        number.appendFormat( ":%#", Fmt( value, Format().precision( 9 ) ) );
        return addValue( name, LocTextInternal::kArgumentDecimal, number.view() );
    }

    LocalizedMessage& LocalizedMessage::addValue( string_view name, utf8 kind, string_view value )
    {
        _body.push_back( LocTextInternal::kSeparator );
        LocTextInternal::appendPlain( _body, name );
        _body.push_back( LocTextInternal::kSeparator );
        _body.push_back( kind );
        _body.append( value.data(), value.size() );
        return *this;
    }

    string LocalizedMessage::str() const
    {
        string text = _body;
        text.push_back( LocTextInternal::kEnd );
        return text;
    }

    string LocalizedMessage::join( string_view separator, const vector<string>& listText )
    {
        string text;
        text.push_back( LocTextInternal::kBegin );
        text.push_back( LocTextInternal::kKindList );
        LocTextInternal::appendPlain( text, separator );
        size_t        partCount = 0;
        const string* pLast     = nullptr;
        for ( const string& part : listText )
        {
            if ( part.empty() )
                continue;
            ++partCount;
            pLast = &part;
            text.push_back( LocTextInternal::kSeparator );
            text.append( LocTextInternal::makeNested( part ) );
        }
        if ( partCount <= 1 )
            return pLast != nullptr ? *pLast : string{};
        text.push_back( LocTextInternal::kEnd );
        return text;
    }

    string LocalizedMessage::literal( string_view text )
    {
        string result;
        result.push_back( LocTextInternal::kBegin );
        result.push_back( LocTextInternal::kKindLiteral );
        LocTextInternal::appendPlain( result, text );
        result.push_back( LocTextInternal::kEnd );
        return result;
    }

    bool LocalizedMessage::isMessage( string_view text )
    {
        return text.size() >= 3 && text.front() == LocTextInternal::kBegin && text.back() == LocTextInternal::kEnd;
    }
} // namespace sw
