#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickCircuitDef.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
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

    bool GimmickCircuitDef::loadFromAsset( const AssetID& assetID )
    {
        XMLDocument doc;
        XMLNode     root;
        if ( XMLReadUtil::loadRootOrWarn( doc, assetID, GimmickCircuitDefInternal::kRootName, root, _sourceName ) == false )
            return false;
        return loadRoot( XMLCatalogRoot{ root } );
    }

    bool GimmickCircuitDef::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        XMLDocument doc;
        XMLNode     root;
        _sourceName = string( sourceName.empty() ? string_view( "<gimmick xml>" ) : sourceName );
        if ( XMLReadUtil::parseRootOrWarn( doc, xmlText, _sourceName, GimmickCircuitDefInternal::kRootName, root ) == false )
            return false;
        return loadRoot( XMLCatalogRoot{ root } );
    }

    bool GimmickCircuitDef::loadRoot( const XMLCatalogRoot& root )
    {
        clear();
        // 적지 않으면 clear() 가 둔 기본 스텝 길이(constant::time::kDefaultFixedStepSeconds)다. 0 이하 · 깨진 값은 오류다.
        GimmickCircuitRootParams rootParams{ _stepTime };
        const bool               bRoot         = root.readProperties( rootParams, _sourceName ); // stepTime > 0 은 PROPERTY 가 본다
        _stepTime                              = bRoot ? rootParams._stepTime : _stepTime;
        using Element                          = XMLCatalogElement<GimmickCircuitDef>;
        static constexpr Element kArrElement[] = {
            Element::of<&GimmickCircuitDef::loadNode>( "Node" ).uncounted(),
            Element::of<&GimmickCircuitDef::loadWire>( "Wire" ).uncounted(),
        };
        return XMLCatalogReadResult::readInDocumentOrder( *this, root, _sourceName, kArrElement ).isClean() && bRoot;
    }

    bool GimmickCircuitDef::loadNode( const XMLNode& element, string_view sourceName )
    {
        const utf8* pID   = element.findAttribute( "id" );
        const utf8* pKind = element.findAttribute( "kind" );
        if ( pID == nullptr || pKind == nullptr )
        {
            SW_LOG_WARNING_ID( "GimmickCircuitDef.NodeNeedsBothId", "%#: <Node> needs both id and kind", sourceName );
            return false;
        }
        GimmickNodeDef& node = addNode( hashed_string( pID ), hashed_string( pKind ) );
        for ( XMLAttribute attribute = element.getFirstAttribute(); attribute; attribute = attribute.getNext() )
        {
            const bool bReserved = StringUtil::equals( attribute.getName(), "id" ) || StringUtil::equals( attribute.getName(), "kind" );
            if ( bReserved == false )
                node.setParam( hashed_string( attribute.getName() ), string_view( attribute.getValue() ) );
        }
        return true;
    }

    bool GimmickCircuitDef::loadWire( const XMLNode& element, string_view sourceName )
    {
        GimmickWireParams wire;
        if ( XMLFields::readProperties( element, wire, sourceName ) == false )
            return false;
        if ( addWire( wire._from, wire._to, wire._bInvert ) == false )
        {
            SW_LOG_WARNING_ID( "GimmickCircuitDef.WireNeedsNodePort", "%#: <Wire from=\"%#\" to=\"%#\"> needs node.port on both ends", sourceName, wire._from, wire._to );
            return false;
        }
        return true;
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
        _stepTime = constant::time::kDefaultFixedStepSeconds;
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

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<GimmickCircuitDef>::acquire`). 파일 규칙이 없는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( GimmickCircuitDef, GimmickCircuitDef, SW_GF_API, AssetTypeDef::makeCacheOnly( "GimmickCircuitDef", AssetTypeLoader::of<GimmickCircuitDef>( MemoryTag::Game ) ) );
} // namespace sw
