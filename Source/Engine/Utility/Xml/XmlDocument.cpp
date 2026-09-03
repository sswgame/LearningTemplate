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
        pNode.print( writer, "\t", pugi::format_default | pugi::format_no_declaration );
        return std::move( writer._result );
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
        _impl->doc.save( writer, "\t", pugi::format_default | pugi::format_no_declaration );
        return std::move( writer._result );
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
