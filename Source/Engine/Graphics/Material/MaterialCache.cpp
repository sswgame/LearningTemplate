#include "pch.h"

#include "Engine/Graphics/Material/MaterialCache.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/Cache/BuiltInAssetCache.h"

namespace sw
{
    SW_LOG_CALLER( "MaterialCache" );

    namespace
    {
        /** @brief 고정 토큰(`MaterialCache::pinAsset`)이 놓일 때 확보한 참조를 놓는 삭제자입니다. 머티리얼 객체는 캐시가 소유하므로 지우지 않는다. */
        struct MaterialCachePinRelease
        {
            MaterialCache* _pCache;
            string         _key;

            void operator()( const Material* /*pMaterial*/ ) const { _pCache->release( _key ); }
        };
    } // namespace

    struct MaterialCache::Impl
    {
        struct Entry
        {
            shared_ptr<Material> _material; ///< shared 인 이유는 Material.h 머리 주석에 있습니다(렌더 패킷이 소유를 빌립니다)
            string               _path;
            uint32               _refCount{ 0 };
        };

        unordered_map<string, Entry> _mapEntry;
        vector<string>               _listPendingKey; ///< `requestInitialize` 가 등록한, GPU 자원을 초기화할 경로(`_mutex` 아래)
        vector<string>               _listPSOWarmKey; ///< 고정(`pinAsset`)이 넣은, PSO 를 미리 만들 경로(`_mutex` 아래)
        atomic<uint32>               _pendingCount;   ///< `_listPendingKey` 의 크기. 0 이면 `initializePending` 이 잠그지 않는다(매 프레임 불린다)
        atomic<uint32>               _psoWarmCount;   ///< `_listPSOWarmKey` 의 크기. 0 이면 `takePSOWarmMaterials` 가 잠그지 않는다(매 프레임 불린다)
        std::shared_mutex            _mutex;

        Impl()
            : _mapEntry{}
            , _listPendingKey{}
            , _listPSOWarmKey{}
            , _pendingCount{ 0 }
            , _psoWarmCount{ 0 }
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
        SW_MEMORY_SCOPE( Material );
        if ( relativePath.empty() || _impl == nullptr )
            return nullptr;

        const string key = FileUtil::normalizePath( relativePath );
        engine::getSubsystem<AssetManager>().getAssetDatabase().ensureMeta( key );

        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        Impl::Entry&                        entry = _impl->_mapEntry[key];
        if ( entry._material == nullptr )
        {
            entry._material = Material::create();
            entry._path     = key;
            // 씬이 쓰는 머티리얼이다 — 텍스처를 워커가 읽는 동안 로드 중 텍스처를 그린다(렌더 스레드가 기다리지 않게).
            entry._material->setTextureLoadStart( AssetLoadStart::Async );
        }
        ++entry._refCount;

        // GPU 자원이 초기화됐는지는 머티리얼에게 묻는다. 캐시가 따로 기록하면 디바이스가 죽었을 때 그 기록이 틀려진다.
        if ( pDevice != nullptr && entry._material->isRHIValid() == false )
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

        // 참조가 0 이면 항목을 그 자리에서 지우므로 여기에 0 이 들어올 일은 없다. 그래도
        // 막아 둔다. 부호 없는 수라 한 번 되감기면 42억이 되고, 그 뒤로는 참조가 0 에 닿지 못해
        // 이 머티리얼이 캐시에 영원히 남는다. 되감김은 조용하고 증상은 멀리서 나타난다.
        // 같은 구조의 `TextureCache::release` 도 같은 검사를 한다.
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

    void MaterialCache::requestInitialize( string_view relativePath )
    {
        if ( relativePath.empty() || _impl == nullptr )
            return;

        const string                        key = FileUtil::normalizePath( relativePath );
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        const auto                          it = _impl->_mapEntry.find( key );
        if ( it == _impl->_mapEntry.end() || it->second._material->isRHIValid() )
            return;
        for ( const string& pendingKey : _impl->_listPendingKey )
        {
            if ( pendingKey == key )
                return;
        }
        _impl->_listPendingKey.push_back( key );
        _impl->_pendingCount.store( static_cast<uint32>( _impl->_listPendingKey.size() ), std::memory_order_release );
    }

    void MaterialCache::initializePending( IRHIDevice* pDevice )
    {
        SW_MEMORY_SCOPE( Material );
        const bool bNoPendingMaterials = pDevice == nullptr || _impl == nullptr || _impl->_pendingCount.load( std::memory_order_acquire ) == 0;
        if ( bNoPendingMaterials )
            return;

        vector<string> listKey;
        {
            std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
            listKey.swap( _impl->_listPendingKey );
            _impl->_pendingCount.store( 0, std::memory_order_release );
        }

        // 초기화하는 동안(파일 읽기와 버퍼 생성)에는 잠그지 않는다. 소유권을 하나 빌려 그 사이 참조가 0 이 되어도 머티리얼이 살아 있게 한다.
        for ( const string& key : listKey )
        {
            shared_ptr<Material> material;
            {
                std::shared_lock<std::shared_mutex> lock{ _impl->_mutex };
                const auto                          it = _impl->_mapEntry.find( key );
                if ( it == _impl->_mapEntry.end() )
                    continue; // 그 사이에 해제됐다
                material = it->second._material;
            }
            if ( material->isRHIValid() )
                continue;
            if ( material->initialize( pDevice, key ) == false )
                SW_LOG_ERROR( "Failed to initialize Material %#", key.c_str() );
        }
    }

    bool MaterialCache::hasPendingInitialize() const
    {
        return _impl != nullptr && _impl->_pendingCount.load( std::memory_order_acquire ) != 0;
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
        SW_MEMORY_SCOPE( Material );
        if ( relativePath.empty() || _impl == nullptr )
            return;

        const string key{ FileUtil::normalizePath( relativePath ) };

        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        auto                                it{ _impl->_mapEntry.find( key ) };
        if ( it != _impl->_mapEntry.end() )
        {
            if ( it->second._material->isRHIValid() && pDevice != nullptr )
            {
                // 아직 이전 프레임(들)이 GPU 에서 이 Material 의 bindless 상수버퍼 인덱스를 참조하고
                // 있을 수 있다. releaseRHI() 의 unregisterBindlessResource 는 인덱스를 즉시 프리리스트로
                // 반환해서, waitIdle 없이 바로 initialize() 가 같은 인덱스를 재할당하면 아직 그 인덱스를
                // 읽는 중인 드로우가 다른 머티리얼의 값을 읽는 조용한 데이터 오염이 될 수 있다.
                pDevice->waitIdle();
                it->second._material->releaseRHI( pDevice );
            }

            if ( pDevice != nullptr && it->second._material->initialize( pDevice, key ) == false )
                SW_LOG_ERROR( "Hot-Reload failed for Material %#", key.c_str() );
        }
    }

    shared_ptr<const void> MaterialCache::pinAsset( string_view relativePath )
    {
        if ( AssetTypeRegistry::isPathOfType( relativePath, Material::kAssetType ) == false )
            return {};
        // 디바이스 없이 확보하면 항목만 만든다(실패는 빈 경로뿐인데 접미사가 맞았으니 비지 않았다).
        const Material* pMaterial = acquire( relativePath, nullptr );
        SW_ASSERT( pMaterial != nullptr );
        requestInitialize( relativePath );
        const string key = FileUtil::normalizePath( relativePath );
        {
            std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
            if ( VectorUtil::contains( _impl->_listPSOWarmKey, key ) == false )
                _impl->_listPSOWarmKey.push_back( key );
            _impl->_psoWarmCount.store( static_cast<uint32>( _impl->_listPSOWarmKey.size() ), std::memory_order_release );
        }
        return shared_ptr<const void>( pMaterial, MaterialCachePinRelease{ this, key } );
    }

    void MaterialCache::takePSOWarmMaterials( vector<shared_ptr<Material>>& outListMaterial )
    {
        outListMaterial.clear();
        if ( _impl == nullptr || _impl->_psoWarmCount.load( std::memory_order_acquire ) == 0 )
            return;
        std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
        size_t                              keyIndex = 0;
        while ( keyIndex < _impl->_listPSOWarmKey.size() )
        {
            const auto it = _impl->_mapEntry.find( _impl->_listPSOWarmKey[keyIndex] );
            if ( it != _impl->_mapEntry.end() && it->second._material->isRHIValid() == false )
            {
                ++keyIndex; // 아직 초기화 전이다 — 셰이더를 모른다
                continue;
            }
            if ( it != _impl->_mapEntry.end() )
                outListMaterial.push_back( it->second._material );
            VectorUtil::removeAtSwap( _impl->_listPSOWarmKey, keyIndex );
        }
        _impl->_psoWarmCount.store( static_cast<uint32>( _impl->_listPSOWarmKey.size() ), std::memory_order_release );
    }

    /**
     * @brief 캐시를 통째로 비웁니다. **GPU 자원을 돌려주지는 않습니다.**
     * @warning **디바이스가 죽은 뒤에만 부를 수 있습니다.** 이유는 `TextureCache::clear()` 와 같습니다.
     *          `IAssetCache::clear()` 는 디바이스를 인자로 받지 않습니다. 캐시가 디바이스를 보관하면 백엔드를 교체할 때
     *          죽은 포인터가 되기 때문입니다(이 파일 헤더의 머리말 참고). 캐시도 디바이스를 보관하지 않으므로
     *          여기서 `releaseRHI` 를 부를 방법이 없습니다.
     *
     *          지금 이 함수가 불리는 경로는 `AssetManager::shutdown` → `clearAssetCaches()` 하나뿐입니다.
     *          `Engine::shutdown` 이 그보다 **먼저** `_rhi->shutdown()` 을 불러 레지스트리 전체에
     *          `releaseRHI` 를 전달해 둡니다. 평소에는 참조가 0 이 되는 `release()` 쪽이 정리합니다.
     */
    void MaterialCache::clear()
    {
        if ( _impl != nullptr )
        {
            std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
            _impl->_mapEntry.clear();
            _impl->_listPSOWarmKey.clear();
            _impl->_psoWarmCount.store( 0, std::memory_order_release );
        }
    }
} // namespace sw

namespace sw
{
    SW_BUILT_IN_ASSET_CACHE( MaterialCache );
    SW_ASSET_TYPE( Material, Material::kAssetType );
} // namespace sw
