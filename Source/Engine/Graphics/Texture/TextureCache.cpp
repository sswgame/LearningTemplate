#include "pch.h"

#include "Engine/Graphics/Texture/TextureCache.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Resource/Cache/BuiltInAssetCache.h"

#include <condition_variable>

namespace sw
{
    SW_LOG_CALLER( "TextureCache" );

    struct TextureCache::Impl
    {
        struct Entry
        {
            unique_ptr<Texture2D> _texture;
            uint32                _refCount{ 0 };
            bool                  _bLoading{ false };   ///< 잠금 밖에서 올리는 중 — 다른 스레드는 `_cvLoaded` 로 기다린다
            bool                  _bPublished{ false }; ///< 한 번이라도 올라왔는가 — `find` 는 이것만 내준다
        };
        struct RenderTargetSize
        {
            uint32 _width{ TextureCache::kDefaultRenderTargetSize };
            uint32 _height{ TextureCache::kDefaultRenderTargetSize };
        };
        unordered_map<string, Entry>            _mapEntry;
        unordered_map<string, RenderTargetSize> _mapRenderTargetSize; ///< `declareRenderTarget` 이 알린 크기
        std::shared_mutex                       _mutex;
        std::condition_variable_any             _cvLoaded;              ///< 항목의 `_bLoading` 이 내려갈 때 깨운다
        atomic<uint32>                          _reloadGeneration{ 0 }; ///< reload 횟수 — 머티리얼이 SRV 인덱스를 다시 받을 때를 안다
    };

    TextureCache::TextureCache()
        : _impl{ make_unique<Impl>() }
    {
    }

    TextureCache::~TextureCache() = default;

    Texture2D* TextureCache::acquire( string_view relativePath, IRHIDevice* pDevice )
    {
        SW_MEMORY_SCOPE( Texture );
        if ( relativePath.empty() || _impl == nullptr || pDevice == nullptr )
            return nullptr;
        const string key = FileUtil::normalizePath( relativePath );

        // 디스크 읽기와 GPU 생성은 잠금 밖에서 한다. 잠금 안에서는 "진행 중" 표시(_bLoading)만 세우고, 같은 키를 동시에
        // 원한 스레드는 그 표시가 내려갈 때까지 기다렸다가 결과를 받는다(이중 생성 없음). Texture2D 는 unique_ptr 이라
        // 맵이 원소를 옮겨도 주소가 그대로다. 진행 중인 항목은 release · 실패 정리가 지우지 않는다.
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        Texture2D*                          pTexture = nullptr;
        for ( ;; )
        {
            const auto it = _impl->_mapEntry.find( key );
            if ( it == _impl->_mapEntry.end() )
            {
                Impl::Entry& entry = _impl->_mapEntry[key];
                entry._texture     = make_unique<Texture2D>();
                entry._bLoading    = true;
                pTexture           = entry._texture.get();
                break;
            }
            Impl::Entry& entry = it->second;
            if ( entry._bLoading )
            {
                _impl->_cvLoaded.wait( lock );
                continue;
            }
            if ( entry._texture->isRHIValid() )
            {
                ++entry._refCount;
                return entry._texture.get();
            }
            // 들고는 있지만 GPU 자원이 없다(리로드 실패 등) — 같은 객체에 다시 올린다.
            entry._bLoading = true;
            pTexture        = entry._texture.get();
            break;
        }
        const bool             bRenderTarget = isRenderTargetPath( key );
        Impl::RenderTargetSize size{};
        if ( bRenderTarget )
        {
            const auto sizeIter = _impl->_mapRenderTargetSize.find( key );
            if ( sizeIter != _impl->_mapRenderTargetSize.end() )
                size = sizeIter->second;
        }
        lock.unlock();

        // 렌더 텍스처는 파일이 없다. 알린 크기(없으면 기본)로 렌더 타깃을 만든다.
        const bool bReady = bRenderTarget ? pTexture->createRenderTarget( pDevice, key, size._width, size._height, constant::kOffscreenColorFormat )
                                          : pTexture->loadFromResource( pDevice, key );

        lock.lock();
        const auto it = _impl->_mapEntry.find( key );
        SW_ASSERT( it != _impl->_mapEntry.end() && it->second._bLoading );
        Impl::Entry& entry = it->second;
        entry._bLoading    = false;
        if ( bReady )
        {
            entry._bPublished = true;
            ++entry._refCount;
        }
        else
        {
            pTexture = nullptr;
            if ( entry._refCount == 0 )
                _impl->_mapEntry.erase( it );
        }
        lock.unlock();
        _impl->_cvLoaded.notify_all();
        return pTexture;
    }

    bool TextureCache::isRenderTargetPath( string_view relativePath )
    {
        return StringUtil::startsWith( relativePath, string_view{ kRenderTargetPrefix }, true );
    }

    void TextureCache::declareRenderTarget( string_view relativePath, uint32 width, uint32 height )
    {
        if ( _impl == nullptr || isRenderTargetPath( relativePath ) == false || width == 0 || height == 0 )
            return;
        const string                        key = FileUtil::normalizePath( relativePath );
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        Impl::RenderTargetSize&             size = _impl->_mapRenderTargetSize[key];
        size._width                              = width;
        size._height                             = height;
    }

    void TextureCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        SW_MEMORY_SCOPE( Texture );
        // 렌더 텍스처는 디스크에 없다 — 다시 읽을 것이 없다.
        if ( relativePath.empty() || _impl == nullptr || pDevice == nullptr || isRenderTargetPath( relativePath ) )
            return;
        const string key = FileUtil::normalizePath( relativePath );

        // 기다림(waitIdle)과 다시 올리기는 잠금 밖에서 한다. 잠금을 쥔 채 waitIdle 을 부르면 같은 잠금을 기다리는
        // 렌더 스레드와 서로 기다릴 수 있다. 진행 중 표시를 세워 두면 그사이 acquire 는 기다리고 release 는 지우지 않는다.
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        Texture2D*                          pTexture = nullptr;
        for ( ;; )
        {
            const auto it = _impl->_mapEntry.find( key );
            if ( it == _impl->_mapEntry.end() || it->second._texture == nullptr )
                return;
            if ( it->second._bLoading )
            {
                _impl->_cvLoaded.wait( lock );
                continue;
            }
            it->second._bLoading = true;
            pTexture             = it->second._texture.get();
            break;
        }
        lock.unlock();

        // 이전 프레임이 아직 이 텍스처와 그 bindless SRV 인덱스를 읽고 있을 수 있다. 인덱스를 돌려주는 때는 백엔드마다 다르다:
        // DX12 · Vulkan 은 해제 큐(enqueueGPURelease)를 거쳐 GPU 펜스가 지난 뒤에 프리리스트로 돌려주지만,
        // DX11 · GL 은 unregisterBindlessTexture 에서 곧바로 돌려준다. 그 둘에서 기다리지 않고 다시 올리면 같은 인덱스를 받은
        // 다른 텍스처를 읽는 조용한 오염이 된다(MaterialCache::reload 와 같은 이유). 텍스처 객체 해제도 같은 대기에 기댄다.
        pDevice->waitIdle();
        pTexture->releaseRHI( pDevice );
        if ( pTexture->loadFromResource( pDevice, key ) == false )
            SW_LOG_ERROR( "Hot-Reload failed for Texture %#", key.c_str() );
        // 다시 올린 텍스처는 새 SRV 인덱스를 받았다(실패했으면 인덱스가 없다). 옛 인덱스는 이미 돌려줬으므로 빌려 간 머티리얼이 다시 받게 한다.
        _impl->_reloadGeneration.fetch_add( 1u, std::memory_order_acq_rel );

        lock.lock();
        const auto it = _impl->_mapEntry.find( key );
        SW_ASSERT( it != _impl->_mapEntry.end() && it->second._bLoading );
        it->second._bLoading = false;
        // 다시 올리는 사이 마지막 참조가 놓였으면 release 대신 여기서 내린다.
        if ( it->second._refCount == 0 )
        {
            pTexture->releaseRHI( pDevice );
            _impl->_mapEntry.erase( it );
        }
        lock.unlock();
        _impl->_cvLoaded.notify_all();
    }

    const Texture2D* TextureCache::find( string_view relativePath ) const
    {
        if ( relativePath.empty() || _impl == nullptr )
            return nullptr;
        const string                        key = FileUtil::normalizePath( relativePath );
        std::shared_lock<std::shared_mutex> lock{ _impl->_mutex };
        const auto                          it = _impl->_mapEntry.find( key );
        // 처음 올리는 중인 항목은 내주지 않는다 — 올리는 스레드가 잠금 없이 그 객체를 채우고 있다.
        return it != _impl->_mapEntry.end() && it->second._bPublished ? it->second._texture.get() : nullptr;
    }

    uint32 TextureCache::getReloadGeneration() const
    {
        return _impl != nullptr ? _impl->_reloadGeneration.load( std::memory_order_acquire ) : 0u;
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
        // 올리는 중(acquire · reload)이면 지우지 않는다 — 그 스레드가 끝날 때 참조 수를 보고 정리한다.
        if ( it->second._refCount == 0 && it->second._bLoading == false )
        {
            if ( it->second._texture != nullptr )
                it->second._texture->releaseRHI( pDevice );
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
     *          `releaseRHI` 를 부를 방법이 없습니다. 살아 있는 디바이스에서 부르면 텍스처 핸들과
     *          **bindless SRV 인덱스**가 그대로 샙니다. 후자는 프리리스트로 영영 안 돌아옵니다.
     *
     *          지금 이 함수로 오는 길은 하나뿐이고 그 순서가 이 계약을 지킵니다:
     *          `EngineLoop::shutdown` 이 `_rhi->shutdown()` 을 **먼저** 부르고, 그것이
     *          `RHIRenderResource` 레지스트리 전체에 `releaseRHI` 를 밀어 둔 뒤에야
     *          `AssetManager::shutdown` → `clearAssetCaches()` 가 여기에 닿습니다.
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

namespace sw
{
    SW_BUILT_IN_ASSET_CACHE( TextureCache );
} // namespace sw
