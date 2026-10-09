#include "pch.h"

#include "Engine/Localization/PortableObjectFile.h"

#include "Core/Container/StringUtil.h"

namespace sw
{
    namespace
    {
        /** @brief 지금 읽고 있는 따옴표 글이 어느 칸에 이어지는지입니다. */
        enum class PortableObjectField : uint8
        {
            None = 0,
            Context,
            Source,
            Translation,
            PreviousSource
        };

        struct PortableObjectFileInternal
        {
            static constexpr utf8 kDoubleQuote = 0x22; ///< `"` — 문자 리터럴로 적으면 린트의 글 토큰이 어긋난다

            /** @brief `"…"` 하나를 풀어 @p outText 에 붙입니다. 따옴표로 시작 · 끝나지 않으면 false 입니다. */
            [[nodiscard]] static bool appendQuoted( string_view token, string& outText )
            {
                token = StringUtil::trim( token );
                if ( token.size() < 2 || token.front() != kDoubleQuote || token.back() != kDoubleQuote )
                    return false;
                token = token.substr( 1, token.size() - 2 );
                for ( size_t index = 0; index < token.size(); ++index )
                {
                    const utf8 character = token[index];
                    if ( character != '\\' || index + 1 >= token.size() )
                    {
                        outText.push_back( character );
                        continue;
                    }
                    ++index;
                    const utf8 escaped = token[index];
                    if ( escaped == 'n' )
                        outText.push_back( '\n' );
                    else if ( escaped == 't' )
                        outText.push_back( '\t' );
                    else if ( escaped == 'r' )
                        outText.push_back( '\r' );
                    else
                        outText.push_back( escaped );
                }
                return true;
            }

            static string findHeaderField( string_view header, string_view fieldName )
            {
                size_t lineStart = 0;
                while ( lineStart < header.size() )
                {
                    size_t lineEnd = header.find( '\n', lineStart );
                    if ( lineEnd == string_view::npos )
                        lineEnd = header.size();
                    const string_view line = header.substr( lineStart, lineEnd - lineStart );
                    if ( StringUtil::startsWith( line, fieldName, true ) && line.size() > fieldName.size() && line[fieldName.size()] == ':' )
                        return string( StringUtil::trim( line.substr( fieldName.size() + 1 ) ) );
                    lineStart = lineEnd + 1;
                }
                return {};
            }

            static void appendQuotedLine( string& outText, string_view keyword, string_view value )
            {
                outText.append( keyword );
                outText.append( " \"" );
                outText.append( PortableObjectFile::escapeText( value ) );
                outText.append( "\"\n" );
            }
        };

        /** @brief 읽는 동안의 상태 — 지금 항목과 다 읽은 항목들입니다. */
        struct PortableObjectParseState
        {
            vector<PortableObjectEntry> _listEntry;
            PortableObjectEntry         _current;
            string                      _language;
            string                      _projectName;
            PortableObjectField         _field{ PortableObjectField::None };
            bool                        _bHasSource{ false };
            bool                        _bHasTranslation{ false };

            /** @brief 항목 하나를 마칩니다 — 머리 항목(msgid "")은 Language · 프로젝트 이름만 읽습니다. */
            void flush()
            {
                if ( _bHasSource && _current._source.empty() )
                {
                    _language    = PortableObjectFileInternal::findHeaderField( _current._translation, "Language" );
                    _projectName = PortableObjectFileInternal::findHeaderField( _current._translation, "X-Localization-Project" );
                }
                else if ( _bHasSource )
                    _listEntry.push_back( std::move( _current ) );
                _current         = PortableObjectEntry{};
                _bHasSource      = false;
                _bHasTranslation = false;
                _field           = PortableObjectField::None;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string PortableObjectFile::escapeText( string_view text )
    {
        string escaped;
        escaped.reserve( text.size() + 8 );
        for ( const utf8 character : text )
        {
            if ( character == '\\' )
                escaped.append( "\\\\" );
            else if ( character == PortableObjectFileInternal::kDoubleQuote )
                escaped.append( "\\\"" );
            else if ( character == '\n' )
                escaped.append( "\\n" );
            else if ( character == '\t' )
                escaped.append( "\\t" );
            else if ( character == '\r' )
                escaped.append( "\\r" );
            else
                escaped.push_back( character );
        }
        return escaped;
    }

    bool PortableObjectFile::parse( string_view text, string* pOutError )
    {
        PortableObjectParseState state;
        string                   error;
        uint32                   lineNumber{ 0 };

        size_t lineStart = 0;
        while ( lineStart <= text.size() && error.empty() )
        {
            size_t lineEnd = text.find( '\n', lineStart );
            if ( lineEnd == string_view::npos )
                lineEnd = text.size();
            string_view line = text.substr( lineStart, lineEnd - lineStart );
            lineStart        = lineEnd + 1;
            ++lineNumber;
            if ( lineNumber == 1 && line.size() >= 3 && static_cast<uint8>( line[0] ) == 0xEF )
                line.remove_prefix( 3 ); // UTF-8 BOM
            line = StringUtil::trim( line );
            if ( line.empty() )
            {
                if ( state._bHasTranslation )
                    state.flush();
                if ( lineEnd == text.size() )
                    break;
                continue;
            }

            const bool bStartsEntryPart = line[0] == '#' || StringUtil::startsWith( line, "msgctxt" ) || StringUtil::startsWith( line, "msgid " );
            if ( bStartsEntryPart && state._bHasTranslation )
                state.flush();

            if ( StringUtil::startsWith( line, "#," ) )
            {
                state._current._bFuzzy = state._current._bFuzzy || line.find( "fuzzy" ) != string_view::npos;
                state._field           = PortableObjectField::None;
            }
            else if ( StringUtil::startsWith( line, "#|" ) )
            {
                const string_view rest = StringUtil::trim( line.substr( 2 ) );
                if ( StringUtil::startsWith( rest, "msgid " ) )
                {
                    state._field = PortableObjectField::PreviousSource;
                    if ( PortableObjectFileInternal::appendQuoted( rest.substr( 6 ), state._current._previousSource ) == false )
                        error = "bad #| msgid";
                }
                else if ( state._field == PortableObjectField::PreviousSource && PortableObjectFileInternal::appendQuoted( rest, state._current._previousSource ) == false )
                    error = "bad #| continuation";
            }
            else if ( StringUtil::startsWith( line, "#." ) )
            {
                state._current._listExtractedComment.push_back( string( StringUtil::trim( line.substr( 2 ) ) ) );
                state._field = PortableObjectField::None;
            }
            else if ( StringUtil::startsWith( line, "#:" ) )
            {
                state._current._listReference.push_back( string( StringUtil::trim( line.substr( 2 ) ) ) );
                state._field = PortableObjectField::None;
            }
            else if ( line[0] == '#' )
            {
                if ( state._current._translatorComment.empty() == false )
                    state._current._translatorComment.push_back( '\n' );
                state._current._translatorComment.append( StringUtil::trim( line.substr( 1 ) ) );
                state._field = PortableObjectField::None;
            }
            else if ( StringUtil::startsWith( line, "msgid_plural" ) || StringUtil::startsWith( line, "msgstr[" ) )
                error = "msgid_plural is not used - plurals are ICU {n, plural, ...} inside one msgid";
            else if ( StringUtil::startsWith( line, "msgctxt " ) )
            {
                state._field = PortableObjectField::Context;
                if ( PortableObjectFileInternal::appendQuoted( line.substr( 8 ), state._current._context ) == false )
                    error = "bad msgctxt";
            }
            else if ( StringUtil::startsWith( line, "msgid " ) )
            {
                state._field      = PortableObjectField::Source;
                state._bHasSource = true;
                if ( PortableObjectFileInternal::appendQuoted( line.substr( 6 ), state._current._source ) == false )
                    error = "bad msgid";
            }
            else if ( StringUtil::startsWith( line, "msgstr " ) )
            {
                state._field           = PortableObjectField::Translation;
                state._bHasTranslation = true;
                if ( state._bHasSource == false || PortableObjectFileInternal::appendQuoted( line.substr( 7 ), state._current._translation ) == false )
                    error = "msgstr without msgid";
            }
            else if ( line[0] == PortableObjectFileInternal::kDoubleQuote )
            {
                string* pTarget = nullptr;
                if ( state._field == PortableObjectField::Context )
                    pTarget = &state._current._context;
                else if ( state._field == PortableObjectField::Source )
                    pTarget = &state._current._source;
                else if ( state._field == PortableObjectField::Translation )
                    pTarget = &state._current._translation;
                if ( pTarget == nullptr || PortableObjectFileInternal::appendQuoted( line, *pTarget ) == false )
                    error = "string continuation without a keyword";
            }
            else
                error = "unknown line";

            if ( lineEnd == text.size() )
                break;
        }
        if ( error.empty() && state._bHasSource && state._bHasTranslation == false )
            error = "msgid without msgstr";
        if ( error.empty() == false )
        {
            if ( pOutError != nullptr )
                *pOutError = to_string( lineNumber ) + ": " + error;
            return false;
        }
        state.flush();
        _language    = state._language;
        _projectName = state._projectName;
        _listEntry   = std::move( state._listEntry );
        return true;
    }

    string PortableObjectFile::toText() const
    {
        string text;
        text.append( "msgid \"\"\nmsgstr \"\"\n" );
        text += "\"Language: " + escapeText( _language ) + "\\n\"\n";
        text += "\"X-Localization-Project: " + escapeText( _projectName ) + "\\n\"\n";
        text.append( "\"MIME-Version: 1.0\\n\"\n\"Content-Type: text/plain; charset=UTF-8\\n\"\n\"Content-Transfer-Encoding: 8bit\\n\"\n" );
        text.append( "\"X-Message-Format: ICU\\n\"\n\"X-Generator: SWEngine LocalizationTools\\n\"\n" );
        for ( const PortableObjectEntry& entry : _listEntry )
        {
            text.push_back( '\n' );
            if ( entry._translatorComment.empty() == false )
            {
                size_t lineStart = 0;
                while ( lineStart <= entry._translatorComment.size() )
                {
                    size_t lineEnd = entry._translatorComment.find( '\n', lineStart );
                    if ( lineEnd == string::npos )
                        lineEnd = entry._translatorComment.size();
                    text.append( "# " ).append( string_view( entry._translatorComment ).substr( lineStart, lineEnd - lineStart ) ).append( "\n" );
                    lineStart = lineEnd + 1;
                }
            }
            for ( const string& comment : entry._listExtractedComment )
            {
                text.append( "#. " ).append( comment ).append( "\n" );
            }
            for ( const string& reference : entry._listReference )
            {
                text.append( "#: " ).append( reference ).append( "\n" );
            }
            if ( entry._bFuzzy )
                text.append( "#, fuzzy\n" );
            if ( entry._previousSource.empty() == false )
                PortableObjectFileInternal::appendQuotedLine( text, "#| msgid", entry._previousSource );
            PortableObjectFileInternal::appendQuotedLine( text, "msgctxt", entry._context );
            PortableObjectFileInternal::appendQuotedLine( text, "msgid", entry._source );
            PortableObjectFileInternal::appendQuotedLine( text, "msgstr", entry._translation );
        }
        return text;
    }
} // namespace sw
