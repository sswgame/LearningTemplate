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
        (void)appendNode( document.getRoot(), {} ); // 첫 노드가 루트(`kRootNode`)다
        const string writtenText = makeText();
        // 쓰기가 읽은 글을 그대로 되살리지 못하면 고치지 않는다 — 저장이 손으로 맞춘 모양(한 줄 객체 등)을 조용히 바꾼다.
        // 모양을 지키며 고치려면 VS Code 의 settings.json 편집(jsonc-parser modify)처럼 바뀐 글 범위만 고친다(NEXT).
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
                                                                                              : "laid out by hand in a way the table page cannot keep";
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
        string     text;
        JSONWriter writer( text, _indent );
        const bool bWritten = ProjectSettingsJSONDocumentInternal::writeNode( *this, kRootNode, writer );
        if ( bWritten == false || writer.isComplete() == false )
            return {};
        if ( _bTrailingNewLine != 0 )
            text += '\n';
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
        ProjectSettingsJSONNode& value = _listNode[node];
        switch ( value._type )
        {
            case JSONType::String:
                value._text = string( text );
                return true;
            case JSONType::Number:
            {
                JSONDocument number;
                if ( ProjectSettingsJSONDocumentInternal::parseNumber( text, number ) == false )
                    return false;
                value._text = string( text );
                return true;
            }
            case JSONType::Bool:
                if ( text != "true" && text != "false" )
                    return false;
                value._text = string( text );
                return true;
            case JSONType::Null:
            case JSONType::Array:
            case JSONType::Object:
                return false;
        }
        return false;
    }

    optional<uint32> ProjectSettingsJSONDocument::addArrayItem( uint32 arrayNode )
    {
        if ( isReadOnly() || arrayNode >= _listNode.size() || _listNode[arrayNode]._type != JSONType::Array )
            return nullopt;
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
        const uint32 member = static_cast<uint32>( _listNode.size() );
        _listNode.push_back( ProjectSettingsJSONNode{ JSONType::String, string( key ), {}, {} } );
        _listNode[objectNode]._listChild.push_back( member );
        return true;
    }

    bool ProjectSettingsJSONDocument::removeChild( uint32 parentNode, size_t childIndex )
    {
        if ( isReadOnly() || parentNode >= _listNode.size() || childIndex >= _listNode[parentNode]._listChild.size() )
            return false;
        vector<uint32>& listChild = _listNode[parentNode]._listChild;
        listChild.erase( listChild.begin() + static_cast<ptrdiff_t>( childIndex ) );
        return true;
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
        return node;
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
