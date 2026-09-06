#include "pch.h"

#include "Engine/Graphics/Texture/TextureCache.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Texture/Texture2D.h"

namespace sw
{
    SW_LOG_CALLER( "TextureCache" );

    struct TextureCache::Impl
    {
        struct Entry
        {
            unique_ptr<Texture2D> _texture;
            uint32                _refCount{ 0 };
        };
        unordered_map<string, Entry> _mapEntry;
        std::shared_mutex            _mutex;
    };

    TextureCache::TextureCache()
        : _impl{ make_unique<Impl>() }
    {
    }

    TextureCache::~TextureCache() = default;

    Texture2D* TextureCache::acquire( string_view relativePath, IRHIDevice* pDevice )
    {
        if ( relativePath.empty() || _impl == nullptr || pDevice == nullptr )
            return nullptr;
        const string key = FileUtil::normalizePath( relativePath );

        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        Impl::Entry&                        entry = _impl->_mapEntry[key];
        if ( entry._texture == nullptr )
            entry._texture = make_unique<Texture2D>();
        if ( entry._texture->isReady() == false && entry._texture->loadFromResource( pDevice, key ) == false )
        {
            if ( entry._refCount == 0 )
                _impl->_mapEntry.erase( key );
            return nullptr;
        }
        ++entry._refCount;
        return entry._texture.get();
    }

    void TextureCache::release( string_view relativePath, IRHIDevice* pDevice )
    {
        if ( relativePath.empty() || _impl == nullptr )
            return;
        const string key = FileUtil::normalizePath( relativePath );

        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        auto                                it = _impl->_mapEntry.find( key );
        if ( it == _impl->_mapEntry.end() )
            return;
        if ( it->second._refCount > 0 )
            --it->second._refCount;
        if ( it->second._refCount == 0 )
        {
            if ( it->second._texture != nullptr )
                it->second._texture->shutdown( pDevice );
            _impl->_mapEntry.erase( it );
        }
    }

    void TextureCache::shutdownAllGpu( IRHIDevice* pDevice )
    {
        if ( _impl == nullptr )
            return;
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        for ( auto& [path, entry] : _impl->_mapEntry )
        {
            (void)path;
            if ( entry._texture != nullptr )
                entry._texture->shutdown( pDevice );
        }
    }

    bool TextureCache::reinitializeAll( IRHIDevice* pDevice )
    {
        if ( _impl == nullptr || pDevice == nullptr )
            return false;
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        bool                                bOk = true;
        for ( auto& [path, entry] : _impl->_mapEntry )
        {
            if ( entry._texture != nullptr && entry._texture->loadFromResource( pDevice, path ) == false )
            {
                SW_LOG_ERROR( "TextureCache: failed to re-upload '%#'", path.c_str() );
                bOk = false;
            }
        }
        return bOk;
    }

    void TextureCache::clear()
    {
        if ( _impl == nullptr )
            return;
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        _impl->_mapEntry.clear();
    }
} // namespace sw
