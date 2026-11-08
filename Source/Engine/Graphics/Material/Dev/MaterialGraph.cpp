#include "pch.h"

#include "Engine/Graphics/Material/Dev/MaterialGraph.h"

#include "Core/Log/Logger.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"

namespace sw
{
    SW_ASSET_TYPE( MaterialGraph, MaterialGraphDef::kAssetType );

    const MaterialGraphValue* MaterialGraphNodeDef::findValue( string_view name ) const
    {
        for ( const MaterialGraphValue& value : _listValue )
        {
            if ( value._name == name )
                return &value;
        }
        return nullptr;
    }

    const MaterialGraphLink* MaterialGraphNodeDef::findLink( string_view pin ) const
    {
        for ( const MaterialGraphLink& link : _listLink )
        {
            if ( link._pin == pin )
                return &link;
        }
        return nullptr;
    }

    bool MaterialGraphDef::loadFromAsset( const AssetID& assetID )
    {
        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "MaterialGraph.FileNotFound", "Material graph not found: %#", assetID.view() );
            return false;
        }
        return loadComposed( text, assetID.view(), ResourceContext::forType( *StaticType() ).withAsset( assetID ) );
    }

    bool MaterialGraphDef::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        return loadComposed( xmlText, sourceName, ResourceContext::forType( *StaticType() ) );
    }

    bool MaterialGraphDef::loadComposed( string_view xmlText, string_view sourceName, const ResourceContext& context )
    {
        *this = MaterialGraphDef{};
        string flatText;
        bool   bComposed = false; // 펼치지 않으면 flatText 는 빈 채로 남는다
        if ( ResourceComposer::compose( xmlText, sourceName, context, flatText, bComposed ) == false )
            return false;
        if ( XMLSerializer::deserialize( this, *StaticType(), flatText.empty() ? xmlText : string_view( flatText ) ) == false )
        {
            SW_LOG_ERROR_ID( "MaterialGraph.Unreadable", "%#: material graph could not be read or holds unknown keys / values", sourceName );
            return false;
        }
        if ( _formatVersion != kAssetType._formatVersion )
        {
            SW_LOG_ERROR_ID( "MaterialGraph.WrongVersion", "%#: formatVersion %# is not the current %#", sourceName, _formatVersion, kAssetType._formatVersion );
            return false;
        }
        return true;
    }

    string MaterialGraphDef::saveToXMLText() const
    {
        return XMLSerializer::serialize( this, *StaticType() );
    }

    bool MaterialGraphDef::saveToFile( const AbsolutePath& absPath, const AssetID& baseID ) const
    {
        if ( baseID.isEmpty() )
            return ReflectedXMLFile::saveDesc( absPath, *this );
        return ReflectedXMLFile::saveDescAsChild( absPath, *this, baseID );
    }

    optional<uint32> MaterialGraphDef::findNodeIndex( string_view name ) const
    {
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            if ( _listNode[nodeIndex]._name == name )
                return static_cast<uint32>( nodeIndex );
        }
        return nullopt;
    }
} // namespace sw
