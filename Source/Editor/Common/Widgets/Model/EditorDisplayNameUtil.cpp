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

            /**
             * @brief 낱말 하나로 쓰면 대문자로 읽는 약어입니다(`_uiScale` → "UI Scale", `_ideOpenCommand` → "IDE Open Command").
             * @details 언리얼 `FName::NameToDisplayString` 도 약어 처리를 코드에 둔다. 멤버 이름 규칙(낙타 표기)이 약어를 `Ui` 로 적게 하므로 표시에서만 되돌린다.
             */
            static constexpr string_view kArrAcronym[] = { "AI", "API", "CPU", "FPS", "GI", "GPU", "HDR", "HUD", "ID", "IDE", "IK", "IP", "LOD", "NPC", "PBR", "RHI", "SDF", "UI", "URL", "UV", "VFX", "XP" };

            /** @brief 첫 글자만 대문자로 만든 낱말 @p word 가 약어면 대문자로 바꿉니다. */
            static void upperIfAcronym( string& inoutText, size_t wordStart )
            {
                const string_view word{ inoutText.data() + wordStart, inoutText.size() - wordStart };
                for ( const string_view acronym : kArrAcronym )
                {
                    bool bSame = word.size() == acronym.size();
                    for ( size_t index = 0; bSame && index < word.size(); ++index )
                    {
                        const utf8 ch = word[index];
                        bSame         = ( isLower( ch ) ? static_cast<utf8>( ch - 'a' + 'A' ) : ch ) == acronym[index];
                    }
                    if ( bSame )
                    {
                        for ( size_t index = wordStart; index < inoutText.size(); ++index )
                        {
                            inoutText[index] = isLower( inoutText[index] ) ? static_cast<utf8>( inoutText[index] - 'a' + 'A' ) : inoutText[index];
                        }
                        return;
                    }
                }
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
        bool   bWordStart = true;
        size_t wordStart  = 0;
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
            if ( bWordStart )
            {
                Internal::upperIfAcronym( result, wordStart );
                if ( result.empty() == false )
                    result.push_back( ' ' );
                wordStart = result.size();
            }
            result.push_back( bWordStart && Internal::isLower( ch ) ? static_cast<utf8>( ch - 'a' + 'A' ) : ch );
            bWordStart = false;
        }
        Internal::upperIfAcronym( result, wordStart );
        return result.empty() ? string( name ) : result;
    }

    string EditorDisplayNameUtil::chooseLabel( string_view displayName, string_view name )
    {
        return displayName.empty() ? makeDisplayName( name ) : string( displayName );
    }
} // namespace sw::editor
