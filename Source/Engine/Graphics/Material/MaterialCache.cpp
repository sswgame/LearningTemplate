#include "pch.h"

#include "Engine/Graphics/Material/MaterialCache.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/unordered_map.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Resource/ResourceManager.h"

namespace sw
{
    SW_LOG_CALLER( "MaterialCache" );

    struct MaterialCache::Impl
    {
        struct Entry
        {
            shared_ptr<Material> _material; ///< shared 인 이유는 Material.h 머리 주석 — 렌더 패킷이 소유를 빌린다
            string               _path;
            uint32               _refCount{ 0 };
        };

        unordered_map<string, Entry> _mapEntry;
        std::shared_mutex            _mutex;

        Impl()
            : _mapEntry{}
            , _mutex{}
        {
        }
    };

    MaterialCache::MaterialCache()
        : _impl{ make_unique<Impl>() }
    {
    }

    MaterialCache::~MaterialCache() = default;

    Material* MaterialCache::acquire( string_view relativePath, IRHIDevice* pDevice )
    {
        if ( relativePath.empty() || _impl == nullptr )
            return nullptr;

        const string key = FileUtil::normalizePath( relativePath );
        engine::getResourceManager().getAssetDatabase().ensureMeta( key );

        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        Impl::Entry&                        entry = _impl->_mapEntry[key];
        if ( entry._material == nullptr )
        {
            entry._material = Material::create();
            entry._path     = key;
        }
        ++entry._refCount;

        // "올라갔나" 는 머티리얼에게 묻는다. 캐시가 따로 세면 디바이스가 죽었을 때 그 표식이 거짓말이 된다 —
        // 실제로 그 거짓말을 지우려고 바깥에서 일괄 해제를 불러 주어야 했다.
        if ( pDevice != nullptr && entry._material->isRhiValid() == false )
        {
            if ( entry._material->initialize( pDevice, key ) == false )
            {
                SW_LOG_ERROR( "Failed to initialize Material %#", key.c_str() );
                --entry._refCount;
                if ( entry._refCount == 0 )
                    _impl->_mapEntry.erase( key );
                return nullptr;
            }
        }

        return entry._material.get();
    }

    void MaterialCache::release( string_view relativePath )
    {
        if ( relativePath.empty() || _impl == nullptr )
            return;

        const string key = FileUtil::normalizePath( relativePath );

        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        auto                                it = _impl->_mapEntry.find( key );
        if ( it == _impl->_mapEntry.end() )
            return;

        // 참조가 0 이면 항목을 그 자리에서 지우므로 여기 0 이 들어올 길은 **지금은 없다.** 그래도
        // 막아 둔다 — 부호 없는 수라 한 번 되감기면 42억이 되고, 그 뒤로는 참조가 0 에 닿지 못해
        // 이 머티리얼이 캐시에 영원히 못박힌다. 되감김은 조용하고 증상은 멀리서 나타난다.
        // 같은 모양의 `TextureCache::release` 는 처음부터 이 검사를 하고 있었다.
        if ( it->second._refCount == 0 )
        {
            SW_LOG_WARNING( "Material '%#' 를 acquire 보다 많이 release 했습니다 — 무시합니다.", key.c_str() );
            return;
        }

        --it->second._refCount;
        if ( it->second._refCount == 0 )
            _impl->_mapEntry.erase( it );
    }

    size_t MaterialCache::getCachedCount() const
    {
        if ( _impl == nullptr )
            return 0;
        std::shared_lock<std::shared_mutex> lock{ _impl->_mutex };
        return _impl->_mapEntry.size();
    }

    bool MaterialCache::isCached( string_view relativePath ) const
    {
        if ( relativePath.empty() || _impl == nullptr )
            return false;

        const string                        key = FileUtil::normalizePath( relativePath );
        std::shared_lock<std::shared_mutex> lock{ _impl->_mutex };
        return _impl->_mapEntry.find( key ) != _impl->_mapEntry.end();
    }

    void MaterialCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        if ( relativePath.empty() || _impl == nullptr )
            return;

        const string key{ FileUtil::normalizePath( relativePath ) };

        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        auto                                it{ _impl->_mapEntry.find( key ) };
        if ( it != _impl->_mapEntry.end() )
        {
            if ( it->second._material->isRhiValid() && pDevice != nullptr )
            {
                // 아직 이전 프레임(들)이 GPU에서 이 Material의 bindless 상수버퍼 인덱스를 참조하고
                // 있을 수 있다 — shutdown()의 unregisterBindlessResource는 인덱스를 즉시 프리리스트로
                // 반환해서, waitIdle 없이 바로 initialize()가 같은 인덱스를 재할당하면 아직 그 인덱스를
                // 읽는 중인 드로우가 다른 머티리얼의 값을 읽는 조용한 데이터 오염이 될 수 있다.
                pDevice->waitIdle();
                it->second._material->releaseRhi( pDevice );
            }

            if ( pDevice != nullptr && it->second._material->initialize( pDevice, key ) == false )
                SW_LOG_ERROR( "Hot-Reload failed for Material %#", key.c_str() );
        }
    }

    /**
     * @brief 표를 통째로 비웁니다 — **GPU 자원을 돌려주지는 않습니다.**
     * @warning **디바이스가 죽은 뒤에만 부를 수 있습니다.** 이유는 `TextureCache::clear()` 와 같다:
     *          `IAssetCache::clear()` 는 디바이스를 인자로 받지 않고(이 파일 헤더의 머리말 참고 —
     *          캐시가 디바이스를 들고 있으면 백엔드 교체 때 죽은 포인터가 된다) 캐시도 들고 있지
     *          않으므로 여기서 `releaseRhi` 를 부를 방법이 없다.
     *
     *          지금 이 함수로 오는 길은 `ResourceManager::shutdown` → `clearAssetCaches()` 하나뿐이고,
     *          `EngineLoop::shutdown` 이 그보다 **먼저** `_rhi->shutdown()` 을 불러 등록부 전체에
     *          `releaseRhi` 를 밀어 둔다. 평소 경로는 참조가 0 이 되는 `release()` 쪽이다.
     */
    void MaterialCache::clear()
    {
        if ( _impl != nullptr )
        {
            std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
            _impl->_mapEntry.clear();
        }
    }
} // namespace sw
