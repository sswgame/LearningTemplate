/**
 * @file MaterialCacheReload.cpp
 * @brief 머티리얼 캐시의 핫 리로드입니다(개발 전용 — 배포 구성은 `*Reload.cpp` 를 짓지 않고, 등록은 `SW_ASSET_CACHE_RELOAD_FUNCTION` 이 nullptr 이다).
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Material/MaterialCacheImpl.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    SW_LOG_CALLER( "MaterialCache" );

    void MaterialCache::reload( const AssetID& assetID, IRHIDevice* pDevice )
    {
        const string_view relativePath = assetID.view();
        SW_MEMORY_SCOPE( Material );
        if ( relativePath.empty() || _impl == nullptr )
            return;

        const string key{ FileUtil::normalizePath( relativePath ) };

        // 대기와 초기화는 잠금 밖에서 한다. `waitIdle` 은 렌더 스레드가 받은 일을 끝내길 기다리는데 렌더 스레드는 프레임 안에서
        // `takePSOWarmMaterials` 로 이 잠금을 잡는다. 잠금을 쥔 채 기다리면 서로 영영 기다린다. 소유를 하나 빌려 그 사이 항목이 지워져도 머티리얼이 살아 있게 한다.
        shared_ptr<Material> material;
        {
            ScopedSharedLock<shared_mutex> lock{ _impl->_mutex };
            const auto                     it = _impl->_mapEntry.find( key );
            if ( it == _impl->_mapEntry.end() )
                return;
            material = it->second._material;
        }
        if ( pDevice == nullptr )
            return;

        const Impl::DeviceInitScope initScope{ *_impl };
        if ( material->isRHIValid() )
        {
            // 아직 이전 프레임(들)이 GPU 에서 이 Material 의 bindless 상수버퍼 인덱스를 참조하고
            // 있을 수 있다. releaseRHI() 의 unregisterBindlessResource 는 인덱스를 즉시 프리리스트로
            // 반환해서, waitIdle 없이 바로 initialize() 가 같은 인덱스를 재할당하면 아직 그 인덱스를
            // 읽는 중인 드로우가 다른 머티리얼의 값을 읽는 조용한 데이터 오염이 될 수 있다.
            pDevice->waitIdle( RHIWaitIdleReason::HotReload );
            material->releaseRHI( pDevice );
        }

        if ( material->initialize( pDevice, AssetID{ key } ) == false )
            SW_LOG_ERROR( "Hot-Reload failed for Material %#", key.c_str() );
    }
} // namespace sw
