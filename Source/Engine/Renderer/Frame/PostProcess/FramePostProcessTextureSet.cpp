#include "pch.h"

#include "Engine/Renderer/Frame/PostProcess/FramePostProcessTextureSet.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Serialization/Serializer/DataPath.h"

namespace sw
{
    FramePostProcessTextureSet::FramePostProcessTextureSet()
        : _listEntry{}
    {
    }

    FramePostProcessTextureSet::~FramePostProcessTextureSet() = default;

    const Texture2D* FramePostProcessTextureSet::findReady( const DataPath& path, const utf8* pKind )
    {
        if ( path.isSet() == false || engine::areEngineServicesBound() == false )
            return nullptr;
        const hashed_string key( path.get().c_str() );
        Entry*              pEntry = nullptr;
        for ( Entry& entry : _listEntry )
        {
            if ( entry._path == key )
                pEntry = &entry;
        }
        if ( pEntry == nullptr )
        {
            Entry entry{};
            entry._path = key;
            entry._load = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().request( key.viewOrEmpty(), AssetLoadStart::Async );
            _listEntry.push_back( std::move( entry ) );
            pEntry = &_listEntry.back();
        }
        pEntry->_bUsed = SW_TRUE;
        if ( pEntry->_load.hasFailed() && pEntry->_bFailureReported == SW_FALSE )
        {
            SW_LOG_ERROR_ID( "FramePostProcessTextureSet.LoadFailed", "Post-process %# '%#' could not be loaded (%#) - the effect is left out", pKind, key.c_str(), pEntry->_load.getError().c_str() );
            pEntry->_bFailureReported = SW_TRUE;
        }
        return pEntry->_load.isReady() ? pEntry->_load.getOperation()->getTexture() : nullptr;
    }

    void FramePostProcessTextureSet::pruneUnused()
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

    void FramePostProcessTextureSet::release()
    {
        _listEntry.clear();
    }
} // namespace sw
