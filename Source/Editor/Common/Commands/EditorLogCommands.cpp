#include "pch.h"

#include "Editor/Common/Commands/EditorLogCommands.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"

#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
    namespace
    {
        struct EditorLogCommandsInternal
        {
            static bool isDigit( utf8 ch ) { return '0' <= ch && ch <= '9'; }

            static bool isAlphaNumeric( utf8 ch ) { return isDigit( ch ) || ( 'a' <= ch && ch <= 'z' ) || ( 'A' <= ch && ch <= 'Z' ); }

            /** @brief 경로를 끝내는 문자입니다(공백 · 따옴표 · 괄호 · 꺾쇠 · 대괄호). */
            static bool isPathBoundary( utf8 ch )
            {
                return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '"' || ch == '\'' || ch == '<' || ch == '>' || ch == '[' || ch == ']' ||
                       ch == '(' || ch == '|' || ch == '=';
            }

            /** @brief @p position 에서 시작하는 숫자를 읽습니다. 숫자가 없으면 false 입니다. */
            [[nodiscard]] static bool readNumber( string_view text, size_t position, uint32& outValue, size_t& outEnd )
            {
                uint64 value = 0;
                size_t index = position;
                while ( index < text.size() && isDigit( text[index] ) && index - position < 9 )
                {
                    value = value * 10 + static_cast<uint64>( text[index] - '0' );
                    ++index;
                }
                if ( index == position )
                    return false;
                outValue = static_cast<uint32>( value );
                outEnd   = index;
                return true;
            }

            /**
             * @brief 구분자(`(` · `:`) 앞의 경로를 거꾸로 읽습니다. 확장자(`.` + 영숫자 1~8 자)로 끝나야 경로로 봅니다.
             * @return 경로가 아니면 빈 문자열
             */
            static string_view readPathBefore( string_view text, size_t separator )
            {
                size_t begin = separator;
                while ( begin > 0 && isPathBoundary( text[begin - 1] ) == false )
                {
                    --begin;
                }
                const string_view path = text.substr( begin, separator - begin );
                const size_t      dot  = path.rfind( '.' );
                if ( dot == string_view::npos || dot == 0 )
                    return {};
                const size_t extensionLength = path.size() - dot - 1;
                if ( extensionLength == 0 || extensionLength > 8 )
                    return {};
                for ( size_t index = dot + 1; index < path.size(); ++index )
                {
                    if ( isAlphaNumeric( path[index] ) == false )
                        return {};
                }
                // 확장자 앞에 이름이 있어야 한다(`.cpp` 하나만으로는 경로가 아니다).
                const utf8 beforeDot = path[dot - 1];
                if ( beforeDot == '/' || beforeDot == '\\' )
                    return {};
                return path;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorLog" );

    bool EditorLogCommands::parseSourceLocation( string_view text, EditorSourceLocation& outLocation )
    {
        for ( size_t index = 0; index < text.size(); ++index )
        {
            const utf8 ch = text[index];
            if ( ch != '(' && ch != ':' )
                continue;

            uint32 line = 0;
            size_t end  = 0;
            if ( EditorLogCommandsInternal::readNumber( text, index + 1, line, end ) == false || line == 0 )
                continue;
            // `경로(줄)` 은 닫는 괄호나 열 번호(`,`)가 뒤따라야 한다.
            if ( ch == '(' && ( end >= text.size() || ( text[end] != ')' && text[end] != ',' ) ) )
                continue;

            const string_view path = EditorLogCommandsInternal::readPathBefore( text, index );
            if ( path.empty() )
                continue;
            outLocation._file = string( path );
            outLocation._line = line;
            return true;
        }
        return false;
    }

    bool EditorLogCommands::findLogEntryLocation( string_view message, string_view entryFile, int32 entryLine, EditorSourceLocation& outLocation )
    {
        if ( parseSourceLocation( message, outLocation ) )
            return true;
        if ( entryFile.empty() )
            return false;
        outLocation._file = string( entryFile );
        outLocation._line = entryLine > 0 ? static_cast<uint32>( entryLine ) : 1u;
        return true;
    }

    string EditorLogCommands::makeOpenCommand( string_view commandTemplate, const EditorSourceLocation& location )
    {
        if ( commandTemplate.empty() || location._file.empty() )
            return {};
        const string withFile = StringUtil::replace( commandTemplate, kFilePlaceholder, location._file );
        return StringUtil::replace( withFile, kLinePlaceholder, to_string( location._line ) );
    }

    string EditorLogCommands::getOpenCommandTemplate()
    {
        const string& configured = getEditorToolDefaults()._ideOpenCommand;
        if ( configured.empty() == false )
            return configured;
#if SW_PLATFORM_WINDOWS
        return "cmd /c code -g \"{file}:{line}\"";
#else
        return "code -g \"{file}:{line}\"";
#endif
    }

    bool EditorLogCommands::openInIde( const EditorSourceLocation& location )
    {
        const string command = makeOpenCommand( getOpenCommandTemplate(), location );
        if ( command.empty() )
        {
            SW_LOG_WARNING( "Open in IDE: no command template (_ideOpenCommand in editortooldefaults.json) or no file." );
            return false;
        }
        if ( Process::launchDetached( command ) == false )
        {
            SW_LOG_WARNING( "Open in IDE: failed to launch '%#'", command.c_str() );
            return false;
        }
        SW_LOG_INFO( "Open in IDE: %#", command.c_str() );
        return true;
    }

    EditorLogTagFilter::EditorLogTagFilter()
        : _uniqueHiddenTag{}
        , _revision{ 0 }
    {
    }

    void EditorLogTagFilter::setTagVisible( string_view tag, bool bVisible )
    {
        const string key = StringUtil::toLower( string( tag ).c_str() );
        if ( bVisible )
            _uniqueHiddenTag.erase( key );
        else
            _uniqueHiddenTag.insert( key );
        ++_revision;
    }

    bool EditorLogTagFilter::isTagVisible( string_view tag ) const
    {
        if ( _uniqueHiddenTag.empty() )
            return true;
        return _uniqueHiddenTag.find( StringUtil::toLower( string( tag ).c_str() ) ) == _uniqueHiddenTag.end();
    }

    void EditorLogTagFilter::showAll()
    {
        _uniqueHiddenTag.clear();
        ++_revision;
    }
} // namespace sw::editor
