#include "pch.h"

#include "Engine/Graphics/Material/MaterialParameterCollection.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/BuiltInAssetCache.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"

namespace sw
{
    namespace
    {
        struct MaterialParameterCollectionInternal
        {
            /** @brief `matparam <경로> <이름> <x> [y] [z] [w]` — 묶음의 값을 바꿉니다. 시나리오가 런타임 변경을 확인할 때 쓴다. */
            static bool runSetValue( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() < 3 || listArgument.size() > 6 )
                {
                    outReply = "usage: matparam <collection> <name> <x> [y] [z] [w]";
                    return false;
                }
                float4 value{};
                for ( size_t index = 2; index < listArgument.size(); ++index )
                {
                    float32 component{ 0.0f };
                    if ( StringUtil::parseFloat( listArgument[index], component ) == false )
                    {
                        outReply = "not a number: " + listArgument[index];
                        return false;
                    }
                    ( index == 2 ? value._x : index == 3 ? value._y
                                          : index == 4   ? value._z
                                                         : value._w ) = component;
                }
                const shared_ptr<MaterialParameterCollection> pCollection = MaterialParameterCollection::acquire( AssetID{ listArgument[0] } );
                if ( pCollection == nullptr || pCollection->setValue( hashed_string( string_view( listArgument[1] ) ), value ) == false )
                {
                    outReply = "could not set " + listArgument[1] + " of " + listArgument[0];
                    return false;
                }
                outReply = "set " + listArgument[1];
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( MaterialParameter, "matparam", "matparam <collection> <name> <x> [y] [z] [w]", "Set a value of a material parameter collection; every material bound to it follows",
                    &MaterialParameterCollectionInternal::runSetValue );
} // namespace sw

namespace sw
{
    bool MaterialParameterCollectionDef::validate( string_view sourceLabel ) const
    {
        bool bValid = true;
        for ( size_t index = 0; index < _listParameter.size(); ++index )
        {
            bool bBadName = _listParameter[index]._name.empty();
            for ( size_t otherIndex = 0; otherIndex < index; ++otherIndex )
            {
                bBadName = bBadName || _listParameter[otherIndex]._name == _listParameter[index]._name;
            }
            if ( bBadName )
            {
                SW_LOG_ERROR_ID( "MaterialParameterCollection.BadName", "Material parameter collection '%#': parameter %# has an empty or repeated name '%#'", sourceLabel, index, _listParameter[index]._name );
                bValid = false;
            }
        }
        return bValid;
    }

    shared_ptr<MaterialParameterCollection> MaterialParameterCollection::acquire( const AssetID& assetID )
    {
        return engine::getSubsystem<AssetManager>().getAssetCache<MaterialParameterCollectionCache>().acquire( assetID );
    }

    MaterialParameterCollection::MaterialParameterCollection( MaterialParameterCollectionDef desc )
        : _listEntry{}
        , _generation{ 1u }
    {
        _listEntry.reserve( desc._listParameter.size() );
        for ( const MaterialCollectionParameterDef& parameter : desc._listParameter )
        {
            _listEntry.push_back( Entry{ hashed_string( string_view( parameter._name ) ), parameter._value } );
        }
    }

    void MaterialParameterCollection::replaceValues( MaterialParameterCollectionDef desc )
    {
        _listEntry.clear();
        _listEntry.reserve( desc._listParameter.size() );
        for ( const MaterialCollectionParameterDef& parameter : desc._listParameter )
        {
            _listEntry.push_back( Entry{ hashed_string( string_view( parameter._name ) ), parameter._value } );
        }
        _generation.bump();
    }

    const float4* MaterialParameterCollection::findValue( const hashed_string& name ) const
    {
        const Entry* pEntry = VectorUtil::findBy<&Entry::_name>( _listEntry, name );
        return pEntry != nullptr ? &pEntry->_value : nullptr;
    }

    bool MaterialParameterCollection::setValue( const hashed_string& name, const float4& value )
    {
        Entry* pEntry = VectorUtil::findBy<&Entry::_name>( _listEntry, name );
        if ( pEntry == nullptr )
        {
            SW_LOG_ERROR_ID( "MaterialParameterCollection.UnknownParameter", "Material parameter collection has no parameter '%#'", name.c_str() );
            return false;
        }
        const bool bSame = pEntry->_value._x == value._x && pEntry->_value._y == value._y && pEntry->_value._z == value._z && pEntry->_value._w == value._w;
        if ( bSame == false )
        {
            pEntry->_value = value;
            _generation.bump();
        }
        return true;
    }

    string MaterialParameterCollection::makePropertyText( const float4& value, uint32 componentCount )
    {
        const float32 arrComponent[] = { value._x, value._y, value._z, value._w };
        string        text;
        for ( uint32 index = 0; index < componentCount && index < 4u; ++index )
        {
            if ( index > 0 )
                text += " ";
            text += formatToString( "%#", arrComponent[index] );
        }
        return text;
    }
} // namespace sw

namespace sw
{
    MaterialParameterCollectionCache::MaterialParameterCollectionCache()
        : AssetCacheBase{ CacheTracking::counted( kAssetKindName ) }
        , _table{}
    {
    }

    bool MaterialParameterCollectionCache::readDesc( const AssetID& assetID, MaterialParameterCollectionDef& outDesc )
    {
        return ReflectedXMLFile::loadDesc( assetID, outDesc ) && outDesc.validate( assetID.view() );
    }

    shared_ptr<MaterialParameterCollection> MaterialParameterCollectionCache::acquire( const AssetID& assetID )
    {
        const string                            key{ assetID.view() };
        shared_ptr<MaterialParameterCollection> pLive = _table.findLive( key );
        if ( pLive != nullptr )
        {
            getCacheStats().recordHit();
            return pLive;
        }
        getCacheStats().recordMiss();
        MaterialParameterCollectionDef desc;
        if ( readDesc( assetID, desc ) == false )
            return nullptr;
        return _table.insertOrGetLive( key, sw::make_shared<MaterialParameterCollection>( std::move( desc ) ) );
    }

    shared_ptr<MaterialParameterCollection> MaterialParameterCollectionCache::findLive( const AssetID& assetID ) const
    {
        return _table.findLive( string{ assetID.view() } );
    }

    bool MaterialParameterCollectionCache::reloadLive( const AssetID& assetID )
    {
        const shared_ptr<MaterialParameterCollection> pLive = findLive( assetID );
        MaterialParameterCollectionDef                desc;
        if ( pLive == nullptr || readDesc( assetID, desc ) == false )
            return false;
        pLive->replaceValues( std::move( desc ) );
        SW_LOG_INFO_ID( "MaterialParameterCollection.Reloaded", "Material parameter collection '%#' reloaded (generation %#)", assetID.view(), pLive->getGeneration() );
        return true;
    }

    void MaterialParameterCollectionCache::reload( const AssetID& assetID )
    {
        (void)reloadLive( assetID ); // 읽지 못하면 읽기가 오류를 남겼다 — 지금 값을 그대로 쓴다
    }

    bool MaterialParameterCollectionCache::isCached( string_view relativePath ) const
    {
        return _table.findLive( string{ relativePath } ) != nullptr;
    }

    void MaterialParameterCollectionCache::clear()
    {
        _table.clear();
        getCacheStats().recordClear();
    }

    bool MaterialParameterCollectionCache::reloadComposed( string_view relativePath )
    {
        return reloadLive( AssetID{ relativePath } );
    }

    SW_BUILT_IN_ASSET_CACHE( MaterialParameterCollectionCache );
    SW_ASSET_TYPE( MaterialParameterCollection, MaterialParameterCollectionDef::kAssetType );
} // namespace sw
