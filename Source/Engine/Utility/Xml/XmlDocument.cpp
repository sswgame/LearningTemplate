#include "pch.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Resource/ResourceUtil.h"

#define RAPIDXML_NO_EXCEPTIONS
#include <rapidxml/rapidxml.hpp>
#include <rapidxml/rapidxml_print.hpp>

// RAPIDXML_NO_EXCEPTIONS 정의 시 RapidXML 내부에서 파싱 실패 시 호출하는 콜백 함수입니다.
namespace rapidxml
{
	void parse_error_handler( const utf8* what, void* where )
	{
		(void)where;
		SW_LOG_ERROR( "RapidXML Parse Error: %#", what != nullptr ? what : "unknown" );
	}
} // namespace rapidxml

namespace sw
{
	SW_LOG_CALLER( "XmlDocument" );

	namespace
	{

		rapidxml::xml_node<>* asNode( void* pPtr )
		{
			return static_cast<rapidxml::xml_node<>*>( pPtr );
		}

		rapidxml::xml_attribute<>* asAttr( void* pPtr )
		{
			return static_cast<rapidxml::xml_attribute<>*>( pPtr );
		}

		bool nameEquals( const utf8* pLhs, const utf8* pRhs, bool bIgnoreCase = true )
		{
			if ( pLhs == pRhs )
				return true;
			if ( pLhs == nullptr || pRhs == nullptr )
				return false;
			return bIgnoreCase ? StringUtil::equalsIgnoreCase( pLhs, pRhs ) : ( StringUtil::strcmp( pLhs, pRhs ) == 0 );
		}

		rapidxml::xml_node<>* findChild( rapidxml::xml_node<>* pParent, const utf8* pName, bool bIgnoreCase = true )
		{
			if ( pParent == nullptr || pName == nullptr )
				return nullptr;
			if ( bIgnoreCase == false )
				return pParent->first_node( pName );
			for ( rapidxml::xml_node<>* pChild = pParent->first_node(); pChild != nullptr; pChild = pChild->next_sibling() )
			{
				if ( pChild->name() != nullptr && nameEquals( pChild->name(), pName, true ) )
					return pChild;
			}
			return nullptr;
		}

		rapidxml::xml_node<>* findSibling( rapidxml::xml_node<>* pNode, const utf8* pName, bool bIgnoreCase = true )
		{
			if ( pNode == nullptr || pName == nullptr )
				return nullptr;
			for ( rapidxml::xml_node<>* pSiblingNode = pNode; pSiblingNode != nullptr; pSiblingNode = pSiblingNode->next_sibling() )
			{
				if ( pSiblingNode->name() != nullptr && nameEquals( pSiblingNode->name(), pName, bIgnoreCase ) )
					return pSiblingNode;
			}
			return nullptr;
		}

		rapidxml::xml_attribute<>* findAttr( rapidxml::xml_node<>* pNode, const utf8* pName, bool bIgnoreCase = true )
		{
			if ( pNode == nullptr || pName == nullptr )
				return nullptr;
			if ( bIgnoreCase == false )
				return pNode->first_attribute( pName );
			for ( rapidxml::xml_attribute<>* pAttr = pNode->first_attribute(); pAttr != nullptr; pAttr = pAttr->next_attribute() )
			{
				if ( pAttr->name() != nullptr && nameEquals( pAttr->name(), pName, true ) )
					return pAttr;
			}
			return nullptr;
		}

		const utf8* allocDocString( rapidxml::xml_document<>* pDoc, string_view value )
		{
			if ( pDoc == nullptr )
				return "";
			utf8* pOut = pDoc->allocate_string( nullptr, value.size() + 1 );
			if ( value.empty() == false )
				Memory::copy( pOut, value.data(), value.size() );
			pOut[value.size()] = '\0';
			return pOut;
		}

		bool parseNodeBool( const utf8* pText, bool fallback )
		{
			if ( pText == nullptr || pText[0] == '\0' )
				return fallback;
			return StringUtil::parseBool( pText, fallback );
		}

	} // namespace

	struct XmlDocument::Impl
	{
		string					 buffer;
		rapidxml::xml_document<> doc;
	};

	const utf8* XmlAttribute::name() const
	{
		rapidxml::xml_attribute<>* pAttr = asAttr( _pAttr );
		return pAttr != nullptr && pAttr->name() != nullptr ? pAttr->name() : "";
	}

	const utf8* XmlAttribute::value() const
	{
		rapidxml::xml_attribute<>* pAttr = asAttr( _pAttr );
		return pAttr != nullptr && pAttr->value() != nullptr ? pAttr->value() : "";
	}

	XmlAttribute XmlAttribute::next() const
	{
		rapidxml::xml_attribute<>* pAttr = asAttr( _pAttr );
		if ( pAttr == nullptr )
			return {};
		return XmlAttribute{ pAttr->next_attribute() };
	}

	const utf8* XmlNode::name() const
	{
		rapidxml::xml_node<>* pNode = asNode( _pNode );
		return pNode != nullptr && pNode->name() != nullptr ? pNode->name() : "";
	}

	const utf8* XmlNode::text() const
	{
		rapidxml::xml_node<>* pNode = asNode( _pNode );
		return pNode != nullptr && pNode->value() != nullptr ? pNode->value() : "";
	}

	const utf8* XmlNode::attr( const utf8* pName, bool bIgnoreCaseKeys ) const
	{
		rapidxml::xml_node<>* pNode = asNode( _pNode );
		if ( pNode == nullptr || pName == nullptr )
			return nullptr;
		rapidxml::xml_attribute<>* pAttr = findAttr( pNode, pName, bIgnoreCaseKeys );
		if ( pAttr != nullptr )
			return pAttr->value();
		return nullptr;
	}

	int32 XmlNode::attrInt( const utf8* pName, int32 fallback, bool bIgnoreCaseKeys ) const
	{
		const utf8* pValue = attr( pName, bIgnoreCaseKeys );
		return pValue != nullptr ? StringUtil::atoi( pValue ) : fallback;
	}

	float32 XmlNode::attrFloat( const utf8* pName, float32 fallback, bool bIgnoreCaseKeys ) const
	{
		const utf8* pValue = attr( pName, bIgnoreCaseKeys );
		return pValue != nullptr ? static_cast<float32>( StringUtil::atof( pValue ) ) : fallback;
	}

	bool XmlNode::attrBool( const utf8* pName, bool fallback, bool bIgnoreCaseKeys ) const
	{
		return parseNodeBool( attr( pName, bIgnoreCaseKeys ), fallback );
	}

	XmlNode XmlNode::child( const utf8* pName, bool bIgnoreCaseKeys ) const
	{
		rapidxml::xml_node<>* pNode = asNode( _pNode );
		if ( pNode == nullptr )
			return {};
		if ( pName == nullptr )
			return XmlNode{ pNode->first_node() };
		return XmlNode{ findChild( pNode, pName, bIgnoreCaseKeys ) };
	}

	XmlNode XmlNode::next( const utf8* pName, bool bIgnoreCaseKeys ) const
	{
		rapidxml::xml_node<>* pNode = asNode( _pNode );
		if ( pNode == nullptr )
			return {};
		rapidxml::xml_node<>* pSibling = pNode->next_sibling();
		if ( pName == nullptr )
			return XmlNode{ pSibling };
		return XmlNode{ findSibling( pSibling, pName, bIgnoreCaseKeys ) };
	}

	const utf8* XmlNode::childText( const utf8* pName, bool bIgnoreCaseKeys ) const
	{
		XmlNode childNode = child( pName, bIgnoreCaseKeys );
		if ( childNode.isValid() == false )
			return nullptr;
		return childNode.text();
	}

	int32 XmlNode::childInt( const utf8* pName, int32 fallback, bool bIgnoreCaseKeys ) const
	{
		const utf8* pText = childText( pName, bIgnoreCaseKeys );
		return pText != nullptr ? StringUtil::atoi( pText ) : fallback;
	}

	float32 XmlNode::childFloat( const utf8* pName, float32 fallback, bool bIgnoreCaseKeys ) const
	{
		const utf8* pText = childText( pName, bIgnoreCaseKeys );
		return pText != nullptr ? static_cast<float32>( StringUtil::atof( pText ) ) : fallback;
	}

	bool XmlNode::childBool( const utf8* pName, bool fallback, bool bIgnoreCaseKeys ) const
	{
		return parseNodeBool( childText( pName, bIgnoreCaseKeys ), fallback );
	}

	bool XmlNode::takeChildText( const utf8* pName, string& dst, bool bIgnoreCaseKeys ) const
	{
		const utf8* pValue = childText( pName, bIgnoreCaseKeys );
		if ( pValue == nullptr || pValue[0] == '\0' )
			return false;
		dst = pValue;
		return true;
	}

	XmlAttribute XmlNode::firstAttr() const
	{
		rapidxml::xml_node<>* pNode = asNode( _pNode );
		if ( pNode == nullptr )
			return {};
		return XmlAttribute{ pNode->first_attribute() };
	}

	XmlNode XmlNode::appendChild( const utf8* pName ) const
	{
		rapidxml::xml_node<>* pNode = asNode( _pNode );
		if ( pNode == nullptr || pNode->document() == nullptr )
			return {};
		rapidxml::xml_node<>* pChild = pNode->document()->allocate_node( rapidxml::node_element, pNode->document()->allocate_string( pName ) );
		pNode->append_node( pChild );
		return XmlNode{ pChild };
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
		rapidxml::xml_node<>* pNode = asNode( _pNode );
		if ( pNode == nullptr || pNode->document() == nullptr || pName == nullptr )
			return;
		const utf8*				   pSafeValue = pValue != nullptr ? pValue : "";
		rapidxml::xml_attribute<>* pAttr	  = pNode->document()->allocate_attribute( pNode->document()->allocate_string( pName ), pNode->document()->allocate_string( pSafeValue ) );
		pNode->append_attribute( pAttr );
	}

	void XmlNode::appendAttr( const utf8* pName, string_view value ) const
	{
		rapidxml::xml_node<>* pNode = asNode( _pNode );
		if ( pNode == nullptr || pNode->document() == nullptr || pName == nullptr )
			return;
		rapidxml::xml_document<>* pDoc = pNode->document();
		pNode->append_attribute( pDoc->allocate_attribute( pDoc->allocate_string( pName ), allocDocString( pDoc, value ) ) );
	}

	void XmlNode::appendAttr( const utf8* pName, int32 value ) const
	{
		StringBuilder<32> sb;
		sb.append( value );
		appendAttr( pName, sb.c_str() );
	}

	void XmlNode::appendAttr( const utf8* pName, uint32 value ) const
	{
		StringBuilder<32> sb;
		sb.append( value );
		appendAttr( pName, sb.c_str() );
	}

	void XmlNode::appendAttr( const utf8* pName, float32 value ) const
	{
		StringBuilder<32> sb;
		sb.append( value );
		appendAttr( pName, sb.c_str() );
	}

	void XmlNode::appendAttr( const utf8* pName, bool value ) const
	{
		appendAttr( pName, value ? "1" : "0" );
	}

	void XmlNode::setAttr( const utf8* pName, const utf8* pValue ) const
	{
		if ( _pNode == nullptr || pName == nullptr )
			return;
		rapidxml::xml_node<>*	   pNode	  = asNode( _pNode );
		rapidxml::xml_attribute<>* pAttr	  = findAttr( pNode, pName, true );
		const utf8*				   pSafeValue = pValue != nullptr ? pValue : "";
		if ( pAttr != nullptr )
			pAttr->value( pNode->document()->allocate_string( pSafeValue ) );
		else
			appendAttr( pName, pSafeValue );
	}

	void XmlNode::setAttr( const utf8* pName, string_view value ) const
	{
		if ( _pNode == nullptr || pName == nullptr )
			return;
		rapidxml::xml_node<>*	   pNode = asNode( _pNode );
		rapidxml::xml_attribute<>* pAttr = findAttr( pNode, pName, true );
		if ( pAttr != nullptr )
			pAttr->value( allocDocString( pNode->document(), value ) );
		else
			appendAttr( pName, value );
	}

	void XmlNode::setAttr( const utf8* pName, int32 value ) const
	{
		StringBuilder<32> sb;
		sb.append( value );
		setAttr( pName, sb.c_str() );
	}

	void XmlNode::setAttr( const utf8* pName, uint32 value ) const
	{
		StringBuilder<32> sb;
		sb.append( value );
		setAttr( pName, sb.c_str() );
	}

	void XmlNode::setAttr( const utf8* pName, float32 value ) const
	{
		StringBuilder<32> sb;
		sb.append( value );
		setAttr( pName, sb.c_str() );
	}

	void XmlNode::setAttr( const utf8* pName, bool value ) const
	{
		setAttr( pName, value ? "1" : "0" );
	}

	void XmlNode::setName( const utf8* pName ) const
	{
		if ( _pNode == nullptr || pName == nullptr )
			return;
		rapidxml::xml_node<>* pNode = asNode( _pNode );
		pNode->name( pNode->document()->allocate_string( pName ) );
	}

	void XmlNode::setValue( const utf8* pValue ) const
	{
		rapidxml::xml_node<>* pNode = asNode( _pNode );
		if ( pNode == nullptr || pNode->document() == nullptr )
			return;
		const utf8* pSafeValue = pValue != nullptr ? pValue : "";
		pNode->value( pNode->document()->allocate_string( pSafeValue ) );
	}

	void XmlNode::setValue( string_view value ) const
	{
		rapidxml::xml_node<>* pNode = asNode( _pNode );
		if ( pNode == nullptr || pNode->document() == nullptr )
			return;
		pNode->value( allocDocString( pNode->document(), value ) );
	}

	void XmlNode::setValue( int32 value ) const
	{
		StringBuilder<32> sb;
		sb.append( value );
		setValue( sb.c_str() );
	}

	void XmlNode::setValue( uint32 value ) const
	{
		StringBuilder<32> sb;
		sb.append( value );
		setValue( sb.c_str() );
	}

	void XmlNode::setValue( float32 value ) const
	{
		StringBuilder<32> sb;
		sb.append( value );
		setValue( sb.c_str() );
	}

	void XmlNode::setValue( bool value ) const
	{
		setValue( value ? "1" : "0" );
	}

	string XmlNode::toString() const
	{
		rapidxml::xml_node<>* pNode = asNode( _pNode );
		if ( pNode == nullptr )
			return {};
		string result;
		rapidxml::print( std::back_inserter( result ), *pNode, 0 );
		return result;
	}

	XmlNode XmlNode::appendClone( XmlNode src ) const
	{
		if ( isValid() == false || src.isValid() == false )
			return {};

		const utf8* pName = src.name();
		if ( pName == nullptr || pName[0] == '\0' )
			return {};

		XmlNode dst = appendChild( pName );
		for ( XmlAttribute attr = src.firstAttr(); attr; attr = attr.next() )
			dst.appendAttr( attr.name(), attr.value() != nullptr ? attr.value() : "" );

		bool bHasElementChild = false;
		for ( XmlNode childNode = src.child(); childNode; childNode = childNode.next() )
		{
			const utf8* pChildName = childNode.name();
			if ( pChildName != nullptr && pChildName[0] != '\0' )
			{
				bHasElementChild = true;
				dst.appendClone( childNode );
			}
		}

		if ( bHasElementChild == false )
		{
			const utf8* pText = src.text();
			if ( pText != nullptr && pText[0] != '\0' )
				dst.setValue( pText );
		}
		return dst;
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
		{
			_impl->doc.clear();
			_impl->buffer.clear();
		}
	}

	bool XmlDocument::parse( string_view xmlText )
	{
		if ( _impl == nullptr )
			_impl = make_unique<Impl>();
		_impl->doc.clear();
		_impl->buffer.assign( xmlText.begin(), xmlText.end() );
		if ( _impl->buffer.empty() )
			return false;
		_impl->buffer.push_back( '\0' );
		_impl->doc.parse<0>( _impl->buffer.data() );
		return _impl->doc.first_node() != nullptr;
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
			return XmlNode{ _impl->doc.first_node() };
		return XmlNode{ findChild( &_impl->doc, pName, bIgnoreCaseKeys ) };
	}

	XmlNode XmlDocument::appendRoot( const utf8* pName )
	{
		if ( _impl == nullptr )
			_impl = make_unique<Impl>();
		rapidxml::xml_node<>* pRoot = _impl->doc.allocate_node( rapidxml::node_element, _impl->doc.allocate_string( pName ) );
		_impl->doc.append_node( pRoot );
		return XmlNode{ pRoot };
	}

	string XmlDocument::saveToString() const
	{
		if ( _impl == nullptr )
			return "";
		string result;
		rapidxml::print( std::back_inserter( result ), _impl->doc, 0 );
		return result;
	}

	bool XmlDocument::saveFile( string_view absPath ) const
	{
		if ( absPath.empty() )
			return false;
		return FileUtil::writeTextFile( absPath, saveToString() );
	}
} // namespace sw
