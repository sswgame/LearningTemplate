#include "pch.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitDef.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "GimmickCircuitDef" );

    namespace
    {
        struct GimmickCircuitDefInternal
        {
            static constexpr const utf8* kRootName = "GimmickCircuit";
        };
    } // namespace
} // namespace sw

namespace sw
{
    void GimmickNodeDef::setParam( const hashed_string& name, string_view text )
    {
        for ( GimmickParamDef& param : _listParam )
        {
            if ( param._name == name )
            {
                param._text = string( text );
                return;
            }
        }
        GimmickParamDef param;
        param._name = name;
        param._text = string( text );
        _listParam.push_back( param );
    }

    const string* GimmickNodeDef::findParam( const hashed_string& name ) const
    {
        for ( const GimmickParamDef& param : _listParam )
        {
            if ( param._name == name )
                return &param._text;
        }
        return nullptr;
    }

    bool GimmickCircuitDef::loadFromResource( string_view path )
    {
        XMLDocument doc;
        XMLNode     root;
        if ( GameDataXML::loadRoot( doc, path, GimmickCircuitDefInternal::kRootName, root, _sourceName ) == false )
            return false;
        return loadRoot( root );
    }

    bool GimmickCircuitDef::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        XMLDocument doc;
        XMLNode     root;
        _sourceName = string( sourceName.empty() ? string_view( "<gimmick xml>" ) : sourceName );
        if ( GameDataXML::parseRoot( doc, xmlText, _sourceName, GimmickCircuitDefInternal::kRootName, root ) == false )
            return false;
        return loadRoot( root );
    }

    bool GimmickCircuitDef::loadRoot( const XMLNode& root )
    {
        clear();
        // 적지 않으면 clear() 가 둔 기본 스텝 길이(kDefaultStepTime)다. 0 이하 · 깨진 값은 오류다.
        bool bValid = GameDataXML::readOptionalNumber( root, "stepTime", _sourceName, 0.0f, MathUtil::kMaxFloat, _stepTime );
        if ( _stepTime <= 0.0f )
        {
            SW_LOG_ERROR( "%#: stepTime must be positive", _sourceName );
            bValid = false;
        }
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Node" ) )
            {
                const utf8* pID   = child.findAttribute( "id" );
                const utf8* pKind = child.findAttribute( "kind" );
                if ( pID == nullptr || pKind == nullptr )
                {
                    SW_LOG_WARNING( "%#: <Node> needs both id and kind", _sourceName );
                    bValid = false;
                    continue;
                }
                GimmickNodeDef& node = addNode( hashed_string( pID ), hashed_string( pKind ) );
                for ( XMLAttribute attribute = child.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    const bool bReserved = StringUtil::equals( attribute.getName(), "id" ) || StringUtil::equals( attribute.getName(), "kind" );
                    if ( bReserved == false )
                        node.setParam( hashed_string( attribute.getName() ), string_view( attribute.getValue() ) );
                }
                continue;
            }
            if ( StringUtil::equals( child.getName(), "Wire" ) )
            {
                bool bInvert = false; // 적지 않으면 뒤집지 않는다
                if ( GameDataXML::readOptionalBool( child, "invert", bInvert ) == false )
                {
                    bValid = false;
                    continue;
                }
                if ( addWire( child.getAttributeText( "from" ), child.getAttributeText( "to" ), bInvert ) == false )
                {
                    SW_LOG_WARNING( "%#: <Wire from=\"%#\" to=\"%#\"> needs node.port on both ends", _sourceName, child.getAttributeText( "from" ),
                                    child.getAttributeText( "to" ) );
                    bValid = false;
                }
                continue;
            }
            SW_LOG_WARNING( "%#: unknown element <%#> in <GimmickCircuit>", _sourceName, child.getName() );
            bValid = false;
        }
        return bValid;
    }

    GimmickNodeDef& GimmickCircuitDef::addNode( const hashed_string& id, const hashed_string& kind )
    {
        GimmickNodeDef node;
        node._id   = id;
        node._kind = kind;
        _listNode.push_back( node );
        return _listNode.back();
    }

    bool GimmickCircuitDef::addWire( string_view from, string_view to, bool bInvert )
    {
        GimmickWireDef wire;
        if ( splitPortReference( from, wire._fromNode, wire._fromPort ) == false || splitPortReference( to, wire._toNode, wire._toPort ) == false )
            return false;
        wire._bInvert = bInvert ? SW_TRUE : SW_FALSE;
        _listWire.push_back( wire );
        return true;
    }

    void GimmickCircuitDef::clear()
    {
        _listNode.clear();
        _listWire.clear();
        _stepTime = kDefaultStepTime;
    }

    bool GimmickCircuitDef::splitPortReference( string_view text, hashed_string& outNode, hashed_string& outPort )
    {
        const string_view trimmed            = StringUtil::trim( text );
        const size_t      dot                = trimmed.rfind( '.' );
        const bool        bMalformedPortPath = dot == string_view::npos || dot == 0 || dot + 1 >= trimmed.size();
        if ( bMalformedPortPath )
            return false;
        outNode = hashed_string( string( trimmed.substr( 0, dot ) ) );
        outPort = hashed_string( string( trimmed.substr( dot + 1 ) ) );
        return true;
    }

    const GimmickNodeDef* GimmickCircuitDef::findNode( const hashed_string& id ) const
    {
        return VectorUtil::findBy<&GimmickNodeDef::_id>( _listNode, id );
    }
} // namespace sw
