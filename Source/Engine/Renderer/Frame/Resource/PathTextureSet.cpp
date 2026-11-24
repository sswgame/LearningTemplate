#include "pch.h"

#include "Engine/Renderer/Frame/Resource/PathTextureSet.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Serialization/Serializer/DataPath.h"

namespace sw
{
    PathTextureSet::PathTextureSet()
        : _listEntry{}
    {
    }

    PathTextureSet::~PathTextureSet() = default;

    void PathTextureSet::request( const hashed_string& path )
    {
        if ( engine::areEngineServicesBound() == false )
            return;
        (void)getOrCreateEntry( path );
    }

    const Texture2D* PathTextureSet::requestReady( const DataPath& path, const utf8* pKind )
    {
        if ( path.isSet() == false || engine::areEngineServicesBound() == false )
            return nullptr;
        const hashed_string key( path.get().c_str() );
        Entry&              entry = getOrCreateEntry( key );
        if ( entry._load.hasFailed() && entry._bFailureReported == SW_FALSE )
        {
            SW_LOG_ERROR_ID( "PathTextureSet.LoadFailed", "%# '%#' could not be loaded (%#) - it is left out", pKind, key.c_str(), entry._load.getError().c_str() );
            entry._bFailureReported = SW_TRUE;
        }
        return entry._load.isReady() ? entry._load.getOperation()->getTexture() : nullptr;
    }

    const Texture2D* PathTextureSet::findReady( const hashed_string& path ) const
    {
        const Entry* pEntry = findEntry( path );
        return pEntry != nullptr && pEntry->_load.isReady() ? pEntry->_load.getOperation()->getTexture() : nullptr;
    }

    RHITextureSRVIndex PathTextureSet::findReadySRV( const hashed_string& path ) const
    {
        const Texture2D* pTexture = findReady( path );
        return pTexture != nullptr && pTexture->isRHIValid() ? pTexture->getSRV() : kInvalidDescriptorIndex;
    }

    bool PathTextureSet::isPending( const hashed_string& path ) const
    {
        const Entry* pEntry = findEntry( path );
        return pEntry != nullptr && pEntry->_load.isValid() && pEntry->_load.isReady() == false && pEntry->_load.hasFailed() == false;
    }

    void PathTextureSet::pruneUnused()
    {
        for ( size_t index = 0; index < _listEntry.size(); )
        {
            if ( _listEntry[index]._bUsed == SW_TRUE )
            {
                _listEntry[index]._bUsed = SW_FALSE;
                ++index;
                continue;
            }
            _listEntry.erase( _listEntry.begin() + static_cast<std::ptrdiff_t>( index ) );
        }
    }

    void PathTextureSet::release()
    {
        _listEntry.clear();
    }

    PathTextureSet::Entry& PathTextureSet::getOrCreateEntry( const hashed_string& path )
    {
        for ( Entry& entry : _listEntry )
        {
            if ( entry._path == path )
            {
                entry._bUsed = SW_TRUE;
                return entry;
            }
        }
        // 렌더 스레드가 디스크를 기다리지 않게 워커가 읽는다 — 다 읽기 전 프레임은 그 텍스처를 쓰는 그리기를 뺀다.
        Entry& added = _listEntry.emplace_back();
        added._path  = path;
        added._load  = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().request( path.view(), AssetLoadStart::Async, TextureStreamingGroup::NeverStream );
        added._bUsed = SW_TRUE;
        return added;
    }

    const PathTextureSet::Entry* PathTextureSet::findEntry( const hashed_string& path ) const
    {
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._path == path )
                return &entry;
        }
        return nullptr;
    }
} // namespace sw
