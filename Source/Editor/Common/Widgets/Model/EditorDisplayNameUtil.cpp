#include "pch.h"

#include "Editor/Common/Widgets/Model/EditorDisplayNameUtil.h"

namespace sw::editor
{
    namespace
    {
        struct EditorDisplayNameUtilInternal
        {
            static bool isUpper( utf8 ch ) { return ch >= 'A' && ch <= 'Z'; }
            static bool isLower( utf8 ch ) { return ch >= 'a' && ch <= 'z'; }
            static bool isDigit( utf8 ch ) { return ch >= '0' && ch <= '9'; }
            static bool isSeparator( utf8 ch ) { return ch == '_' || ch == ' '; }

            /** @brief @p name 이 @p prefix 로 시작하고 그 뒤에 글자가 더 있는지 봅니다. @p bUpperAfter 면 바로 뒤 글자가 대문자여야 합니다. */
            static bool hasPrefix( string_view name, string_view prefix, bool bUpperAfter )
            {
                const bool bLonger = name.size() > prefix.size() && name.substr( 0, prefix.size() ) == prefix;
                return bLonger && ( bUpperAfter == false || isUpper( name[prefix.size()] ) );
            }

            /** @brief 앞 밑줄, 멤버 · 정적 접두어, 바로 뒤가 대문자인 `b` · `p` · `pp` 를 뗀 나머지입니다. */
            static string_view stripPrefix( string_view name )
            {
                while ( name.empty() == false && name.front() == '_' )
                {
                    name.remove_prefix( 1 );
                }
                // `m_speed` · `s_count`(비공개 정적 `_s_count` 는 위에서 밑줄을 뗀 뒤 여기서)
                if ( hasPrefix( name, "m_", false ) || hasPrefix( name, "s_", false ) )
                    name.remove_prefix( 2 );
                if ( hasPrefix( name, "pp", true ) )
                    name.remove_prefix( 2 );
                else if ( hasPrefix( name, "b", true ) || hasPrefix( name, "p", true ) )
                    name.remove_prefix( 1 );
                return name;
            }

            /** @brief @p text 의 @p index 글자(구분자 아님, 앞 글자도 구분자 아님)에서 새 단어가 시작하는지 봅니다. */
            static bool startsWord( string_view text, size_t index )
            {
                const utf8 prev     = text[index - 1];
                const utf8 ch       = text[index];
                const bool bNextLow = index + 1 < text.size() && isLower( text[index + 1] );
                if ( isUpper( ch ) )
                    return isLower( prev ) || ( ( isUpper( prev ) || isDigit( prev ) ) && bNextLow );
                if ( isDigit( ch ) )
                    return isDigit( prev ) == false;
                return false;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    string EditorDisplayNameUtil::makeDisplayName( string_view name )
    {
        using Internal         = EditorDisplayNameUtilInternal;
        const string_view text = Internal::stripPrefix( name );
        if ( text.empty() )
            return string( name );

        string result;
        result.reserve( text.size() + 8 );
        bool bWordStart = true;
        for ( size_t index = 0; index < text.size(); ++index )
        {
            const utf8 ch = text[index];
            if ( Internal::isSeparator( ch ) )
            {
                bWordStart = true;
                continue;
            }
            if ( bWordStart == false && Internal::startsWord( text, index ) )
                bWordStart = true;
            if ( bWordStart && result.empty() == false )
                result.push_back( ' ' );
            result.push_back( bWordStart && Internal::isLower( ch ) ? static_cast<utf8>( ch - 'a' + 'A' ) : ch );
            bWordStart = false;
        }
        return result.empty() ? string( name ) : result;
    }

    string EditorDisplayNameUtil::chooseLabel( string_view displayName, string_view name )
    {
        return displayName.empty() ? makeDisplayName( name ) : string( displayName );
    }
} // namespace sw::editor
