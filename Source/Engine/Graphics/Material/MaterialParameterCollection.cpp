#include "pch.h"

#include "Engine/Graphics/Material/MaterialParameterCollection.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/BuiltInAssetCache.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"
#include "Engine/Serialization/Serializer/ReloadResult.h"

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
                if ( _listParameter[index]._global != MaterialGlobalSlot::None && _listParameter[otherIndex]._global == _listParameter[index]._global )
                {
                    SW_LOG_ERROR_ID( "MaterialParameterCollection.GlobalRepeated", "Material parameter collection '%#': '%#' and '%#' feed the same shader global", sourceLabel,
                                     _listParameter[otherIndex]._name, _listParameter[index]._name );
                    bValid = false;
                }
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
            _listEntry.push_back( Entry{ hashed_string( string_view( parameter._name ) ), parameter._value, parameter._global } );
        }
    }

    void MaterialParameterCollection::replaceValues( MaterialParameterCollectionDef desc )
    {
        _listEntry.clear();
        _listEntry.reserve( desc._listParameter.size() );
        for ( const MaterialCollectionParameterDef& parameter : desc._listParameter )
        {
            _listEntry.push_back( Entry{ hashed_string( string_view( parameter._name ) ), parameter._value, parameter._global } );
        }
        _generation.bump( nullptr ); // 묶음 값은 그것을 읽는 머티리얼 모두에 닿는다 — 주인을 모르는 변경이라 읽는 쪽이 전부 다시 본다
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
            if ( pEntry->_global != MaterialGlobalSlot::None )
                return true;             // 전역 값은 프레임 상수로 간다 — 다시 볼 머티리얼이 없다
            _generation.bump( nullptr ); // 묶음 값은 그것을 읽는 머티리얼 모두에 닿는다 — 주인을 모르는 변경이라 읽는 쪽이 전부 다시 본다
        }
        return true;
    }

    bool MaterialParameterCollection::isGlobal( const hashed_string& name ) const
    {
        const Entry* pEntry = VectorUtil::findBy<&Entry::_name>( _listEntry, name );
        return pEntry != nullptr && pEntry->_global != MaterialGlobalSlot::None;
    }

    bool MaterialParameterCollection::writeGlobals( MaterialGlobals& inoutGlobals ) const
    {
        bool bWeather{ false };
        bool bRain{ false };
        for ( const Entry& entry : _listEntry )
        {
            switch ( entry._global )
            {
                case MaterialGlobalSlot::None:
                    break;
                case MaterialGlobalSlot::Weather:
                {
                    inoutGlobals._weather = entry._value;
                    bWeather              = true;
                    break;
                }
                case MaterialGlobalSlot::Rain:
                {
                    inoutGlobals._rain = entry._value;
                    bRain              = true;
                    break;
                }
            }
        }
        return bWeather && bRain;
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

    ReloadResult MaterialParameterCollectionCache::reload( const AssetID& assetID )
    {
        if ( findLive( assetID ) == nullptr )
            return ReloadResult::NotTracked;
        return reloadLive( assetID ) ? ReloadResult::Reloaded : ReloadResult::Failed; // 읽지 못하면 읽기가 오류를 남겼다 — 지금 값을 그대로 쓴다
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

    SW_BUILT_IN_ASSET_CACHE( MaterialParameterCollectionCache, MaterialParameterCollectionCache );
    SW_ASSET_TYPE( MaterialParameterCollection, MaterialParameterCollectionDef::kAssetType );
} // namespace sw
