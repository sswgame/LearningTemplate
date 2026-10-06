#include "pch.h"

#include "Engine/UI/Binding/UiBindingExpression.h"

#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        struct UiBindingExpressionInternal
        {
            /** @brief 종류 이름을 소스로 읽습니다. */
            [[nodiscard]] static bool tryParseSource( string_view kind, UiBindingSource& outSource )
            {
                if ( kind == "bind" )
                    outSource = UiBindingSource::ViewModel;
                else if ( kind == "poll" )
                    outSource = UiBindingSource::Poll;
                else if ( kind == "setting" )
                    outSource = UiBindingSource::Setting;
                else
                    return false;
                return true;
            }

            /** @brief `키=값` 하나를 식에 적습니다. */
            [[nodiscard]] static bool applyOption( string_view option, UiBindingExpression& outExpression, string& outError )
            {
                const size_t equal = option.find( '=' );
                if ( equal == string_view::npos )
                {
                    outError = "option '" + string( option ) + "' is not key=value";
                    return false;
                }
                const string_view key   = StringUtil::trim( option.substr( 0, equal ) );
                const string_view value = StringUtil::trim( option.substr( equal + 1 ) );
                if ( value.empty() )
                {
                    outError = "option '" + string( key ) + "' has no value";
                    return false;
                }
                if ( key == "mode" )
                {
                    if ( value == "OneWay" || value == "TwoWay" )
                    {
                        outExpression._mode = value == "OneWay" ? UiBindingMode::OneWay : UiBindingMode::TwoWay;
                        return true;
                    }
                    outError = "unknown mode '" + string( value ) + "' (OneWay, TwoWay)";
                    return false;
                }
                if ( key == "format" )
                {
                    outExpression._format = hashed_string( value );
                    return true;
                }
                if ( key == "converter" )
                {
                    outExpression._converter = hashed_string( value );
                    return true;
                }
                outError = "unknown option '" + string( key ) + "' (mode, format, converter)";
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool UiBindingExpression::parse( string_view text, UiBindingExpression& outExpression, string& outError )
    {
        outExpression           = UiBindingExpression{};
        const string_view whole = StringUtil::trim( text );
        if ( whole.size() < 2 || whole.front() != '{' || whole.back() != '}' )
        {
            outError = "binding expression must be enclosed in { }";
            return false;
        }
        const string_view body  = whole.substr( 1, whole.size() - 2 );
        const size_t      colon = body.find( ':' );
        if ( colon == string_view::npos )
        {
            outError = "binding expression has no kind (bind:, poll:, setting:)";
            return false;
        }
        const string_view kind = StringUtil::trim( body.substr( 0, colon ) );
        if ( UiBindingExpressionInternal::tryParseSource( kind, outExpression._source ) == false )
        {
            outError = "unknown binding kind '" + string( kind ) + "' (bind, poll, setting)";
            return false;
        }
        if ( outExpression._source == UiBindingSource::Setting )
            outExpression._mode = UiBindingMode::TwoWay;

        string_view rest    = body.substr( colon + 1 );
        size_t      comma   = rest.find( ',' );
        outExpression._path = string( StringUtil::trim( rest.substr( 0, comma ) ) );
        if ( outExpression._path.empty() )
        {
            outError = "binding expression has an empty path";
            return false;
        }
        while ( comma != string_view::npos )
        {
            rest                     = rest.substr( comma + 1 );
            comma                    = rest.find( ',' );
            const string_view option = StringUtil::trim( rest.substr( 0, comma ) );
            if ( UiBindingExpressionInternal::applyOption( option, outExpression, outError ) == false )
                return false;
        }
        return true;
    }
} // namespace sw
