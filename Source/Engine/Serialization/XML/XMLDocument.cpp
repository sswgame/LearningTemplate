#include "pch.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Resource/ResourceIO.h"

#include <pugixml.hpp>

namespace sw
{
    namespace
    {
        /// @brief 에셋 XML 저장 포맷입니다. 줄 접기는 pugixml 이 아니라 wrapLongElementLines 가 합니다.
        inline constexpr uint32 kXMLSaveFormat = pugi::format_default | pugi::format_no_declaration;

        /// @brief 이 길이를 넘는 요소 줄만 속성 단위로 접습니다.
        inline constexpr size_t kXMLWrapColumn = 120;

        /**
         * @brief 시작 태그의 끝(따옴표 밖의 첫 `>`) 위치입니다. 없으면 npos 입니다.
         * @details 접는 것은 **시작 태그의 속성뿐**이다. 그 뒤는 요소 텍스트인데, pugixml 은 텍스트 안의 `"` 를 이스케이프하지 않는다 —
         *          줄 끝까지 따옴표를 세면 `<item>The "Fire" and "Ice" …</item>` 같은 텍스트 속 공백에서 줄을 접게 되고, 읽을 때는
         *          양 끝만 다듬으므로 그 줄바꿈 · 들여쓰기가 값에 영영 남는다(씬 · 직렬화한 문자열 목록 · 맵 값이 모두 이 길로 저장된다).
         */
        size_t findStartTagEnd( string_view line, size_t from )
        {
            bool bInQuotes = false;
            for ( size_t charIndex = from; charIndex < line.size(); ++charIndex )
            {
                if ( line[charIndex] == '"' )
                    bInQuotes = !bInQuotes;
                else if ( line[charIndex] == '>' && bInQuotes == false )
                    return charIndex;
            }
            return string_view::npos;
        }

        /**
         * @brief 한 줄에 담긴 속성들의 끝 위치를 모읍니다. 부르는 쪽이 줄을 **시작 태그까지** 잘라 넘깁니다(`findStartTagEnd`).
         * @details 속성 값 안에도 공백이 있으므로 따옴표 밖의 공백만 경계로 셉니다. 값 안의 따옴표는
         *          XML 이 `&quot;` 로 이스케이프하므로 따옴표 쌍만 세면 안전합니다 — 속성 안에서는. 요소 텍스트의 따옴표는
         *          이스케이프되지 않으므로 거기까지 세면 안 됩니다.
         * @param outListEnd 각 속성의 끝 위치(반열림). 첫 원소가 첫 속성의 끝입니다.
         */
        void collectAttributeEnds( string_view line, size_t from, vector<size_t>& outListEnd )
        {
            bool bInQuotes = false;
            for ( size_t charIndex = from; charIndex < line.size(); ++charIndex )
            {
                if ( line[charIndex] != '"' )
                    continue;
                bInQuotes = !bInQuotes;
                if ( bInQuotes == false )
                    outListEnd.push_back( charIndex + 1 );
            }
        }

        /**
         * @brief 너무 긴 요소 줄을 속성 단위로 접습니다.
         * @details 리플렉션 직렬화는 스칼라를 속성으로 씁니다. 필드가 20개면 속성 20개짜리 요소가
         *          한 줄이 되어 1,000자를 넘고, 읽기도 어렵고 한 글자만 고쳐도 diff 가 줄 전체를
         *          바뀐 것으로 표시합니다.
         *
         *          pugixml 의 `format_indent_attributes` 는 태그 이름만 남기고 속성을 모두 아래로
         *          내려서, 짧은 요소까지 여러 줄로 흩어지고 무슨 요소인지 눈에 덜 들어옵니다.
         *          그래서 직접 접습니다. **짧은 줄은 그대로 두고**, 긴 줄만 첫 속성을 태그 옆에
         *          남긴 채 나머지를 한 줄에 하나씩 내립니다.
         *
         *              <RenderGraphPassDesc _name="Shadow"
         *                  _type="Shadow"
         *                  _depthAttachment="ShadowMap" />
         */
        string wrapLongElementLines( string_view xml )
        {
            string result;
            result.reserve( xml.size() + xml.size() / 4 );

            vector<size_t> listAttrEnd;
            size_t         lineStart = 0;
            while ( lineStart <= xml.size() )
            {
                size_t lineEnd = xml.find( '\n', lineStart );
                if ( lineEnd == string_view::npos )
                    lineEnd = xml.size();

                const string_view line      = xml.substr( lineStart, lineEnd - lineStart );
                const bool        bLastLine = ( lineEnd >= xml.size() );

                size_t indentLen = 0;
                while ( indentLen < line.size() && ( line[indentLen] == '\t' || line[indentLen] == ' ' ) )
                {
                    ++indentLen;
                }

                // 접을 대상: 충분히 길고, 여는 태그이며, 속성이 둘 이상인 줄.
                const bool bOpenTag = ( indentLen + 1 < line.size() ) && line[indentLen] == '<' &&
                                      line[indentLen + 1] != '/' && line[indentLen + 1] != '!' && line[indentLen + 1] != '?';
                const size_t firstSpace = line.find( ' ', indentLen );
                const size_t tagEnd     = bOpenTag ? findStartTagEnd( line, indentLen ) : string_view::npos;

                // 속성은 시작 태그 안에서만 찾는다(`findStartTagEnd` 설명). 태그 이름 뒤 첫 공백이 태그 밖이면 속성이 없는 요소다.
                listAttrEnd.clear();
                const bool bWrapAttributes = line.size() > kXMLWrapColumn && bOpenTag && firstSpace != string_view::npos && tagEnd != string_view::npos && firstSpace < tagEnd;
                if ( bWrapAttributes )
                    collectAttributeEnds( line.substr( 0, tagEnd ), firstSpace, listAttrEnd );

                if ( listAttrEnd.size() < 2 )
                {
                    result.append( line.data(), line.size() );
                    if ( bLastLine == false )
                        result.push_back( '\n' );
                    lineStart = lineEnd + 1;
                    continue;
                }

                // 태그 + 첫 속성은 같은 줄에 둔다. 무슨 요소인지가 먼저 보여야 한다.
                // 길이를 함께 넘기므로 널 종단이 필요 없다(검사기가 (ptr, count) 오버로드를 모른다).
                // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
                result.append( line.data(), listAttrEnd[0] );

                // 이어지는 속성은 **첫 속성과 같은 열**에 세운다. `<TagName ` 만큼의 폭을 들여쓰기
                // 뒤부터 측정해서 공백으로 메우므로, 탭 폭이 몇이든 세로줄이 맞는다.
                const size_t alignColumn = firstSpace + 1 - indentLen;

                for ( size_t attrIndex = 1; attrIndex < listAttrEnd.size(); ++attrIndex )
                {
                    size_t tokenStart = listAttrEnd[attrIndex - 1];
                    while ( tokenStart < line.size() && line[tokenStart] == ' ' )
                    {
                        ++tokenStart;
                    }

                    result.push_back( '\n' );
                    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) — 길이를 함께 넘긴다(위 첫 속성과 같다)
                    result.append( line.data(), indentLen );
                    result.append( alignColumn, ' ' );
                    result.append( line.data() + tokenStart, listAttrEnd[attrIndex] - tokenStart );
                }

                // 마지막 속성 뒤의 꼬리( "/>" 또는 ">" )는 그대로 붙인다.
                const size_t tailStart = listAttrEnd.back();
                if ( tailStart < line.size() )
                    result.append( line.data() + tailStart, line.size() - tailStart );

                if ( bLastLine == false )
                    result.push_back( '\n' );
                lineStart = lineEnd + 1;
            }

            return result;
        }

        struct XMLStringWriter : pugi::xml_writer
        {
            string _result;

            void write( const void* pData, size_t size ) override
            {
                _result.append( static_cast<const utf8*>( pData ), size );
            }
        };

        struct XMLDocumentInternal
        {
            /**
             * @brief pugixml 의 전역 할당 함수를 sw 할당자(`Memory::allocate` · `Memory::free`)로 바꿉니다. 처음 부를 때 한 번만 하고 되돌리지 않습니다.
             * @details pugixml 의 할당 함수는 프로세스 전역(pugixml 라이브러리 하나)입니다. 바꾸기 전에 잡은 블록을 바꾼 뒤에 풀면 sw 할당자가 남의 블록을
             *          받으므로, 문서 트리가 하나라도 생기기 전에(`XMLDocument::Impl` 의 첫 멤버) 부릅니다. 엔진에서 pugixml 을 쓰는 곳은 이 파일뿐입니다.
             *          되돌리지 않는 이유도 같습니다 — 종료 중에 풀리는 문서가 CRT `free` 로 sw 블록을 풀게 됩니다.
             */
            static bool installSwAllocator()
            {
                static const bool s_bInstalled = setSwAllocator();
                return s_bInstalled;
            }

            static bool setSwAllocator()
            {
                pugi::set_memory_management_functions( &Memory::allocate, &Memory::free );
                return true;
            }

            static pugi::xml_node asNode( void* pPtr )
            {
                return pugi::xml_node{ static_cast<pugi::xml_node_struct*>( pPtr ) };
            }

            static pugi::xml_attribute asAttribute( void* pPtr )
            {
                return pugi::xml_attribute{ static_cast<pugi::xml_attribute_struct*>( pPtr ) };
            }

            // 이름은 철자 그대로 찾는다(XMLNode 머리말) — pugixml 의 child · attribute 가 그 비교다.
            static pugi::xml_node findChild( pugi::xml_node parent, const utf8* pName )
            {
                if ( parent.empty() || pName == nullptr )
                    return {};
                return parent.child( pName );
            }

            static pugi::xml_node findSibling( pugi::xml_node node, const utf8* pName )
            {
                if ( node.empty() || pName == nullptr )
                    return {};
                for ( pugi::xml_node sibling = node; sibling.empty() == false; sibling = sibling.next_sibling() )
                {
                    if ( StringUtil::equals( sibling.name(), pName ) )
                        return sibling;
                }
                return {};
            }

            static pugi::xml_attribute findAttr( pugi::xml_node node, const utf8* pName )
            {
                if ( node.empty() || pName == nullptr )
                    return {};
                return node.attribute( pName );
            }

            /** @brief 값 글 하나를 타입으로 읽습니다. 글이 타입이 아니면 false 입니다(없음은 부르는 쪽이 nullptr 로 먼저 가른다). */
            [[nodiscard]] static bool parseText( const utf8* pText, int32& outValue ) { return StringUtil::parseInt( StringUtil::trim( pText ), outValue ); }
            [[nodiscard]] static bool parseText( const utf8* pText, uint32& outValue )
            {
                uint64     wide{ 0 };
                const bool bParsed = StringUtil::parseUint64( StringUtil::trim( pText ), wide ) && wide <= MathUtil::kMaxUInt32;
                outValue           = static_cast<uint32>( wide );
                return bParsed;
            }
            [[nodiscard]] static bool parseText( const utf8* pText, float32& outValue ) { return StringUtil::parseFloat( StringUtil::trim( pText ), outValue ); }
            [[nodiscard]] static bool parseText( const utf8* pText, bool& outValue ) { return StringUtil::tryParseBool( StringUtil::trim( pText ), outValue ); }
            [[nodiscard]] static bool parseText( const utf8* pText, string& outValue )
            {
                outValue = pText;
                return true;
            }

            static const utf8* getTypeLabel( const int32& ) { return "integer"; }
            static const utf8* getTypeLabel( const uint32& ) { return "unsigned integer"; }
            static const utf8* getTypeLabel( const float32& ) { return "number"; }
            static const utf8* getTypeLabel( const bool& ) { return "boolean"; }
            static const utf8* getTypeLabel( const string& ) { return "text"; }
        };

        /** @brief 속성 · 자식 글 하나를 읽고, 깨졌으면 원소 · 이름 · 글과 함께 오류를 남깁니다. 결과는 0 읽음 · 1 없음 · 2 깨짐입니다. */
        template <typename T>
        uint8 readNodeValueText( const utf8* pElementName, const utf8* pKind, const utf8* pID, const utf8* pName, const utf8* pText, T& outValue )
        {
            if ( pText == nullptr )
                return 1;
            if ( XMLDocumentInternal::parseText( pText, outValue ) )
                return 0;
            SW_LOG_WRITE( sw::LogLevel::Error, pID, "<%#> %# '%#' is not a %#: '%#'", pElementName, pKind, pName, XMLDocumentInternal::getTypeLabel( outValue ), pText );
            return 2;
        }
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "XMLDocument" );

    struct XMLDocument::Impl
    {
        bool               _bSwAllocator{ XMLDocumentInternal::installSwAllocator() }; ///< `doc` 보다 먼저 선다(할당 함수를 먼저 바꾼다)
        pugi::xml_document doc;
    };

    const utf8* XMLAttribute::getName() const
    {
        const pugi::xml_attribute attr = XMLDocumentInternal::asAttribute( _pAttr );
        return attr.name();
    }

    const utf8* XMLAttribute::getValue() const
    {
        const pugi::xml_attribute attr = XMLDocumentInternal::asAttribute( _pAttr );
        return attr.value();
    }

    XMLAttribute XMLAttribute::getNext() const
    {
        const pugi::xml_attribute attr = XMLDocumentInternal::asAttribute( _pAttr );
        if ( attr.empty() )
            return {};
        return XMLAttribute{ attr.next_attribute().internal_object() };
    }

    const utf8* XMLNode::getName() const
    {
        const pugi::xml_node pNode = XMLDocumentInternal::asNode( _pNode );
        return pNode.name();
    }

    const utf8* XMLNode::getText() const
    {
        const pugi::xml_node pNode = XMLDocumentInternal::asNode( _pNode );
        return pNode.child_value();
    }

    int64 XMLNode::getSourceOffset() const
    {
        const pugi::xml_node pNode = XMLDocumentInternal::asNode( _pNode );
        if ( pNode.empty() )
            return -1;
        return static_cast<int64>( pNode.offset_debug() );
    }

    const utf8* XMLNode::findAttribute( const utf8* pName ) const
    {
        const pugi::xml_node pNode = XMLDocumentInternal::asNode( _pNode );
        if ( pNode.empty() || pName == nullptr )
            return nullptr;
        const pugi::xml_attribute pAttr = XMLDocumentInternal::findAttr( pNode, pName );
        if ( pAttr.empty() == false )
            return pAttr.value();
        return nullptr;
    }

    string_view XMLNode::getAttributeText( const utf8* pName ) const
    {
        const utf8* pValue = findAttribute( pName );
        return ( pValue != nullptr ) ? string_view( pValue ) : string_view{};
    }

#define SW_XML_NODE_VALUE_READERS( Type )                                                                                                                      \
    XMLNode::XMLValueRead XMLNode::parseAttributeValue( const utf8* pName, Type& outValue ) const                                                              \
    {                                                                                                                                                          \
        return static_cast<XMLValueRead>( readNodeValueText( getName(), "attribute", "Xml.AttributeTypeMismatch", pName, findAttribute( pName ), outValue ) ); \
    }                                                                                                                                                          \
    XMLNode::XMLValueRead XMLNode::parseChildValue( const utf8* pName, Type& outValue ) const                                                                  \
    {                                                                                                                                                          \
        return static_cast<XMLValueRead>( readNodeValueText( getName(), "child", "Xml.ChildTypeMismatch", pName, findChildText( pName ), outValue ) );         \
    }
    SW_XML_NODE_VALUE_READERS( int32 )
    SW_XML_NODE_VALUE_READERS( uint32 )
    SW_XML_NODE_VALUE_READERS( float32 )
    SW_XML_NODE_VALUE_READERS( bool )
    SW_XML_NODE_VALUE_READERS( string )
#undef SW_XML_NODE_VALUE_READERS

    void XMLNode::reportMissing( const utf8* pKind, const utf8* pName ) const
    {
        SW_LOG_ERROR( "<%#> has no required %# '%#'", getName(), pKind, pName );
    }

    void XMLNode::reportOutOfRange( const utf8* pName, float64 value, float64 minValue, float64 maxValue ) const
    {
        SW_LOG_ERROR_ID( "Xml.AttributeOutOfRange", "<%# %#=\"%#\"> is outside [%#, %#]", getName(), pName, value, minValue, maxValue );
    }

    XMLNode XMLNode::findChild( const utf8* pName ) const
    {
        const pugi::xml_node pNode = XMLDocumentInternal::asNode( _pNode );
        if ( pNode.empty() )
            return {};
        if ( pName == nullptr )
            return XMLNode{ pNode.first_child().internal_object() };
        return XMLNode{ XMLDocumentInternal::findChild( pNode, pName ).internal_object() };
    }

    XMLNode XMLNode::findNextSibling( const utf8* pName ) const
    {
        const pugi::xml_node pNode = XMLDocumentInternal::asNode( _pNode );
        if ( pNode.empty() )
            return {};
        const pugi::xml_node pSibling = pNode.next_sibling();
        if ( pName == nullptr )
            return XMLNode{ pSibling.internal_object() };
        return XMLNode{ XMLDocumentInternal::findSibling( pSibling, pName ).internal_object() };
    }

    const utf8* XMLNode::findChildText( const utf8* pName ) const
    {
        const XMLNode childNode = findChild( pName );
        if ( childNode.isValid() == false )
            return nullptr;
        return childNode.getText();
    }

    bool XMLNode::takeChildText( const utf8* pName, string& dst ) const
    {
        const utf8* pValue = findChildText( pName );
        if ( StringUtil::isNullOrEmpty( pValue ) )
            return false;
        dst = pValue;
        return true;
    }

    XMLAttribute XMLNode::getFirstAttribute() const
    {
        const pugi::xml_node pNode = XMLDocumentInternal::asNode( _pNode );
        if ( pNode.empty() )
            return {};
        return XMLAttribute{ pNode.first_attribute().internal_object() };
    }

    XMLNode XMLNode::appendChild( const utf8* pName ) const
    {
        pugi::xml_node pNode = XMLDocumentInternal::asNode( _pNode );
        if ( pNode.empty() || pName == nullptr )
            return {};
        pugi::xml_node pChild = pNode.append_child( pName );
        return XMLNode{ pChild.internal_object() };
    }

    void XMLNode::appendAttribute( const utf8* pName, const utf8* pValue ) const
    {
        pugi::xml_node pNode = XMLDocumentInternal::asNode( _pNode );
        if ( pNode.empty() || pName == nullptr )
            return;
        pugi::xml_attribute pAttr = pNode.append_attribute( pName );
        pAttr.set_value( pValue != nullptr ? pValue : "" );
    }

    void XMLNode::appendAttribute( const utf8* pName, string_view value ) const
    {
        pugi::xml_node pNode = XMLDocumentInternal::asNode( _pNode );
        if ( pNode.empty() || pName == nullptr )
            return;
        pugi::xml_attribute pAttr = pNode.append_attribute( pName );
        if ( value.empty() )
        {
            pAttr.set_value( "" );
        }
        else
        {
            const string valStr( value );
            pAttr.set_value( valStr.c_str() );
        }
    }

    void XMLNode::appendAttribute( const utf8* pName, bool value ) const
    {
        appendAttribute( pName, value ? "1" : "0" );
    }

    void XMLNode::setAttribute( const utf8* pName, const utf8* pValue ) const
    {
        pugi::xml_node pNode = XMLDocumentInternal::asNode( _pNode );
        if ( pNode.empty() || pName == nullptr )
            return;
        pugi::xml_attribute pAttr = XMLDocumentInternal::findAttr( pNode, pName );
        if ( pAttr.empty() == false )
            pAttr.set_value( pValue != nullptr ? pValue : "" );
        else
            appendAttribute( pName, pValue );
    }

    void XMLNode::setAttribute( const utf8* pName, string_view value ) const
    {
        pugi::xml_node pNode = XMLDocumentInternal::asNode( _pNode );
        if ( pNode.empty() || pName == nullptr )
            return;
        pugi::xml_attribute pAttr = XMLDocumentInternal::findAttr( pNode, pName );
        if ( pAttr.empty() == false )
        {
            const string valStr( value );
            pAttr.set_value( valStr.c_str() );
        }
        else
        {
            appendAttribute( pName, value );
        }
    }

    void XMLNode::setAttribute( const utf8* pName, bool value ) const
    {
        setAttribute( pName, value ? "1" : "0" );
    }

    void XMLNode::setName( const utf8* pName ) const
    {
        pugi::xml_node pNode = XMLDocumentInternal::asNode( _pNode );
        if ( pNode.empty() || pName == nullptr )
            return;
        pNode.set_name( pName );
    }

    void XMLNode::setValue( const utf8* pValue ) const
    {
        pugi::xml_node pNode = XMLDocumentInternal::asNode( _pNode );
        if ( pNode.empty() )
            return;
        pNode.text().set( pValue != nullptr ? pValue : "" );
    }

    void XMLNode::setValue( string_view value ) const
    {
        pugi::xml_node pNode = XMLDocumentInternal::asNode( _pNode );
        if ( pNode.empty() )
            return;
        const string valStr( value );
        pNode.text().set( valStr.c_str() );
    }

    void XMLNode::setValue( bool value ) const
    {
        setValue( value ? "1" : "0" );
    }

    string XMLNode::toString() const
    {
        const pugi::xml_node pNode = XMLDocumentInternal::asNode( _pNode );
        if ( pNode.empty() )
            return {};
        XMLStringWriter writer;
        pNode.print( writer, "\t", kXMLSaveFormat );
        return wrapLongElementLines( writer._result );
    }

    XMLNode XMLNode::appendClone( XMLNode src ) const
    {
        pugi::xml_node pNode = XMLDocumentInternal::asNode( _pNode );
        pugi::xml_node pSrc  = XMLDocumentInternal::asNode( src._pNode );
        if ( pNode.empty() || pSrc.empty() )
            return {};
        pugi::xml_node cloned = pNode.append_copy( pSrc );
        return XMLNode{ cloned.internal_object() };
    }

    XMLDocument::XMLDocument()
        : _impl{ make_unique<Impl>() }
        , _bLastErrorNotFound{ false }
    {
    }

    XMLDocument::~XMLDocument() = default;

    XMLDocument::XMLDocument( XMLDocument&& other ) noexcept
        : _impl{ std::move( other._impl ) }
        , _lastError{ std::move( other._lastError ) }
        , _bLastErrorNotFound{ other._bLastErrorNotFound }
    {
    }

    XMLDocument& XMLDocument::operator=( XMLDocument&& other ) noexcept
    {
        if ( this != &other )
        {
            _impl               = std::move( other._impl );
            _lastError          = std::move( other._lastError );
            _bLastErrorNotFound = other._bLastErrorNotFound;
        }
        return *this;
    }

    void XMLDocument::clear()
    {
        if ( _impl == nullptr )
            _impl = make_unique<Impl>();
        else
            _impl->doc.reset();
    }

    bool XMLDocument::parse( string_view xmlText, string_view sourceName )
    {
        if ( _impl == nullptr )
            _impl = make_unique<Impl>();
        _impl->doc.reset();
        _lastError.clear();
        _bLastErrorNotFound      = false;
        const string_view source = sourceName.empty() ? string_view{ "<memory>" } : sourceName;
        if ( xmlText.empty() )
        {
            _lastError = string( source ) + ": empty document";
            return false;
        }

        const pugi::xml_parse_result result = _impl->doc.load_buffer(
            xmlText.data(),
            xmlText.size(),
            pugi::parse_default | pugi::parse_trim_pcdata );

        if ( result.status != pugi::status_ok )
        {
            // `경로:줄:열: 이유` — IDE 터미널에서 눌러 그 자리로 간다.
            uint32 line   = 0;
            uint32 column = 0;
            StringUtil::getLineAndColumn( xmlText, result.offset > 0 ? static_cast<size_t>( result.offset ) : 0, line, column );
            StringBuilder<constant::kMaxBuffer1024> message;
            message.appendFormat( "%#:%#:%#: %#", source, line, column, result.description() );
            _lastError = message.c_str();
            SW_LOG_ERROR( "XML parse error at %#", _lastError );
            return false;
        }

        if ( _impl->doc.first_child().empty() )
        {
            _lastError = string( source ) + ": no element";
            return false;
        }
        return true;
    }

    bool XMLDocument::loadFile( string_view absPath )
    {
        // 경로로 읽은 파일도 쿠킹 작업의 입력이다 — `FileUtil` 로 바로 읽으면 기록에서 빠진다(`ResourceIO::readTextFile`).
        string text;
        if ( ResourceIO::readTextFile( absPath, text ) == false )
        {
            _lastError = string( absPath ) + ": cannot read the file (or it was read during cooking outside a cook job)";
            return false;
        }
        return parse( text, absPath );
    }

    bool XMLDocument::loadResource( string_view relativePath, string* pOutAbsPath )
    {
        if ( relativePath.empty() )
        {
            _lastError          = "empty path";
            _bLastErrorNotFound = false;
            return false;
        }
        string text;
        string absPath;
        if ( ResourceIO::readTextResource( ResourceID{ relativePath }, text, &absPath ) == false )
        {
            _lastError          = string( relativePath ) + ": not found (no file at that path and no resource by that name)";
            _bLastErrorNotFound = true;
            return false;
        }
        if ( pOutAbsPath != nullptr )
            *pOutAbsPath = absPath;
        return parse( text, absPath );
    }

    uint32 XMLDocument::computeLineNumber( string_view text, int64 offset )
    {
        if ( offset < 0 )
            return 0;
        const size_t end  = MathUtil::min( static_cast<size_t>( offset ), text.size() );
        uint32       line = 1;
        for ( size_t index = 0; index < end; ++index )
        {
            if ( text[index] == '\n' )
                ++line;
        }
        return line;
    }

    XMLNode XMLDocument::getRoot( const utf8* pName ) const
    {
        if ( _impl == nullptr )
            return {};
        if ( pName == nullptr )
            return XMLNode{ _impl->doc.first_child().internal_object() };
        return XMLNode{ XMLDocumentInternal::findChild( _impl->doc, pName ).internal_object() };
    }

    XMLNode XMLDocument::appendRoot( const utf8* pName )
    {
        if ( _impl == nullptr )
            _impl = make_unique<Impl>();
        pugi::xml_node pRoot = _impl->doc.append_child( pName );
        return XMLNode{ pRoot.internal_object() };
    }

    string XMLDocument::saveToString() const
    {
        if ( _impl == nullptr )
            return "";
        XMLStringWriter writer;
        _impl->doc.save( writer, "\t", kXMLSaveFormat );
        return wrapLongElementLines( writer._result );
    }

    bool XMLDocument::saveFile( string_view absPath ) const
    {
        if ( absPath.empty() )
            return false;
        return FileUtil::writeTextFile( absPath, saveToString() );
    }

    string XMLDocument::escapeString( string_view text )
    {
        StringBuilder<constant::kMaxBuffer1024> out;
        for ( const utf8 ch : text )
        {
            switch ( ch )
            {
                case '&':
                {
                    out.append( "&amp;" );
                    break;
                }
                case '<':
                {
                    out.append( "&lt;" );
                    break;
                }
                case '>':
                {
                    out.append( "&gt;" );
                    break;
                }
                case '"':
                {
                    out.append( "&quot;" );
                    break;
                }
                case '\'':
                {
                    out.append( "&apos;" );
                    break;
                }
                default:
                {
                    out.append( ch );
                    break;
                }
            }
        }
        return string{ out.view() };
    }

    string XMLDocument::unescapeString( string_view text )
    {
        StringBuilder<constant::kMaxBuffer1024> out;
        for ( size_t index = 0; index < text.size(); ++index )
        {
            if ( text[index] == '&' )
            {
                const string_view remaining = text.substr( index );
                if ( StringUtil::startsWith( remaining, "&amp;" ) )
                {
                    out.append( '&' );
                    index += 4;
                }
                else if ( StringUtil::startsWith( remaining, "&lt;" ) )
                {
                    out.append( '<' );
                    index += 3;
                }
                else if ( StringUtil::startsWith( remaining, "&gt;" ) )
                {
                    out.append( '>' );
                    index += 3;
                }
                else if ( StringUtil::startsWith( remaining, "&quot;" ) )
                {
                    out.append( '"' );
                    index += 5;
                }
                else if ( StringUtil::startsWith( remaining, "&apos;" ) )
                {
                    out.append( '\'' );
                    index += 5;
                }
                else
                {
                    out.append( text[index] );
                }
            }
            else
            {
                out.append( text[index] );
            }
        }
        return string{ out.view() };
    }
} // namespace sw
