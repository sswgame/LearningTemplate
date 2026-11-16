#include "pch.h"

#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsJSONDocument.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/JSON/JSONWriter.h"

namespace sw::editor
{
    namespace
    {
        struct ProjectSettingsJSONDocumentInternal
        {
            /** @brief 도구가 다시 쓰는 파일의 끝 이름입니다 — 손으로 고친 값은 다음 생성이 덮는다. */
            static constexpr const utf8* kGeneratedSuffix = ".generated.json";
            /** @brief 줄바꿈 없이 한 줄로 쓰는 들여쓰기 값입니다(`JSONWriter` 의 음수 들여쓰기). */
            static constexpr int32 kSingleLineIndent = -1;

            /** @brief 글의 첫 들여쓴 줄의 칸 수입니다(파이썬 `json.dump( indent )` · 저장소 JSON 의 모양). 줄바꿈이 없으면 한 줄(-1)입니다. */
            static int32 detectIndent( string_view text )
            {
                const size_t lineBreak = text.find( '\n' );
                if ( lineBreak == string_view::npos )
                    return kSingleLineIndent;
                int32 spaces = 0;
                for ( size_t index = lineBreak + 1; index < text.size() && text[index] == ' '; ++index )
                    ++spaces;
                return spaces;
            }

            /** @brief @p text 가 JSON 수 하나면 그 값입니다. 로그를 남기지 않는다(편집 칸이 글자마다 부른다). */
            static bool parseNumber( string_view text, JSONDocument& outDocument )
            {
                return text.empty() == false && outDocument.tryParse( text ) && outDocument.getRoot().isNumber();
            }

            /** @brief 노드 @p node 를 씁니다. 수 글이 JSON 수가 아니면(편집 검사 `setLeafText` 를 지나 오지 않는 자리) false 이고 글을 쓰지 않는다. */
            [[nodiscard]] static bool writeNode( const ProjectSettingsJSONDocument& document, uint32 node, JSONWriter& writer )
            {
                const ProjectSettingsJSONNode& value = document.getNode( node );
                switch ( value._type )
                {
                    case JSONType::Null:
                        writer.valueNull();
                        return true;
                    case JSONType::Bool:
                        writer.valueBool( value._text == "true" );
                        return true;
                    case JSONType::String:
                        writer.valueString( value._text );
                        return true;
                    case JSONType::Number:
                    {
                        // 수는 읽은 글을 다시 파싱해 그 값의 글로 쓴다 — 편집 검사와 같은 함수(`parseNumber`)다.
                        JSONDocument number;
                        if ( parseNumber( value._text, number ) == false )
                            return false;
                        writer.valueJSON( number.getRoot() );
                        return true;
                    }
                    case JSONType::Array:
                    {
                        writer.beginArray();
                        bool bWritten = true;
                        for ( const uint32 child : value._listChild )
                            bWritten = bWritten && writeNode( document, child, writer );
                        writer.endArray();
                        return bWritten;
                    }
                    case JSONType::Object:
                    {
                        writer.beginObject();
                        bool bWritten = true;
                        for ( const uint32 child : value._listChild )
                        {
                            writer.key( document.getNode( child )._key );
                            bWritten = bWritten && writeNode( document, child, writer );
                        }
                        writer.endObject();
                        return bWritten;
                    }
                }
                return false;
            }

            static bool isSpace( utf8 character ) { return character == ' ' || character == '\t' || character == '\n' || character == '\r'; }

            static size_t skipSpace( string_view text, size_t pos )
            {
                while ( pos < text.size() && isSpace( text[pos] ) )
                    ++pos;
                return pos;
            }

            /** @brief 여는 따옴표 @p pos 의 글 끝(닫는 따옴표 다음)입니다. 닫히지 않으면 npos 입니다. */
            static size_t skipString( string_view text, size_t pos )
            {
                for ( ++pos; pos < text.size(); ++pos )
                {
                    if ( text[pos] == '\\' )
                        ++pos;
                    else if ( text[pos] == '"' )
                        return pos + 1;
                }
                return string_view::npos;
            }

            /** @brief 수 · 참거짓 · null 글의 끝입니다. */
            static size_t skipScalar( string_view text, size_t pos )
            {
                while ( pos < text.size() && isSpace( text[pos] ) == false && text[pos] != ',' && text[pos] != ']' && text[pos] != '}' )
                    ++pos;
                return pos;
            }

            /** @brief 마지막 줄바꿈 뒤의 공백(@p text 안에 줄바꿈이 없으면 빈 글)입니다 — 그 줄의 들여쓰기다. */
            static string_view getIndentAfterLastBreak( string_view text )
            {
                const size_t lineBreak = text.rfind( '\n' );
                if ( lineBreak == string_view::npos )
                    return {};
                const string_view rest = text.substr( lineBreak + 1 );
                return rest.substr( 0, skipSpace( rest, 0 ) );
            }

            /** @brief @p pos 가 든 줄의 들여쓰기입니다. */
            static string_view getLineIndent( string_view text, size_t pos )
            {
                const size_t lineStart = pos == 0 ? string_view::npos : text.rfind( '\n', pos - 1 );
                const size_t begin     = lineStart == string_view::npos ? 0 : lineStart + 1;
                size_t       end       = begin;
                while ( end < pos && ( text[end] == ' ' || text[end] == '\t' ) )
                    ++end;
                return text.substr( begin, end - begin );
            }

            static string_view slice( string_view text, uint32 begin, uint32 end ) { return text.substr( begin, end - begin ); }

            static uint32 countLeaves( const ProjectSettingsJSONDocument& document, uint32 node )
            {
                const ProjectSettingsJSONNode& value = document.getNode( node );
                if ( value._type != JSONType::Array && value._type != JSONType::Object )
                    return 1;
                uint32 count = 0;
                for ( const uint32 child : value._listChild )
                    count += countLeaves( document, child );
                return count;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    ProjectSettingsJSONDocument::ProjectSettingsJSONDocument()
        : _listNode{}
        , _absolutePath{}
        , _state{}
        , _recordedUndoStepCount{ 0 }
        , _indent{ ProjectSettingsJSONDocumentInternal::kSingleLineIndent }
        , _readOnlyReason{ ProjectSettingsReadOnlyReason::None }
        , _bTrailingNewLine{ 0 }
    {
    }

    expected<void> ProjectSettingsJSONDocument::open( string_view absolutePath )
    {
        string text;
        if ( FileUtil::readTextFile( absolutePath, text ) == false )
        {
            _listNode.clear();
            return unexpected( Error{ ErrorKind::NotFound, "the settings file could not be read", absolutePath } );
        }
        return openText( absolutePath, text );
    }

    expected<void> ProjectSettingsJSONDocument::openText( string_view absolutePath, string_view text )
    {
        using Internal = ProjectSettingsJSONDocumentInternal;
        _listNode.clear();
        JSONDocument document;
        if ( document.parse( text, absolutePath ) == false ) // 틀린 자리(줄:열)는 parse 가 오류로 남긴다
            return unexpected( Error{ ErrorKind::InvalidArgument, "the settings file is not valid JSON", absolutePath } );
        _absolutePath     = string( absolutePath );
        _indent           = Internal::detectIndent( text );
        _bTrailingNewLine = StringUtil::endsWith( text, "\n" ) ? 1 : 0;
        _readOnlyReason   = StringUtil::endsWith( absolutePath, Internal::kGeneratedSuffix ) ? ProjectSettingsReadOnlyReason::Generated : ProjectSettingsReadOnlyReason::None;
        _sourceText       = string( text );
        (void)appendNode( document.getRoot(), {} ); // 첫 노드가 루트(`kRootNode`)다
        size_t sourcePos = 0;
        if ( mapSource( kRootNode, sourcePos ) == false || Internal::skipSpace( text, sourcePos ) != text.size() )
        {
            for ( ProjectSettingsJSONNode& node : _listNode )
                node._source = ProjectSettingsJSONSpan{}; // 범위 없이 쓴다 — 아래에서 그 글이 원문과 같은지 본다
        }
        const string writtenText = makeText();
        // 쓴 글이 읽은 글과 다르면(원문 범위를 찾지 못했고 다시 쓴 글도 그 모양이 아니다) 고치지 않는다 — 저장이 파일 모양을 조용히 바꾼다.
        if ( _readOnlyReason == ProjectSettingsReadOnlyReason::None && writtenText != text )
            _readOnlyReason = ProjectSettingsReadOnlyReason::LayoutNotPreserved;
        _state.reset( writtenText );
        return {};
    }

    expected<void> ProjectSettingsJSONDocument::revert()
    {
        const string path = _absolutePath;
        return open( path );
    }

    bool ProjectSettingsJSONDocument::saveDocument()
    {
        if ( isOpen() == false || isReadOnly() )
        {
            const utf8* pReason = isOpen() == false                                           ? "not open"
                                : _readOnlyReason == ProjectSettingsReadOnlyReason::Generated ? "generated by a tool"
                                                                                              : "laid out in a way the table page cannot keep";
            SW_LOG_ERROR_ID( "ProjectSettingsJSON.SaveRefused", "Project settings: %# is %# - not saved", _absolutePath, pReason );
            return false;
        }
        const string text = makeText();
        // 쓴 글을 다시 읽어 같은 트리인지 본다 — 쓰기기가 실패한 글(빈 글 · 잘린 글)로 설정 파일을 덮지 않는다.
        ProjectSettingsJSONDocument check;
        if ( text.empty() || check.openText( _absolutePath, text ).has_value() == false || check.makeText() != text )
        {
            SW_LOG_ERROR_ID( "ProjectSettingsJSON.SaveTextInvalid", "Project settings: the text made for %# does not read back the same - not saved", _absolutePath );
            return false;
        }
        if ( FileUtil::writeTextFile( _absolutePath, text ) == false )
        {
            SW_LOG_ERROR_ID( "ProjectSettingsJSON.SaveWriteFailed", "Project settings: %# could not be written", _absolutePath );
            return false;
        }
        _state.markSaved( text );
        return true;
    }

    bool ProjectSettingsJSONDocument::isModified() const
    {
        return isOpen() && _state.isDirty( makeText() );
    }

    string ProjectSettingsJSONDocument::makeText() const
    {
        if ( isOpen() == false )
            return {};
        string                         text;
        const ProjectSettingsJSONSpan& root = _listNode[kRootNode]._source;
        if ( root.isPresent() == false )
        {
            if ( appendFreshText( kRootNode, {}, text ) == false )
                return {};
            if ( _bTrailingNewLine != 0 )
                text += '\n';
            return text;
        }
        text.append( _sourceText, 0, root._valueBegin ); // 루트 앞뒤의 글(끝 줄바꿈 등)은 원문 그대로다
        if ( appendValueText( kRootNode, {}, text ) == false )
            return {};
        text.append( _sourceText, root._valueEnd, string::npos );
        return text;
    }

    uint32 ProjectSettingsJSONDocument::countLeaves() const
    {
        return isOpen() ? ProjectSettingsJSONDocumentInternal::countLeaves( *this, kRootNode ) : 0u;
    }

    bool ProjectSettingsJSONDocument::setLeafText( uint32 node, string_view text )
    {
        if ( isReadOnly() || node >= _listNode.size() )
            return false;
        const JSONType type = _listNode[node]._type;
        switch ( type )
        {
            case JSONType::String:
                break;
            case JSONType::Number:
            {
                JSONDocument number;
                if ( ProjectSettingsJSONDocumentInternal::parseNumber( text, number ) == false )
                    return false;
                break;
            }
            case JSONType::Bool:
                if ( text != "true" && text != "false" )
                    return false;
                break;
            case JSONType::Null:
            case JSONType::Array:
            case JSONType::Object:
                return false;
        }
        if ( _listNode[node]._text == text )
            return true; // 같은 값 — 되돌리기 단계를 만들지 않는다
        // 글 칸을 치는 동안은 칸 하나가 한 단계다(`endTextEdit` 가 끊는다). 수 · 불은 칸을 떠날 때 · 누를 때 한 번 오므로 늘 새 단계다
        const string coalesceKey = type == JSONType::String ? "text:" + to_string( static_cast<uint64>( node ) ) : string{};
        recordEdit( coalesceKey );
        _listNode[node]._text = string( text );
        return true;
    }

    optional<uint32> ProjectSettingsJSONDocument::addArrayItem( uint32 arrayNode )
    {
        if ( isReadOnly() || arrayNode >= _listNode.size() || _listNode[arrayNode]._type != JSONType::Array )
            return nullopt;
        recordEdit();
        uint32 item = 0;
        if ( _listNode[arrayNode]._listChild.empty() )
        {
            item = static_cast<uint32>( _listNode.size() );
            _listNode.push_back( ProjectSettingsJSONNode{ JSONType::String, {}, {}, {} } );
        }
        else
        {
            item = appendEmptyLike( _listNode[arrayNode]._listChild.back(), {} );
        }
        _listNode[arrayNode]._listChild.push_back( item );
        return item;
    }

    bool ProjectSettingsJSONDocument::addObjectMember( uint32 objectNode, string_view key )
    {
        if ( isReadOnly() || key.empty() || objectNode >= _listNode.size() || _listNode[objectNode]._type != JSONType::Object )
            return false;
        for ( const uint32 child : _listNode[objectNode]._listChild )
        {
            if ( _listNode[child]._key == key )
                return false;
        }
        recordEdit();
        const uint32 member = static_cast<uint32>( _listNode.size() );
        _listNode.push_back( ProjectSettingsJSONNode{ JSONType::String, string( key ), {}, {} } );
        _listNode[objectNode]._listChild.push_back( member );
        return true;
    }

    bool ProjectSettingsJSONDocument::removeChild( uint32 parentNode, size_t childIndex )
    {
        if ( isReadOnly() || parentNode >= _listNode.size() || childIndex >= _listNode[parentNode]._listChild.size() )
            return false;
        recordEdit();
        vector<uint32>& listChild = _listNode[parentNode]._listChild;
        listChild.erase( listChild.begin() + static_cast<ptrdiff_t>( childIndex ) );
        return true;
    }

    void ProjectSettingsJSONDocument::recordEdit( string_view coalesceKey )
    {
        if ( _state.record( _listNode, coalesceKey ) )
            ++_recordedUndoStepCount;
    }

    uint32 ProjectSettingsJSONDocument::appendNode( const JSONValue& value, string_view key )
    {
        const uint32 node = static_cast<uint32>( _listNode.size() );
        _listNode.push_back( ProjectSettingsJSONNode{ value.getType(), string( key ), {}, {} } );
        switch ( value.getType() )
        {
            case JSONType::Null:
                break;
            case JSONType::Bool:
            case JSONType::Number:
            {
                const expected<string> text = value.dump(); // dump 는 글의 UTF-8 만 검사해 지므로 수 · 참거짓은 지지 않는다
                SW_ASSERT( text.has_value() );
                _listNode[node]._text = text.has_value() ? *text : string{};
                break;
            }
            case JSONType::String:
                _listNode[node]._text = value.asString();
                break;
            case JSONType::Array:
                for ( size_t index = 0; index < value.size(); ++index )
                {
                    const uint32 child = appendNode( value.at( index ), {} );
                    _listNode[node]._listChild.push_back( child ); // push_back 이 배열을 옮길 수 있어 자식을 만든 뒤에 다시 찾는다
                }
                break;
            case JSONType::Object:
                for ( const string& memberName : value.getMemberNames() )
                {
                    const uint32 child = appendNode( value.get( memberName ), memberName );
                    _listNode[node]._listChild.push_back( child );
                }
                break;
        }
        _listNode[node]._textAtOpen      = _listNode[node]._text;
        _listNode[node]._listChildAtOpen = _listNode[node]._listChild;
        return node;
    }

    bool ProjectSettingsJSONDocument::mapSource( uint32 node, size_t& inoutPos )
    {
        using Internal         = ProjectSettingsJSONDocumentInternal;
        const string_view text = _sourceText;
        size_t            pos  = Internal::skipSpace( text, inoutPos );
        if ( pos >= text.size() )
            return false;
        // 아래 재귀는 노드 배열을 늘리지 않으므로 참조가 그대로다
        ProjectSettingsJSONNode& value   = _listNode[node];
        const bool               bObject = value._type == JSONType::Object;
        value._source._valueBegin        = static_cast<uint32>( pos );
        if ( bObject || value._type == JSONType::Array )
        {
            if ( text[pos] != ( bObject ? '{' : '[' ) )
                return false;
            pos = Internal::skipSpace( text, pos + 1 );
            for ( size_t index = 0; index < value._listChild.size(); ++index )
            {
                if ( index > 0 )
                {
                    if ( pos >= text.size() || text[pos] != ',' )
                        return false;
                    pos = Internal::skipSpace( text, pos + 1 );
                }
                ProjectSettingsJSONNode& child = _listNode[value._listChild[index]];
                child._source._memberBegin     = static_cast<uint32>( pos );
                if ( bObject )
                {
                    const size_t keyEnd = pos < text.size() && text[pos] == '"' ? Internal::skipString( text, pos ) : string_view::npos;
                    if ( keyEnd == string_view::npos )
                        return false;
                    const string_view rawKey = text.substr( pos + 1, keyEnd - pos - 2 );
                    if ( rawKey.find( '\\' ) == string_view::npos && rawKey != child._key ) // 겹친 키는 트리에 하나만 남아 차례가 어긋난다
                        return false;
                    child._source._keyEnd = static_cast<uint32>( keyEnd );
                    pos                   = Internal::skipSpace( text, keyEnd );
                    if ( pos >= text.size() || text[pos] != ':' )
                        return false;
                    ++pos;
                }
                if ( mapSource( value._listChild[index], pos ) == false )
                    return false;
                pos = Internal::skipSpace( text, pos );
            }
            if ( pos >= text.size() || text[pos] != ( bObject ? '}' : ']' ) )
                return false;
            ++pos;
        }
        else if ( value._type == JSONType::String )
        {
            pos = text[pos] == '"' ? Internal::skipString( text, pos ) : string_view::npos;
            if ( pos == string_view::npos )
                return false;
        }
        else
        {
            const size_t end = Internal::skipScalar( text, pos );
            if ( end == pos )
                return false;
            pos = end;
        }
        value._source._valueEnd = static_cast<uint32>( pos );
        inoutPos                = pos;
        return true;
    }

    bool ProjectSettingsJSONDocument::appendValueText( uint32 node, string_view lineIndent, string& inoutText ) const
    {
        using Internal                       = ProjectSettingsJSONDocumentInternal;
        const ProjectSettingsJSONNode& value = _listNode[node];
        const ProjectSettingsJSONSpan& span  = value._source;
        const string_view              text  = _sourceText;
        if ( span.isPresent() == false )
            return appendFreshText( node, lineIndent, inoutText );
        if ( value._type != JSONType::Array && value._type != JSONType::Object )
        {
            if ( value._text != value._textAtOpen )
                return appendFreshText( node, lineIndent, inoutText ); // 말단은 한 줄이다
            inoutText.append( Internal::slice( text, span._valueBegin, span._valueEnd ) );
            return true;
        }
        if ( value._listChild != value._listChildAtOpen )
            return appendChangedContainerText( node, inoutText );
        // 원소가 그대로면 원문을 따라 쓰되 자식 값 자리만 자식의 글로 바꾼다
        uint32 cursor = span._valueBegin;
        for ( const uint32 child : value._listChild )
        {
            const ProjectSettingsJSONSpan& childSpan = _listNode[child]._source;
            inoutText.append( Internal::slice( text, cursor, childSpan._valueBegin ) );
            if ( appendValueText( child, Internal::getLineIndent( text, childSpan._memberBegin ), inoutText ) == false )
                return false;
            cursor = childSpan._valueEnd;
        }
        inoutText.append( Internal::slice( text, cursor, span._valueEnd ) );
        return true;
    }

    bool ProjectSettingsJSONDocument::appendChangedContainerText( uint32 node, string& inoutText ) const
    {
        using Internal                       = ProjectSettingsJSONDocumentInternal;
        const ProjectSettingsJSONNode& value = _listNode[node];
        const ProjectSettingsJSONSpan& span  = value._source;
        const string_view              text  = _sourceText;
        if ( value._listChildAtOpen.empty() )
            return appendFreshText( node, Internal::getLineIndent( text, span._valueBegin ), inoutText ); // 원문에 본뜰 구분자가 없다
        const ProjectSettingsJSONSpan& first = _listNode[value._listChildAtOpen.front()]._source;
        const ProjectSettingsJSONSpan& last  = _listNode[value._listChildAtOpen.back()]._source;
        const string_view              lead  = Internal::slice( text, span._valueBegin + 1, first._memberBegin );
        const string_view              trail = Internal::slice( text, last._valueEnd, span._valueEnd - 1 );
        // 원소 사이 구분자는 원문의 첫 구분자다. 원소가 하나뿐이었으면 쉼표 뒤에 여는 괄호 뒤의 공백을 쓴다(`[\n    "a"\n  ]` → `,\n    `)
        const string      separator   = value._listChildAtOpen.size() > 1 ? string( Internal::slice( text, first._valueEnd, _listNode[value._listChildAtOpen[1]]._source._memberBegin ) )
                                                                          : "," + string( lead );
        const string_view childIndent = Internal::getIndentAfterLastBreak( lead );
        const bool        bObject     = value._type == JSONType::Object;
        const string_view colon       = bObject ? Internal::slice( text, first._keyEnd, first._valueBegin ) : string_view{};
        inoutText += bObject ? '{' : '[';
        for ( size_t index = 0; index < value._listChild.size(); ++index )
        {
            inoutText.append( index == 0 ? lead : string_view( separator ) );
            const ProjectSettingsJSONNode& child = _listNode[value._listChild[index]];
            if ( bObject && child._source.isPresent() )
            {
                inoutText.append( Internal::slice( text, child._source._memberBegin, child._source._valueBegin ) );
            }
            else if ( bObject )
            {
                JSONWriter keyWriter( inoutText, Internal::kSingleLineIndent );
                keyWriter.valueString( child._key );
                inoutText.append( colon );
            }
            if ( appendValueText( value._listChild[index], childIndent, inoutText ) == false )
                return false;
        }
        if ( value._listChild.empty() == false )
            inoutText.append( trail );
        inoutText += bObject ? '}' : ']';
        return true;
    }

    bool ProjectSettingsJSONDocument::appendFreshText( uint32 node, string_view lineIndent, string& inoutText ) const
    {
        string     fresh;
        JSONWriter writer( fresh, _indent );
        if ( ProjectSettingsJSONDocumentInternal::writeNode( *this, node, writer ) == false || writer.isComplete() == false )
            return false;
        // 쓰기기는 0 칸에서 들여 쓴다 — 줄마다 놓일 자리의 들여쓰기를 앞에 붙인다
        for ( const utf8 character : fresh )
        {
            inoutText += character;
            if ( character == '\n' )
                inoutText.append( lineIndent );
        }
        return true;
    }

    uint32 ProjectSettingsJSONDocument::appendEmptyLike( uint32 sampleNode, string_view key )
    {
        const ProjectSettingsJSONNode sample = _listNode[sampleNode]; // 복사 — 아래 push_back 이 배열을 옮긴다
        const uint32                  node   = static_cast<uint32>( _listNode.size() );
        _listNode.push_back( ProjectSettingsJSONNode{ sample._type, string( key ), {}, {} } );
        switch ( sample._type )
        {
            case JSONType::Number:
                _listNode[node]._text = "0";
                break;
            case JSONType::Bool:
                _listNode[node]._text = "false";
                break;
            case JSONType::Object:
                for ( const uint32 sampleChild : sample._listChild )
                {
                    const string childKey = _listNode[sampleChild]._key; // 복사 — 재귀의 push_back 이 배열을 옮긴다
                    const uint32 child    = appendEmptyLike( sampleChild, childKey );
                    _listNode[node]._listChild.push_back( child );
                }
                break;
            case JSONType::Null:
            case JSONType::String:
            case JSONType::Array:
                break;
        }
        return node;
    }
} // namespace sw::editor
