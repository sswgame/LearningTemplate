/**
 * @file TextureCacheReload.cpp
 * @brief 텍스처 캐시의 핫 리로드입니다(개발 전용 — 배포 구성은 `*Reload.cpp` 를 짓지 않고, 등록은 `SW_ASSET_CACHE_RELOAD_FUNCTION` 이 nullptr 이다).
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Graphics/Texture/TextureCacheImpl.h"

namespace sw
{
    SW_LOG_CALLER( "TextureCache" );

    void TextureCache::reload( const AssetID& assetID, IRHIDevice* pDevice )
    {
        const string_view relativePath = assetID.view();
        SW_MEMORY_SCOPE( Texture );
        // 렌더 텍스처 · CPU 텍스처는 디스크에 없다 — 다시 읽을 것이 없다.
        const bool bNotOnDisk = relativePath.empty() || isRenderTargetPath( relativePath ) || isCPUTexturePath( relativePath );
        const bool bCanReload = _impl != nullptr && pDevice != nullptr;
        if ( bNotOnDisk || bCanReload == false )
            return;
        const string key = FileUtil::normalizePath( relativePath );

        // 기다림(waitIdle)과 다시 올리기는 잠금 밖에서 한다. 잠금을 쥔 채 waitIdle 을 부르면 같은 잠금을 기다리는
        // 렌더 스레드와 서로 기다릴 수 있다. 진행 중 표시를 세워 두면 그사이 acquire 는 기다리고 release 는 지우지 않는다.
        UniqueLock<shared_mutex> lock{ _impl->_mutex };
        Texture2D*               pTexture = nullptr;
        for ( ;; )
        {
            const auto it = _impl->_mapEntry.find( key );
            if ( it == _impl->_mapEntry.end() || it->second._texture == nullptr )
                return;
            // 스트리밍이 위쪽 밉을 바꿔 끼우는 중이면 그 끝을 기다린다(렌더 스레드가 같은 텍스처의 핸들을 바꾸고 있다).
            if ( it->second._bLoading || it->second._bUpgrading )
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
        pDevice->waitIdle( RHIWaitIdleReason::HotReload );
        pTexture->releaseRHI( pDevice );
        if ( pTexture->loadFromAsset( pDevice, AssetID{ key } ) == false )
            SW_LOG_ERROR( "Hot-Reload failed for Texture %#", key.c_str() );

        lock.lock();
        const auto            it = _impl->_mapEntry.find( key );
        unique_ptr<Texture2D> pErased;
        SW_ASSERT( it != _impl->_mapEntry.end() && it->second._bLoading );
        it->second._bLoading = false;
        // 다시 올리는 사이 마지막 참조가 놓였으면 release 대신 여기서 지우고, 장치 호출은 잠금 밖에서 한다(잠금은 잎 잠금, 결정 5-3).
        if ( it->second._refCount == 0 )
        {
            pErased = std::move( it->second._texture );
            _impl->_mapEntry.erase( it );
        }
        lock.unlock();
        if ( pErased != nullptr )
            pErased->releaseRHI( pDevice );
        // 다시 올린 텍스처는 새 SRV 인덱스를 받았다(실패했으면 인덱스가 없다). 옛 인덱스는 이미 돌려줬으므로 빌려 간 머티리얼이 다시 받게 한다.
        // 세대는 진행 중 표시를 내린 **뒤에** 올린다 — 먼저 올리면 새 세대를 본 쪽의 find 가 아직 nullptr 를 받고 그 세대를 소비해 버린다.
        _impl->_reloadGeneration.fetch_add( 1u, std::memory_order_acq_rel );
        _impl->_cvLoaded.notify_all();
    }
} // namespace sw
