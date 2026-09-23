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

        // 이전 프레임이 아직 이 텍스처의 bindless SRV 인덱스를 읽고 있을 수 있다. releaseRhi() 는
        // 인덱스를 곧바로 프리리스트로 돌려주므로, 기다리지 않고 다시 올리면 같은 인덱스를 받은
        // 다른 텍스처를 읽는 조용한 오염이 된다(MaterialCache::reload 와 같은 이유).
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

    bool TextureCache::isCached( string_view relativePath ) const
    {
        if ( relativePath.empty() || _impl == nullptr )
            return false;

        const string                        key = FileUtil::normalizePath( relativePath );
        std::shared_lock<std::shared_mutex> lock{ _impl->_mutex };
        return _impl->_mapEntry.find( key ) != _impl->_mapEntry.end();
    }

    size_t TextureCache::getCachedCount() const
    {
        if ( _impl == nullptr )
            return 0;
        std::shared_lock<std::shared_mutex> lock{ _impl->_mutex };
        return _impl->_mapEntry.size();
    }

    /**
     * @brief 표를 통째로 비웁니다. **GPU 자원을 돌려주지는 않습니다.**
     * @warning **디바이스가 죽은 뒤에만 부를 수 있습니다.** `IAssetCache::clear()` 는 디바이스를
     *          인자로 받지 않고(그 이유는 `MaterialCache.h` 머리말에 있습니다. 캐시가 디바이스를
     *          들고 있으면 백엔드 교체 때 죽은 포인터가 됩니다), 캐시도 들고 있지 않으므로 여기서
     *          `releaseRhi` 를 부를 방법이 없습니다. 살아 있는 디바이스에서 부르면 텍스처 핸들과
     *          **bindless SRV 인덱스**가 그대로 샙니다. 후자는 프리리스트로 영영 안 돌아옵니다.
     *
     *          지금 이 함수로 오는 길은 하나뿐이고 그 순서가 이 계약을 지킵니다:
     *          `EngineLoop::shutdown` 이 `_rhi->shutdown()` 을 **먼저** 부르고, 그것이
     *          `RHIRenderResource` 등록부 전체에 `releaseRhi` 를 밀어 둔 뒤에야
     *          `ResourceManager::shutdown` → `clearAssetCaches()` 가 여기에 닿습니다.
     *          참조가 0 이 되어 내리는 평소 경로는 `release()` 이고, 그쪽은 제대로 돌려줍니다.
     */
    void TextureCache::clear()
    {
        if ( _impl == nullptr )
            return;
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        _impl->_mapEntry.clear();
    }
} // namespace sw
