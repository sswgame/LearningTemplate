#include "pch.h"

#include "Engine/UI/Binding/UIBindingExpression.h"

#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        struct UIBindingExpressionInternal
        {
            /** @brief 종류 이름을 소스로 읽습니다. */
            [[nodiscard]] static bool tryParseSource( string_view kind, UIBindingSource& outSource )
            {
                if ( kind == "bind" )
                    outSource = UIBindingSource::ViewModel;
                else if ( kind == "poll" )
                    outSource = UIBindingSource::Poll;
                else if ( kind == "setting" )
                    outSource = UIBindingSource::Setting;
                else
                    return false;
                return true;
            }

            /** @brief `키=값` 하나를 식에 적습니다. */
            [[nodiscard]] static expected<void> applyOption( string_view option, UIBindingExpression& outExpression )
            {
                string_view key;
                string_view value;
                if ( StringUtil::trySplitKeyValue( option, key, value ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "a binding option is not key=value", option } );
                if ( value.empty() )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "a binding option has no value", key } );
                if ( key == "mode" )
                {
                    if ( value == "OneWay" || value == "TwoWay" )
                    {
                        outExpression._mode = value == "OneWay" ? UIBindingMode::OneWay : UIBindingMode::TwoWay;
                        return {};
                    }
                    return unexpected( Error{ ErrorKind::InvalidArgument, "unknown binding mode (OneWay, TwoWay)", value } );
                }
                if ( key == "format" )
                {
                    outExpression._format = hashed_string( value );
                    return {};
                }
                if ( key == "converter" )
                {
                    outExpression._converter = hashed_string( value );
                    return {};
                }
                return unexpected( Error{ ErrorKind::InvalidArgument, "unknown binding option (mode, format, converter)", key } );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    expected<void> UIBindingExpression::parse( string_view text, UIBindingExpression& outExpression )
    {
        outExpression                  = UIBindingExpression{};
        const string_view whole        = StringUtil::trim( text );
        const bool        bNotEnclosed = whole.size() < 2 || whole.front() != '{' || whole.back() != '}';
        if ( bNotEnclosed )
            return unexpected( Error{ ErrorKind::InvalidArgument, "binding expression must be enclosed in { }", whole } );
        const string_view body  = whole.substr( 1, whole.size() - 2 );
        const size_t      colon = body.find( ':' );
        if ( colon == string_view::npos )
            return unexpected( Error{ ErrorKind::InvalidArgument, "binding expression has no kind (bind:, poll:, setting:)", whole } );
        const string_view kind = StringUtil::trim( body.substr( 0, colon ) );
        if ( UIBindingExpressionInternal::tryParseSource( kind, outExpression._source ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "unknown binding kind (bind, poll, setting)", kind } );
        if ( outExpression._source == UIBindingSource::Setting )
            outExpression._mode = UIBindingMode::TwoWay;

        string_view rest    = body.substr( colon + 1 );
        size_t      comma   = rest.find( ',' );
        outExpression._path = string( StringUtil::trim( rest.substr( 0, comma ) ) );
        if ( outExpression._path.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "binding expression has an empty path", whole } );
        while ( comma != string_view::npos )
        {
            rest                     = rest.substr( comma + 1 );
            comma                    = rest.find( ',' );
            const string_view option = StringUtil::trim( rest.substr( 0, comma ) );
            if ( expected<void> applied = UIBindingExpressionInternal::applyOption( option, outExpression ); applied.has_value() == false )
                return applied;
        }
        return {};
    }
} // namespace sw
