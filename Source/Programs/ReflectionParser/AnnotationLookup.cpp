#include "pch.h"

#include "Programs/ReflectionParser/AnnotationLookup.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Programs/ReflectionParser/CursorReader.h"
#include "Programs/ReflectionParser/ParserConfig.h"

namespace sw
{
    struct AnnotationLookup::AttrSearch
    {
        string_view _prefix;
        string      _spelling;
    };

    CXChildVisitResult AnnotationLookup::attrSearchVisitor( CXCursor cursor, CXCursor, CXClientData data )
    {
        const CXCursorKind kind = clang_getCursorKind( cursor );
        if ( kind != CXCursor_AnnotateAttr && kind != CXCursor_UnexposedAttr )
            return CXChildVisit_Continue;

        AttrSearch*    pSearch    = static_cast<AttrSearch*>( data );
        const CXString cxSpelling = clang_getCursorSpelling( cursor );
        const utf8*    pText      = clang_getCString( cxSpelling );
        const bool     bMatch     = pText != nullptr && string_view( pText ).find( pSearch->_prefix ) != string_view::npos;
        if ( bMatch )
            pSearch->_spelling = pText;
        clang_disposeString( cxSpelling );
        return bMatch ? CXChildVisit_Break : CXChildVisit_Continue;
    }

    string AnnotationLookup::findAnnotateAttr( const CXCursor cursor, const string_view prefix )
    {
        AttrSearch search{ prefix, {} };
        clang_visitChildren( cursor, attrSearchVisitor, &search );
        return std::move( search._spelling );
    }

    const string& AnnotationLookup::getCachedFileContent( const string& path )
    {
        thread_local unordered_map<string, string> s_mapPathToContent;
        const auto                                 it = s_mapPathToContent.find( path );
        if ( it != s_mapPathToContent.end() )
            return it->second;

        // 못 읽으면 빈 글로 캐시한다 — 그 헤더의 애노테이션을 찾지 못하니 알린다.
        string content;
        if ( FileUtil::readTextFile( path, content ) == false )
            SW_LOG_ERROR_ID( "AstVisitor.CouldNotReadAnnotations", "Could not read '%#' - annotations in it are not seen", path );
        return s_mapPathToContent.emplace( path, std::move( content ) ).first->second;
    }

    struct AnnotationLookup::SourceWindow
    {
        const string* _pContent;
        size_t        _windowStart;
        string_view   _window;

        SourceWindow()
            : _pContent{ nullptr }
            , _windowStart{ 0 }
            , _window{}
        {
        }

        bool isValid() const noexcept { return _pContent != nullptr; }
    };

    AnnotationLookup::SourceWindow AnnotationLookup::makeSourceWindow( const CXCursor cursor, const ParserConfig& config )
    {
        SourceWindow result;
        CXFile       file   = nullptr;
        uint32       offset = 0;
        clang_getFileLocation( clang_getCursorLocation( cursor ), &file, nullptr, nullptr, &offset );
        if ( file == nullptr )
            return result;

        const string& content = getCachedFileContent( CursorReader::takeString( clang_getFileName( file ) ) );
        if ( content.empty() || offset > content.size() )
            return result;

        const size_t lookback = config._sourceLookbackBytes;
        result._pContent      = &content;
        result._windowStart   = ( offset > lookback ) ? ( offset - lookback ) : 0;
        result._window        = string_view( content.data() + result._windowStart, offset - result._windowStart );
        return result;
    }

    size_t AnnotationLookup::rfindOutsideComments( const string_view window, const string_view searchStr )
    {
        size_t searchEnd = string::npos;
        while ( true )
        {
            const size_t pos = window.rfind( searchStr, searchEnd );
            if ( pos == string_view::npos )
                return string::npos;

            bool bInLineComment  = false;
            bool bInBlockComment = false;
            for ( size_t index = 0; index < pos; ++index )
            {
                if ( bInLineComment == false && bInBlockComment == false )
                {
                    if ( window[index] == '/' && index + 1 < pos )
                    {
                        if ( window[index + 1] == '/' )
                        {
                            bInLineComment = true;
                            ++index;
                        }
                        else if ( window[index + 1] == '*' )
                        {
                            bInBlockComment = true;
                            ++index;
                        }
                    }
                }
                else if ( bInLineComment )
                {
                    if ( window[index] == '\n' )
                        bInLineComment = false;
                }
                else if ( bInBlockComment )
                {
                    const bool bBlockCommentEnd = window[index] == '*' && index + 1 < pos && window[index + 1] == '/';
                    if ( bBlockCommentEnd )
                    {
                        bInBlockComment = false;
                        ++index;
                    }
                }
            }

            if ( bInLineComment == false && bInBlockComment == false )
                return pos;
            if ( pos == 0 )
                return string::npos;
            searchEnd = pos - 1;
        }
    }

    bool AnnotationLookup::hasNoDeclarationBoundary( const string_view text )
    {
        bool bInQuote = false;
        for ( size_t index = 0; index < text.size(); ++index )
        {
            const utf8 character = text[index];
            if ( bInQuote )
            {
                if ( character == '\\' )
                    ++index; // 이스케이프한 글자는 따옴표를 닫지 않는다
                else if ( character == '"' )
                    bInQuote = false;
                continue;
            }
            const bool bStatementBoundary = character == ';' || character == '{' || character == '}';
            if ( character == '"' )
                bInQuote = true;
            else if ( bStatementBoundary )
                return false;
        }
        return true;
    }

    bool AnnotationLookup::hasSourceAnnotation( const CXCursor cursor, const ReflectAnnotationDesc& desc, const ParserConfig& config )
    {
        const SourceWindow source = makeSourceWindow( cursor, config );
        if ( source.isValid() == false )
            return false;

        const size_t macroPos = rfindOutsideComments( source._window, desc._pMacroOpen );
        if ( macroPos != string_view::npos && hasNoDeclarationBoundary( source._window.substr( macroPos ) ) )
            return true;

        StringBuilder<constant::buffer::kMax128> needle;
        needle.appendFormat( "annotate(\"%#", desc._pPrefix );
        const size_t annotatePos = rfindOutsideComments( source._window, needle.view() );
        return annotatePos != string_view::npos && hasNoDeclarationBoundary( source._window.substr( annotatePos ) );
    }

    string AnnotationLookup::extractSourceAnnotation( const CXCursor cursor, const ReflectAnnotationDesc& desc, const ParserConfig& config )
    {
        const SourceWindow source = makeSourceWindow( cursor, config );
        if ( source.isValid() == false )
            return {};

        const string_view macroOpen = desc._pMacroOpen;
        const size_t      macroPos  = rfindOutsideComments( source._window, macroOpen );
        if ( macroPos == string_view::npos )
            return {};

        // 매크로와 커서 사이에 경계가 있으면 앞 선언의 것이다(`hasSourceAnnotation` 과 같은 판정 — 따옴표 안은 보지 않는다).
        if ( hasNoDeclarationBoundary( source._window.substr( macroPos ) ) == false )
            return {};

        // 매크로 괄호 깊이를 따라 읽는다. 인자는 창 끝(커서)을 넘어 끝날 수 있어 파일 내용에서 읽는다.
        const string& content   = *source._pContent;
        const size_t  argsStart = source._windowStart + macroPos + macroOpen.size();
        size_t        charIndex = argsStart;
        int32         depth     = 1;
        bool          bInQuote  = false;
        while ( charIndex < content.size() && depth > 0 )
        {
            const utf8 character = content[charIndex++];
            if ( bInQuote )
            {
                if ( character == '\\' )
                    ++charIndex; // 이스케이프한 글자(`\"`)는 따옴표를 닫지 않는다
                else if ( character == '"' )
                    bInQuote = false;
                continue;
            }
            if ( character == '"' )
                bInQuote = true;
            else if ( character == '(' )
                ++depth;
            else if ( character == ')' )
                --depth;
        }
        if ( depth != 0 || charIndex <= argsStart )
            return {}; // 괄호가 닫히지 않았다(파일 끝) — 컴파일러가 먼저 알린다

        StringBuilder<constant::buffer::kMax1024> b;
        b.append( desc._pPrefix );
        b.append( string_view( content.data() + argsStart, charIndex - argsStart - 1 ) );
        return string( b.view() );
    }

    bool AnnotationLookup::hasAnnotation( const CXCursor cursor, const ReflectAnnotationDesc& desc, const ParserConfig& config )
    {
        return findAnnotateAttr( cursor, desc._pPrefix ).empty() == false || hasSourceAnnotation( cursor, desc, config );
    }

    string AnnotationLookup::readAnnotation( const CXCursor cursor, const ReflectAnnotationDesc& desc, const ParserConfig& config )
    {
        string spelling = findAnnotateAttr( cursor, desc._pPrefix );
        if ( spelling.empty() )
            spelling = extractSourceAnnotation( cursor, desc, config );
        return spelling;
    }

    bool AnnotationLookup::isReflectedRecord( const CXCursor declaration, const ParserConfig& config )
    {
        if ( clang_Cursor_isNull( declaration ) != 0 )
            return false;
        const CXCursor definition   = clang_getCursorDefinition( declaration );
        const CXCursor searchCursor = clang_Cursor_isNull( definition ) == 0 ? definition : declaration;
        return hasAnnotation( searchCursor, annotation::kReflect, config );
    }

    void AnnotationLookup::reportUnknownTokens( const ReflectAnnotationDesc& desc, const string_view owner, const vector<string>& listUnknownToken )
    {
        for ( const string& token : listUnknownToken )
        {
            SW_LOG_ERROR_ID( "AstVisitor.ERRORUnknownTokenSpellings", "ERROR: %#() on '%#' has an unknown token '%#'. Spellings are listed in AnnotationMeta.txt [%#]; "
                                                                      "a free-form editor hint is written Meta = \"Key\" or Meta = \"Key=Value\".",
                             desc._pMacroName, owner, token, desc._pScope );
        }
    }
} // namespace sw
