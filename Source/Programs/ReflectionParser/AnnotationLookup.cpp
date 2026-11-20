#include "pch.h"

#include "Programs/ReflectionParser/AnnotationLookup.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Programs/ReflectionParser/CursorReader.h"

namespace sw
{
    struct AnnotationLookup::AttrSearch
    {
        string_view _prefix;
        string      _spelling;
    };

    /** @brief 어휘 부모의 자식을 차례로 보며 @p _target 바로 앞 형제를 찾습니다. */
    struct AnnotationLookup::SiblingSearch
    {
        CXCursor               _target;
        CXCursor               _previous;
        CXCursor               _found;
        uint8                  _bHasPrevious : 1;
        uint8                  _bFound       : 1;
        [[maybe_unused]] uint8 _reserved     : 6;
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

    CXChildVisitResult AnnotationLookup::stringLiteralVisitor( CXCursor cursor, CXCursor, CXClientData data )
    {
        if ( clang_getCursorKind( cursor ) != CXCursor_StringLiteral )
            return CXChildVisit_Continue;
        *static_cast<string*>( data ) = CursorReader::getCursorSpelling( cursor );
        return CXChildVisit_Break;
    }

    string AnnotationLookup::unescapeLiteralSpelling( const string_view spelling )
    {
        // clang `StringLiteral::outputString` 의 거꾸로다: 앞뒤 따옴표, `\\` `\"` `\a\b\f\n\r\t\v`, 인쇄할 수 없는 바이트는 `\ooo`(팔진 셋).
        const bool bQuoted = spelling.size() >= 2 && spelling.front() == '"' && spelling.back() == '"';
        if ( bQuoted == false )
            return {};
        const string_view body = spelling.substr( 1, spelling.size() - 2 );
        string            result;
        result.reserve( body.size() );
        for ( size_t index = 0; index < body.size(); ++index )
        {
            const utf8 character = body[index];
            if ( character != '\\' )
            {
                result.push_back( character );
                continue;
            }
            if ( ++index >= body.size() )
                return {};
            const utf8 escaped = body[index];
            switch ( escaped )
            {
                case 'a':
                    result.push_back( '\a' );
                    break;
                case 'b':
                    result.push_back( '\b' );
                    break;
                case 'f':
                    result.push_back( '\f' );
                    break;
                case 'n':
                    result.push_back( '\n' );
                    break;
                case 'r':
                    result.push_back( '\r' );
                    break;
                case 't':
                    result.push_back( '\t' );
                    break;
                case 'v':
                    result.push_back( '\v' );
                    break;
                default:
                {
                    const bool bOctal = escaped >= '0' && escaped <= '7';
                    if ( bOctal == false )
                    {
                        result.push_back( escaped ); // `\\` · `\"` · `\'`
                        break;
                    }
                    uint32 value  = 0;
                    size_t digits = 0;
                    while ( digits < 3 && index < body.size() && body[index] >= '0' && body[index] <= '7' )
                    {
                        value = value * 8 + static_cast<uint32>( body[index] - '0' );
                        ++index;
                        ++digits;
                    }
                    --index;
                    result.push_back( static_cast<utf8>( value & 0xFFu ) );
                    break;
                }
            }
        }
        return result;
    }

    string AnnotationLookup::readMarker( const CXCursor cursor, const ReflectAnnotationDesc& desc )
    {
        if ( clang_getCursorKind( cursor ) != CXCursor_StaticAssert )
            return {};
        string literal;
        clang_visitChildren( cursor, stringLiteralVisitor, &literal );
        string spelling = unescapeLiteralSpelling( literal );
        if ( StringUtil::startsWith( spelling, desc._pPrefix ) == false )
            return {};
        return spelling;
    }

    CXChildVisitResult AnnotationLookup::siblingSearchVisitor( CXCursor cursor, CXCursor, CXClientData data )
    {
        SiblingSearch* pSearch = static_cast<SiblingSearch*>( data );
        if ( clang_equalCursors( cursor, pSearch->_target ) != 0 )
        {
            pSearch->_bFound = SW_TRUE;
            if ( pSearch->_bHasPrevious == SW_TRUE )
                pSearch->_found = pSearch->_previous;
            return CXChildVisit_Break;
        }
        pSearch->_previous     = cursor;
        pSearch->_bHasPrevious = SW_TRUE;
        return CXChildVisit_Continue;
    }

    string AnnotationLookup::findPreviousSiblingMarker( const CXCursor declaration, const ReflectAnnotationDesc& desc )
    {
        if ( clang_Cursor_isNull( declaration ) != 0 )
            return {};
        const CXCursor definition = clang_getCursorDefinition( declaration );
        SiblingSearch  search{};
        search._target       = clang_Cursor_isNull( definition ) == 0 ? definition : declaration;
        search._previous     = clang_getNullCursor();
        search._found        = clang_getNullCursor();
        search._bHasPrevious = SW_FALSE;
        search._bFound       = SW_FALSE;
        clang_visitChildren( clang_getCursorLexicalParent( search._target ), siblingSearchVisitor, &search );
        if ( search._bFound == SW_FALSE || clang_Cursor_isNull( search._found ) != 0 )
            return {};
        return readMarker( search._found, desc );
    }

    CXChildVisitResult AnnotationLookup::bodyMarkerVisitor( CXCursor cursor, CXCursor, CXClientData data )
    {
        if ( clang_getCursorKind( cursor ) != CXCursor_CXXMethod || CursorReader::isStringEqual( clang_getCursorSpelling( cursor ), annotation::kReflectBodyMarkerFn ) == false )
            return CXChildVisit_Continue;
        *static_cast<bool*>( data ) = findAnnotateAttr( cursor, annotation::kReflectBodyPrefix ).empty() == false;
        return CXChildVisit_Break;
    }

    bool AnnotationLookup::isReflectedRecord( const CXCursor declaration )
    {
        if ( clang_Cursor_isNull( declaration ) != 0 )
            return false;
        const CXCursor definition = clang_getCursorDefinition( declaration );
        if ( clang_Cursor_isNull( definition ) != 0 )
            return false; // 정의를 못 보는 선언(전방 선언만)은 본문 표식을 볼 수 없다
        bool bBody = false;
        clang_visitChildren( definition, bodyMarkerVisitor, &bBody );
        return bBody;
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
