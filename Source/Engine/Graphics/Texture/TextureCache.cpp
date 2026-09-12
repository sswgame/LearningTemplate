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
        if ( entry._texture->isRhiValid() == false && entry._texture->loadFromResource( pDevice, key ) == false )
        {
            if ( entry._refCount == 0 )
                _impl->_mapEntry.erase( key );
            return nullptr;
        }
        ++entry._refCount;
        return entry._texture.get();
    }

    void TextureCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        if ( relativePath.empty() || _impl == nullptr || pDevice == nullptr )
            return;
        const string key = FileUtil::normalizePath( relativePath );

        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        auto                                it = _impl->_mapEntry.find( key );
        if ( it == _impl->_mapEntry.end() || it->second._texture == nullptr )
            return;

        // 이전 프레임이 아직 이 텍스처의 bindless SRV 인덱스를 읽고 있을 수 있다. shutdown() 은
        // 인덱스를 곧바로 프리리스트로 돌려주므로, 기다리지 않고 다시 올리면 같은 인덱스를 받은
        // 다른 텍스처를 읽는 조용한 오염이 된다 (MaterialCache::reload 와 같은 이유).
        pDevice->waitIdle();
        it->second._texture->releaseRhi( pDevice );
        if ( it->second._texture->loadFromResource( pDevice, key ) == false )
            SW_LOG_ERROR( "Hot-Reload failed for Texture %#", key.c_str() );
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
                it->second._texture->releaseRhi( pDevice );
            _impl->_mapEntry.erase( it );
        }
    }

    void TextureCache::clear()
    {
        if ( _impl == nullptr )
            return;
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        _impl->_mapEntry.clear();
    }
} // namespace sw
