#include "pch.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Resource/ResourceUtil.h"

#include <pugixml.hpp>

namespace sw
{
    namespace
    {
        /// @brief 자산 XML 저장 포맷. 줄 접기는 pugixml 이 아니라 wrapLongElementLines 가 한다.
        inline constexpr uint32 kXmlSaveFormat = pugi::format_default | pugi::format_no_declaration;

        /// @brief 이 길이를 넘는 요소 줄만 속성 단위로 접는다.
        inline constexpr size_t kXmlWrapColumn = 120;

        /**
         * @brief 한 줄에 담긴 속성들을 잘라, 다음 속성이 시작되는 위치들을 모읍니다.
         * @details 속성 값 안에도 공백이 있으므로 따옴표 밖의 공백만 경계로 센다. 값 안의 따옴표는
         *          XML 이 `&quot;` 로 이스케이프하므로 따옴표 쌍만 세면 안전하다.
         * @param outListEnd 각 속성의 끝 위치(반열림). 첫 원소가 첫 속성의 끝이다.
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
         * @details 리플렉션 직렬화는 스칼라를 속성으로 쓴다. 필드가 20개면 속성 20개짜리 요소가
         *          한 줄이 되어 1,000자를 넘고, 읽기도 어렵고 한 글자만 고쳐도 diff 가 줄 전체를
         *          바뀐 것으로 표시한다.
         *
         *          pugixml 의 `format_indent_attributes` 는 태그 이름만 남기고 속성을 전부 아래로
         *          내려서, 짧은 요소까지 여러 줄로 흩어지고 무슨 요소인지 눈에 덜 들어온다.
         *          그래서 직접 접는다 — **짧은 줄은 그대로 두고**, 긴 줄만 첫 속성을 태그 옆에
         *          남긴 채 나머지를 한 줄에 하나씩 내린다.
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
                    ++indentLen;

                // 접을 대상: 충분히 길고, 여는 태그이며, 속성이 둘 이상인 줄.
                const bool   bOpenTag   = ( indentLen + 1 < line.size() ) && line[indentLen] == '<' &&
                                          line[indentLen + 1] != '/' && line[indentLen + 1] != '!' && line[indentLen + 1] != '?';
                const size_t firstSpace = line.find( ' ', indentLen );

                listAttrEnd.clear();
                if ( line.size() > kXmlWrapColumn && bOpenTag && firstSpace != string_view::npos )
                    collectAttributeEnds( line, firstSpace, listAttrEnd );

                if ( listAttrEnd.size() < 2 )
                {
                    result.append( line.data(), line.size() );
                    if ( bLastLine == false )
                        result.push_back( '\n' );
                    lineStart = lineEnd + 1;
                    continue;
                }

                // 태그 + 첫 속성은 같은 줄에 둔다 — 무슨 요소인지가 먼저 보여야 한다.
                result.append( line.data(), listAttrEnd[0] );

                // 이어지는 속성은 **첫 속성과 같은 열**에 세운다. `<TagName ` 만큼의 폭을 들여쓰기
                // 뒤부터 재서 공백으로 메우므로, 탭 폭이 몇이든 세로줄이 맞는다.
                const size_t alignColumn = firstSpace + 1 - indentLen;

                for ( size_t attrIndex = 1; attrIndex < listAttrEnd.size(); ++attrIndex )
                {
                    size_t tokenStart = listAttrEnd[attrIndex - 1];
                    while ( tokenStart < line.size() && line[tokenStart] == ' ' )
                        ++tokenStart;

                    result.push_back( '\n' );
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

        struct XmlStringWriter : pugi::xml_writer
        {
            string _result;

            void write( const void* pData, size_t size ) override
            {
                _result.append( static_cast<const utf8*>( pData ), size );
            }
        };

        struct XmlDocumentInternal
        {
            static pugi::xml_node asNode( void* pPtr )
            {
                return pugi::xml_node{ static_cast<pugi::xml_node_struct*>( pPtr ) };
            }

            static pugi::xml_attribute asAttr( void* pPtr )
            {
                return pugi::xml_attribute{ static_cast<pugi::xml_attribute_struct*>( pPtr ) };
            }

            static bool nameEquals( const utf8* pLhs, const utf8* pRhs, bool bIgnoreCase = true )
            {
                return StringUtil::equals( pLhs, pRhs, bIgnoreCase );
            }

            static pugi::xml_node findChild( pugi::xml_node parent, const utf8* pName, bool bIgnoreCase = true )
            {
                if ( parent.empty() || pName == nullptr )
                    return {};
                if ( bIgnoreCase == false )
                    return parent.child( pName );
                for ( pugi::xml_node child = parent.first_child(); child.empty() == false; child = child.next_sibling() )
                {
                    if ( nameEquals( child.name(), pName, true ) )
                        return child;
                }
                return {};
            }

            static pugi::xml_node findSibling( pugi::xml_node node, const utf8* pName, bool bIgnoreCase = true )
            {
                if ( node.empty() || pName == nullptr )
                    return {};
                for ( pugi::xml_node sibling = node; sibling.empty() == false; sibling = sibling.next_sibling() )
                {
                    if ( nameEquals( sibling.name(), pName, bIgnoreCase ) )
                        return sibling;
                }
                return {};
            }

            static pugi::xml_attribute findAttr( pugi::xml_node node, const utf8* pName, bool bIgnoreCase = true )
            {
                if ( node.empty() || pName == nullptr )
                    return {};
                if ( bIgnoreCase == false )
                    return node.attribute( pName );
                for ( pugi::xml_attribute attr = node.first_attribute(); attr.empty() == false; attr = attr.next_attribute() )
                {
                    if ( nameEquals( attr.name(), pName, true ) )
                        return attr;
                }
                return {};
            }

            static bool parseNodeBool( const utf8* pText, bool fallback )
            {
                if ( StringUtil::isNullOrEmpty( pText ) )
                    return fallback;
                return StringUtil::parseBool( pText, fallback );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "XmlDocument" );

    struct XmlDocument::Impl
    {
        pugi::xml_document doc;
    };

    const utf8* XmlAttribute::name() const
    {
        const pugi::xml_attribute attr = XmlDocumentInternal::asAttr( _pAttr );
        return attr.name();
    }

    const utf8* XmlAttribute::value() const
    {
        const pugi::xml_attribute attr = XmlDocumentInternal::asAttr( _pAttr );
        return attr.value();
    }

    XmlAttribute XmlAttribute::next() const
    {
        const pugi::xml_attribute attr = XmlDocumentInternal::asAttr( _pAttr );
        if ( attr.empty() )
            return {};
        return XmlAttribute{ attr.next_attribute().internal_object() };
    }

    const utf8* XmlNode::name() const
    {
        const pugi::xml_node pNode = XmlDocumentInternal::asNode( _pNode );
        return pNode.name();
    }

    const utf8* XmlNode::text() const
    {
        const pugi::xml_node pNode = XmlDocumentInternal::asNode( _pNode );
        return pNode.child_value();
    }

    const utf8* XmlNode::attr( const utf8* pName, bool bIgnoreCaseKeys ) const
    {
        const pugi::xml_node pNode = XmlDocumentInternal::asNode( _pNode );
        if ( pNode.empty() || pName == nullptr )
            return nullptr;
        const pugi::xml_attribute pAttr = XmlDocumentInternal::findAttr( pNode, pName, bIgnoreCaseKeys );
        if ( pAttr.empty() == false )
            return pAttr.value();
        return nullptr;
    }

    int32 XmlNode::attrInt( const utf8* pName, int32 fallback, bool bIgnoreCaseKeys ) const
    {
        const utf8* pValue = attr( pName, bIgnoreCaseKeys );
        if ( pValue == nullptr )
            return fallback;
        int32 val{ fallback };
        StringUtil::parseInt( pValue, val );
        return val;
    }

    float32 XmlNode::attrFloat( const utf8* pName, float32 fallback, bool bIgnoreCaseKeys ) const
    {
        const utf8* pValue = attr( pName, bIgnoreCaseKeys );
        if ( pValue == nullptr )
            return fallback;
        float32 val{ fallback };
        StringUtil::parseFloat( pValue, val );
        return val;
    }

    bool XmlNode::attrBool( const utf8* pName, bool fallback, bool bIgnoreCaseKeys ) const
    {
        return XmlDocumentInternal::parseNodeBool( attr( pName, bIgnoreCaseKeys ), fallback );
    }

    XmlNode XmlNode::child( const utf8* pName, bool bIgnoreCaseKeys ) const
    {
        const pugi::xml_node pNode = XmlDocumentInternal::asNode( _pNode );
        if ( pNode.empty() )
            return {};
        if ( pName == nullptr )
            return XmlNode{ pNode.first_child().internal_object() };
        return XmlNode{ XmlDocumentInternal::findChild( pNode, pName, bIgnoreCaseKeys ).internal_object() };
    }

    XmlNode XmlNode::next( const utf8* pName, bool bIgnoreCaseKeys ) const
    {
        const pugi::xml_node pNode = XmlDocumentInternal::asNode( _pNode );
        if ( pNode.empty() )
            return {};
        const pugi::xml_node pSibling = pNode.next_sibling();
        if ( pName == nullptr )
            return XmlNode{ pSibling.internal_object() };
        return XmlNode{ XmlDocumentInternal::findSibling( pSibling, pName, bIgnoreCaseKeys ).internal_object() };
    }

    const utf8* XmlNode::childText( const utf8* pName, bool bIgnoreCaseKeys ) const
    {
        const XmlNode childNode = child( pName, bIgnoreCaseKeys );
        if ( childNode.isValid() == false )
            return nullptr;
        return childNode.text();
    }

    int32 XmlNode::childInt( const utf8* pName, int32 fallback, bool bIgnoreCaseKeys ) const
    {
        const utf8* pText = childText( pName, bIgnoreCaseKeys );
        if ( pText == nullptr )
            return fallback;
        int32 val{ fallback };
        StringUtil::parseInt( pText, val );
        return val;
    }

    float32 XmlNode::childFloat( const utf8* pName, float32 fallback, bool bIgnoreCaseKeys ) const
    {
        const utf8* pText = childText( pName, bIgnoreCaseKeys );
        if ( pText == nullptr )
            return fallback;
        float32 val{ fallback };
        StringUtil::parseFloat( pText, val );
        return val;
    }

    bool XmlNode::childBool( const utf8* pName, bool fallback, bool bIgnoreCaseKeys ) const
    {
        return XmlDocumentInternal::parseNodeBool( childText( pName, bIgnoreCaseKeys ), fallback );
    }

    bool XmlNode::takeChildText( const utf8* pName, string& dst, bool bIgnoreCaseKeys ) const
    {
        const utf8* pValue = childText( pName, bIgnoreCaseKeys );
        if ( StringUtil::isNullOrEmpty( pValue ) )
            return false;
        dst = pValue;
        return true;
    }

    XmlAttribute XmlNode::firstAttr() const
    {
        const pugi::xml_node pNode = XmlDocumentInternal::asNode( _pNode );
        if ( pNode.empty() )
            return {};
        return XmlAttribute{ pNode.first_attribute().internal_object() };
    }

    XmlNode XmlNode::appendChild( const utf8* pName ) const
    {
        pugi::xml_node pNode = XmlDocumentInternal::asNode( _pNode );
        if ( pNode.empty() || pName == nullptr )
            return {};
        pugi::xml_node pChild = pNode.append_child( pName );
        return XmlNode{ pChild.internal_object() };
    }

    XmlNode XmlNode::appendChild( const utf8* pName, string_view value ) const
    {
        XmlNode childNode = appendChild( pName );
        childNode.setValue( value );
        return childNode;
    }

    XmlNode XmlNode::appendChild( const utf8* pName, int32 value ) const
    {
        XmlNode childNode = appendChild( pName );
        childNode.setValue( value );
        return childNode;
    }

    XmlNode XmlNode::appendChild( const utf8* pName, uint32 value ) const
    {
        XmlNode childNode = appendChild( pName );
        childNode.setValue( value );
        return childNode;
    }

    XmlNode XmlNode::appendChild( const utf8* pName, float32 value ) const
    {
        XmlNode childNode = appendChild( pName );
        childNode.setValue( value );
        return childNode;
    }

    XmlNode XmlNode::appendChild( const utf8* pName, bool value ) const
    {
        XmlNode childNode = appendChild( pName );
        childNode.setValue( value );
        return childNode;
    }

    void XmlNode::appendAttr( const utf8* pName, const utf8* pValue ) const
    {
        pugi::xml_node pNode = XmlDocumentInternal::asNode( _pNode );
        if ( pNode.empty() || pName == nullptr )
            return;
        pugi::xml_attribute pAttr = pNode.append_attribute( pName );
        pAttr.set_value( pValue != nullptr ? pValue : "" );
    }

    void XmlNode::appendAttr( const utf8* pName, string_view value ) const
    {
        pugi::xml_node pNode = XmlDocumentInternal::asNode( _pNode );
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

    void XmlNode::appendAttr( const utf8* pName, int32 value ) const
    {
        StringBuilder<constant::kMaxBuffer32> sb;
        sb.append( value );
        appendAttr( pName, sb.c_str() );
    }

    void XmlNode::appendAttr( const utf8* pName, uint32 value ) const
    {
        StringBuilder<constant::kMaxBuffer32> sb;
        sb.append( value );
        appendAttr( pName, sb.c_str() );
    }

    void XmlNode::appendAttr( const utf8* pName, float32 value ) const
    {
        StringBuilder<constant::kMaxBuffer32> sb;
        sb.append( value );
        appendAttr( pName, sb.c_str() );
    }

    void XmlNode::appendAttr( const utf8* pName, bool value ) const
    {
        appendAttr( pName, value ? "1" : "0" );
    }

    void XmlNode::setAttr( const utf8* pName, const utf8* pValue ) const
    {
        pugi::xml_node pNode = XmlDocumentInternal::asNode( _pNode );
        if ( pNode.empty() || pName == nullptr )
            return;
        pugi::xml_attribute pAttr = XmlDocumentInternal::findAttr( pNode, pName, true );
        if ( pAttr.empty() == false )
            pAttr.set_value( pValue != nullptr ? pValue : "" );
        else
            appendAttr( pName, pValue );
    }

    void XmlNode::setAttr( const utf8* pName, string_view value ) const
    {
        pugi::xml_node pNode = XmlDocumentInternal::asNode( _pNode );
        if ( pNode.empty() || pName == nullptr )
            return;
        pugi::xml_attribute pAttr = XmlDocumentInternal::findAttr( pNode, pName, true );
        if ( pAttr.empty() == false )
        {
            const string valStr( value );
            pAttr.set_value( valStr.c_str() );
        }
        else
        {
            appendAttr( pName, value );
        }
    }

    void XmlNode::setAttr( const utf8* pName, int32 value ) const
    {
        StringBuilder<constant::kMaxBuffer32> sb;
        sb.append( value );
        setAttr( pName, sb.c_str() );
    }

    void XmlNode::setAttr( const utf8* pName, uint32 value ) const
    {
        StringBuilder<constant::kMaxBuffer32> sb;
        sb.append( value );
        setAttr( pName, sb.c_str() );
    }

    void XmlNode::setAttr( const utf8* pName, float32 value ) const
    {
        StringBuilder<constant::kMaxBuffer32> sb;
        sb.append( value );
        setAttr( pName, sb.c_str() );
    }

    void XmlNode::setAttr( const utf8* pName, bool value ) const
    {
        setAttr( pName, value ? "1" : "0" );
    }

    void XmlNode::setName( const utf8* pName ) const
    {
        pugi::xml_node pNode = XmlDocumentInternal::asNode( _pNode );
        if ( pNode.empty() || pName == nullptr )
            return;
        pNode.set_name( pName );
    }

    void XmlNode::setValue( const utf8* pValue ) const
    {
        pugi::xml_node pNode = XmlDocumentInternal::asNode( _pNode );
        if ( pNode.empty() )
            return;
        pNode.text().set( pValue != nullptr ? pValue : "" );
    }

    void XmlNode::setValue( string_view value ) const
    {
        pugi::xml_node pNode = XmlDocumentInternal::asNode( _pNode );
        if ( pNode.empty() )
            return;
        const string valStr( value );
        pNode.text().set( valStr.c_str() );
    }

    void XmlNode::setValue( int32 value ) const
    {
        StringBuilder<constant::kMaxBuffer32> sb;
        sb.append( value );
        setValue( sb.c_str() );
    }

    void XmlNode::setValue( uint32 value ) const
    {
        StringBuilder<constant::kMaxBuffer32> sb;
        sb.append( value );
        setValue( sb.c_str() );
    }

    void XmlNode::setValue( float32 value ) const
    {
        StringBuilder<constant::kMaxBuffer32> sb;
        sb.append( value );
        setValue( sb.c_str() );
    }

    void XmlNode::setValue( bool value ) const
    {
        setValue( value ? "1" : "0" );
    }

    string XmlNode::toString() const
    {
        const pugi::xml_node pNode = XmlDocumentInternal::asNode( _pNode );
        if ( pNode.empty() )
            return {};
        XmlStringWriter writer;
        pNode.print( writer, "\t", kXmlSaveFormat );
        return wrapLongElementLines( writer._result );
    }

    XmlNode XmlNode::appendClone( XmlNode src ) const
    {
        pugi::xml_node pNode = XmlDocumentInternal::asNode( _pNode );
        pugi::xml_node pSrc  = XmlDocumentInternal::asNode( src._pNode );
        if ( pNode.empty() || pSrc.empty() )
            return {};
        pugi::xml_node cloned = pNode.append_copy( pSrc );
        return XmlNode{ cloned.internal_object() };
    }

    XmlDocument::XmlDocument()
        : _impl{ make_unique<Impl>() } {}

    XmlDocument::~XmlDocument() = default;

    XmlDocument::XmlDocument( XmlDocument&& other ) noexcept
        : _impl{ std::move( other._impl ) } {}

    XmlDocument& XmlDocument::operator=( XmlDocument&& other ) noexcept
    {
        if ( this != &other )
            _impl = std::move( other._impl );
        return *this;
    }

    void XmlDocument::clear()
    {
        if ( _impl == nullptr )
            _impl = make_unique<Impl>();
        else
            _impl->doc.reset();
    }

    bool XmlDocument::parse( string_view xmlText )
    {
        if ( _impl == nullptr )
            _impl = make_unique<Impl>();
        _impl->doc.reset();
        if ( xmlText.empty() )
            return false;

        const pugi::xml_parse_result result = _impl->doc.load_buffer(
            xmlText.data(),
            xmlText.size(),
            pugi::parse_default | pugi::parse_trim_pcdata );

        if ( result.status != pugi::status_ok )
        {
            SW_LOG_ERROR( "PugiXML Parse Error: %# (offset %#)", result.description(), result.offset );
            return false;
        }

        return _impl->doc.first_child().empty() == false;
    }

    bool XmlDocument::loadFile( string_view absPath )
    {
        string text;
        if ( FileUtil::readTextFile( absPath, text ) == false )
            return false;
        return parse( text );
    }

    bool XmlDocument::loadResource( string_view relativePath, string* pOutAbsPath )
    {
        string text;
        string absPath;
        if ( ResourceUtil::readTextResource( relativePath, text, &absPath ) == false )
            return false;
        if ( pOutAbsPath != nullptr )
            *pOutAbsPath = absPath;
        return parse( text );
    }

    bool XmlDocument::loadPath( string_view path, string* pOutAbsPath )
    {
        if ( path.empty() )
            return false;
        if ( FileUtil::fileExists( path ) )
        {
            if ( pOutAbsPath != nullptr )
                *pOutAbsPath = string{ path };
            return loadFile( path );
        }
        return loadResource( path, pOutAbsPath );
    }

    XmlNode XmlDocument::root( const utf8* pName, bool bIgnoreCaseKeys ) const
    {
        if ( _impl == nullptr )
            return {};
        if ( pName == nullptr )
            return XmlNode{ _impl->doc.first_child().internal_object() };
        return XmlNode{ XmlDocumentInternal::findChild( _impl->doc, pName, bIgnoreCaseKeys ).internal_object() };
    }

    XmlNode XmlDocument::appendRoot( const utf8* pName )
    {
        if ( _impl == nullptr )
            _impl = make_unique<Impl>();
        pugi::xml_node pRoot = _impl->doc.append_child( pName );
        return XmlNode{ pRoot.internal_object() };
    }

    string XmlDocument::saveToString() const
    {
        if ( _impl == nullptr )
            return "";
        XmlStringWriter writer;
        _impl->doc.save( writer, "\t", kXmlSaveFormat );
        return wrapLongElementLines( writer._result );
    }

    bool XmlDocument::saveFile( string_view absPath ) const
    {
        if ( absPath.empty() )
            return false;
        return FileUtil::writeTextFile( absPath, saveToString() );
    }

    string XmlDocument::escapeString( string_view text )
    {
        StringBuilder<constant::kMaxBuffer1024> out;
        for ( const utf8 ch : text )
        {
            switch ( ch )
            {
                case '&':
                    out.append( "&amp;" );
                    break;
                case '<':
                    out.append( "&lt;" );
                    break;
                case '>':
                    out.append( "&gt;" );
                    break;
                case '"':
                    out.append( "&quot;" );
                    break;
                case '\'':
                    out.append( "&apos;" );
                    break;
                default:
                    out.append( ch );
                    break;
            }
        }
        return string{ out.view() };
    }

    string XmlDocument::unescapeString( string_view text )
    {
        StringBuilder<constant::kMaxBuffer1024> out;
        for ( size_t index = 0; index < text.size(); ++index )
        {
            if ( text[index] == '&' )
            {
                const string_view rem = text.substr( index );
                if ( StringUtil::startsWith( rem, "&amp;" ) )
                {
                    out.append( '&' );
                    index += 4;
                }
                else if ( StringUtil::startsWith( rem, "&lt;" ) )
                {
                    out.append( '<' );
                    index += 3;
                }
                else if ( StringUtil::startsWith( rem, "&gt;" ) )
                {
                    out.append( '>' );
                    index += 3;
                }
                else if ( StringUtil::startsWith( rem, "&quot;" ) )
                {
                    out.append( '"' );
                    index += 5;
                }
                else if ( StringUtil::startsWith( rem, "&apos;" ) )
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
