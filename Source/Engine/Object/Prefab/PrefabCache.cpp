#include "pch.h"

#include "Engine/Object/Prefab/PrefabCache.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_set.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/BuiltInAssetCache.h"
#include "Engine/Asset/Cook/CookStore.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/CookCodeFingerprint.h"
#include "Engine/Content/CookJob.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Object/Prefab/PrefabOverrides.h"
#include "Engine/Object/Prefab/PrefabPathStack.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

/**
 * @file PrefabCache.cpp
 * @brief 프리팹 로드 캐시, 스폰 · 중첩 스폰, 펼치기, 쿠킹, 핫 리로드를 구현합니다.
 * @details 형식 셋 읽기 · 쓰기는 `PrefabAsset.cpp` 다.
 */

namespace sw
{
    namespace
    {
        struct PrefabCacheInternal
        {
            /** @brief 프리팹 캐시의 계측 칸입니다(`CacheStats`). 캐시는 에셋 매니저마다 하나지만 칸은 프로세스에 하나라 매니저를 다시 세워도 이어 센다. */
            static const CacheStatsHandle& getCacheStats()
            {
                static const CacheStatsHandle s_stats = CacheStats::getOrCreate( PrefabCache::kAssetKindName );
                return s_stats;
            }

            /** @brief 로드 검증에서 순환이 없다는 값입니다(`PrefabThreadChain::_cycleFloor`). */
            static constexpr size_t kNoCycle = static_cast<size_t>( constant::invalid_index::kUint64 );

            static constexpr const utf8* kDefaultInstance = "PrefabInstance";

            /**
             * @brief 부품 @p pChild 를 부모 오브젝트 @p pParent 의 `attachKey` 씬 컴포넌트(비면 primary)에 붙입니다. 씬 컴포넌트가 없는 부품은 붙이지 않습니다.
             * @details 상대 위치를 그대로 둔다(`KeepRelative`) — 노드의 오버라이드가 적은 위치가 부모 기준 위치다.
             */
            static void attachNestedInstance( GameObject* pChild, GameObject* pParent, const string& attachKey )
            {
                SceneComponent* pChildRoot = pChild->getPrimarySceneComponent();
                if ( pChildRoot == nullptr )
                    return;
                SceneComponent* pTarget = pParent->getPrimarySceneComponent();
                if ( attachKey.empty() == false )
                {
                    Component* pKeyed = ComponentStableKey::findComponent( pParent, attachKey );
                    if ( pKeyed != nullptr && pKeyed->isSceneComponent() )
                        pTarget = static_cast<SceneComponent*>( pKeyed );
                    else
                        SW_LOG_WARNING_ID( "PrefabAsset.NestedPrefabInstanceAttaches", "Nested prefab instance '%#' attaches to '%#', which '%#' does not have - it is attached to the primary scene component",
                                           pChild->getName().c_str(), attachKey, pParent->getName().c_str() );
                }
                if ( pTarget == nullptr )
                {
                    SW_LOG_WARNING_ID( "PrefabAsset.NestedPrefabInstanceNo", "Nested prefab instance '%#' has no scene component of '%#' to attach to - it is left at the root", pChild->getName().c_str(),
                                       pParent->getName().c_str() );
                    return;
                }
                if ( pChildRoot->attachToComponent( pTarget ) == false )
                    SW_LOG_WARNING_ID( "PrefabAsset.NestedPrefabInstanceCould", "Nested prefab instance '%#' could not be attached to '%#'", pChild->getName().c_str(), pParent->getName().c_str() );
            }

            /** @brief @p listPartOverride 가운데 경로가 @p partPath 인 것을 문서 순서대로 @p inoutStateXML 에 얹습니다. 얹지 못하면 false 입니다. */
            [[nodiscard]] static bool applyPartOverrides( const vector<PrefabPartOverride>& listPartOverride, const string& partPath, string& inoutStateXML )
            {
                for ( const PrefabPartOverride& part : listPartOverride )
                {
                    if ( part._partPath != partPath )
                        continue;
                    string merged;
                    if ( PrefabOverrides::makeInstanceState( inoutStateXML, part._overrideXML, {}, merged ) == false )
                        return false;
                    inoutStateXML = std::move( merged );
                }
                return true;
            }

            /**
             * @brief 노드가 자기 부품 아래에 덮어쓴 것 @p inoutListInner(부품 @p partPath 에서 측정한 경로)를 바깥 루트에서 측정한 경로로 바꾸고, 그 뒤에 바깥 것 @p listEnclosingPart 를 잇습니다.
             * @details 안쪽이 앞이라 같은 부품이면 바깥 것이 나중에 얹혀 이긴다(유니티: 씬 인스턴스의 오버라이드가 바깥 프리팹의 오버라이드를 이긴다).
             */
            static void prependInnerParts( string_view partPath, vector<PrefabPartOverride>& inoutListInner, const vector<PrefabPartOverride>& listEnclosingPart )
            {
                for ( PrefabPartOverride& inner : inoutListInner )
                {
                    inner._partPath = PrefabPartOverride::joinPath( partPath, inner._partPath );
                }
                inoutListInner.insert( inoutListInner.end(), listEnclosingPart.begin(), listEnclosingPart.end() );
            }

            /** @brief @p rootObjectID 인스턴스가 스폰한 부품과 그 부품들이 스폰한 부품을 모두 모읍니다(부모 부품이 앞). */
            static void collectPartTree( const PrefabInstanceRegistry& registry, uint64 rootObjectID, vector<uint64>& outListObjectID )
            {
                outListObjectID.clear();
                registry.collectNestedPartsOf( rootObjectID, outListObjectID );
                vector<uint64> listChildPart;
                for ( size_t index = 0; index < outListObjectID.size(); ++index )
                {
                    registry.collectNestedPartsOf( outListObjectID[index], listChildPart );
                    outListObjectID.insert( outListObjectID.end(), listChildPart.begin(), listChildPart.end() );
                }
            }
        };

        /**
         * @brief 한 스레드가 프리팹을 펼치는 호출 사슬의 상태입니다 — 소유자는 그 스레드입니다.
         * @details 재진입하는 호출 사슬(컴포넌트가 로드 뒤 같은 프리팹을 스폰한다 · 원본을 로드하다 중첩 원본을 로드한다)의 순환을 막는 기록이라, 공유 상태가 아니라
         *          사슬을 도는 스레드의 상태다. 씬을 읽는 워커와 게임 스레드가 동시에 펼치므로 스레드마다 하나이고, 한 이름공간 변수(함수 안 정적 지역 셋이 아니다)에 모았다.
         */
        struct PrefabThreadChain
        {
            /** @brief 이 스레드가 스폰 중인 프리팹 경로입니다. 스폰 · 중첩 스폰 · 펼치기가 같이 씁니다.
             *         컴포넌트가 로드 뒤 같은 프리팹을 스폰하는 순환(`PrefabTest.CircularReferenceSpawnProtection`)과, 로드한 뒤 원본을 고쳐 생긴 중첩 순환을 막는다. */
            PrefabPathStack::List _spawnStack;
            /** @brief 이 스레드가 중첩 인스턴스를 검증하며 로드 중인 프리팹 경로입니다(`PrefabCache::validateNestedInstances`). 스폰 스택과 따로 둔다 — 런타임 스폰이 로드를 실패시키지 않게. */
            PrefabPathStack::List _loadStack;
            /** @brief 로드 스택에서 찾은 순환의 시작 깊이입니다. 순환에 든 프리팹(그 깊이 이상)은 로드에 실패하고, 시작 깊이의 프리팹이 끝날 때 지운다. */
            size_t _cycleFloor{ PrefabCacheInternal::kNoCycle };
        };

        thread_local PrefabThreadChain t_prefabChain;
    } // namespace
} // namespace sw

namespace sw
{
    SW_BUILT_IN_ASSET_CACHE( PrefabCache );

    string PrefabCache::makeCacheKey( string_view assetRelativePath )
    {
        string key = FileUtil::normalizePath( assetRelativePath );

        // 확장자 길이를 손으로 쓰지 않는다 — 숫자와 문자열이 떨어져 있으면 어긋난다.
        for ( const string_view extension : { ".bin", ".xml", ".json" } )
        {
            if ( FileUtil::hasExtension( key, extension ) )
            {
                key.resize( key.size() - extension.size() );
                break;
            }
        }
        return key;
    }

    bool PrefabCache::isCached( string_view assetRelativePath ) const
    {
        if ( assetRelativePath.empty() )
            return false;

        const string                   cacheKey = PrefabCache::makeCacheKey( assetRelativePath );
        ScopedSharedLock<shared_mutex> readLock{ _mapCacheMutex };
        return _mapCache.find( cacheKey ) != _mapCache.end();
    }

    size_t PrefabCache::getCachedCount() const
    {
        ScopedSharedLock<shared_mutex> readLock{ _mapCacheMutex };
        return _mapCache.size();
    }

    void PrefabCache::clear()
    {
        UniqueLock<shared_mutex> writeLock{ _mapCacheMutex };
        _cacheGeneration.fetch_add( 1, std::memory_order_acq_rel );
        _mapLoading.clear();
        _mapCache.clear();
        PrefabCacheInternal::getCacheStats().recordClear();
    }

    PrefabCache::PrefabCache()
        : AssetCacheBase{ CacheTracking::counted( kAssetKindName ) }
        , _mapCacheMutex{}
        , _mapCache{}
        , _mapLoading{}
        , _cacheGeneration{ 0 }
    {
    }

    PrefabCache::~PrefabCache()
    {
        // 워커가 아직 이 캐시로 읽고 있을 수 있다(로드 작업이 원시 포인터를 쥔다). 모두 끝날 때까지 기다린다 — 아무도 집지 않은 로드는 여기서 실행한다.
        vector<shared_ptr<PrefabLoadOperation>> listLoading;
        {
            UniqueLock<shared_mutex> writeLock{ _mapCacheMutex };
            for ( const auto& [key, weakOperation] : _mapLoading )
            {
                shared_ptr<PrefabLoadOperation> pOperation = weakOperation.lock();
                if ( pOperation != nullptr )
                    listLoading.push_back( std::move( pOperation ) );
            }
        }
        for ( const shared_ptr<PrefabLoadOperation>& pOperation : listLoading )
        {
            pOperation->wait();
        }
    }

    PrefabLoadOperation::PrefabLoadOperation( PrefabCache* pCache, string_view resolvedPath, string cacheKey )
        : AssetLoadOperation{ PrefabCache::kAssetKindName, resolvedPath, MemoryTag::Scene }
        , _pCache{ pCache }
        , _cacheKey{ std::move( cacheKey ) }
        , _pAsset{}
    {
    }

    expected<void> PrefabLoadOperation::runLoad()
    {
        _pAsset = _pCache->loadAndCache( getPath() );
        {
            // 결과가 캐시에 든 **뒤에** 진행 중 표에서 뺀다 — 그 사이에 온 요청은 진행 중 로드 작업이나 캐시 둘 중 하나를 반드시 본다.
            // 이 로드 작업의 칸일 때만 뺀다(다시 읽기가 표를 비운 뒤 새 요청이 새 로드 작업을 넣었을 수 있다).
            UniqueLock<shared_mutex> writeLock{ _pCache->_mapCacheMutex };
            const auto               it = _pCache->_mapLoading.find( _cacheKey );
            if ( it != _pCache->_mapLoading.end() && ( it->second.expired() || it->second.lock().get() == this ) )
                _pCache->_mapLoading.erase( it );
        }
        if ( _pAsset == nullptr )
        {
            return unexpected( Error{ ErrorKind::InvalidArgument, "the prefab could not be loaded (the loader logged why)", getPath() } );
        }
        return {};
    }

    PrefabHandle PrefabCache::request( string_view assetRelativePath )
    {
        return requestWith( assetRelativePath, AssetLoadStart::Async );
    }

    shared_ptr<const void> PrefabCache::pinAsset( string_view relativePath )
    {
        if ( AssetTypeRegistry::isPathOfType( relativePath, PrefabAsset::kAssetType ) == false )
            return {};
        return request( relativePath ).getOperation();
    }

    PrefabHandle PrefabCache::requestWith( string_view assetRelativePath, AssetLoadStart start )
    {
        const string resolvedPath = string( assetRelativePath );
        const string cacheKey     = PrefabCache::makeCacheKey( resolvedPath );

        shared_ptr<PrefabLoadOperation> pOperation;
        {
            UniqueLock<shared_mutex> writeLock{ _mapCacheMutex };
            const auto               loadingIt = _mapLoading.find( cacheKey );
            if ( loadingIt != _mapLoading.end() )
                pOperation = loadingIt->second.lock();
            if ( pOperation != nullptr )
            {
                PrefabCacheInternal::getCacheStats().recordHit();
                return PrefabHandle{ std::move( pOperation ) };
            }

            // 진행 중 표를 잡은 채 캐시를 본다 — 로드는 캐시에 넣은 뒤 표에서 빼므로, 여기서 둘 다 없으면 정말 아무도 읽지 않았다.
            pOperation         = sw::make_shared<PrefabLoadOperation>( this, resolvedPath, cacheKey );
            const auto cacheIt = _mapCache.find( cacheKey );
            if ( cacheIt != _mapCache.end() )
            {
                pOperation->_pAsset = cacheIt->second;
                pOperation->markReadyWithoutLoad();
                PrefabCacheInternal::getCacheStats().recordHit();
                return PrefabHandle{ std::move( pOperation ) };
            }
            _mapLoading[cacheKey] = pOperation;
            PrefabCacheInternal::getCacheStats().recordMiss();
        }
        AssetLoadOperation::startLoad( pOperation, start );
        return PrefabHandle{ std::move( pOperation ) };
    }

    shared_ptr<const PrefabAsset> PrefabCache::loadPrefab( string_view assetRelativePath )
    {
        // 기다림이 계약이다 — 씬 · 중첩 프리팹 · 에디터 명령은 받은 자산으로 그 자리에서 객체를 짓는다. 기다리지 않을 곳은 `request` 핸들을 든다.
        return requestWith( assetRelativePath, AssetLoadStart::OnCallingThread ).wait();
    }

    shared_ptr<const PrefabAsset> PrefabCache::loadAndCache( string_view assetRelativePath )
    {
        SW_MEMORY_SCOPE( Scene );
        const string resolvedPath = string( assetRelativePath );

        const string cacheKey   = PrefabCache::makeCacheKey( resolvedPath );
        const uint32 generation = _cacheGeneration.load( std::memory_order_acquire );
        {
            ScopedSharedLock<shared_mutex> readLock{ _mapCacheMutex };
            const auto                     cacheIt = _mapCache.find( cacheKey );
            if ( cacheIt != _mapCache.end() )
                return cacheIt->second;
        }

        shared_ptr<PrefabAsset> asset = sw::make_shared<PrefabAsset>();

        // 쿠킹본 이름은 쿠커(`cookAllPrefabs`)와 같은 규칙 하나다(`AssetCookPath`).
        string binPath = AssetCookPath::toCookedPath( resolvedPath );
        if ( binPath.empty() )
            binPath = resolvedPath;

        if ( AssetIO::usesSource() == false )
        {
            if ( asset->loadFromBinaryFile( AssetID{ binPath } ) == false )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.CookedContentRequiresCooked", "Cooked content requires the cooked prefab: %#", binPath );
                return nullptr;
            }
        }
        else
        {
            // 소스를 읽는 구성은 소스(XML/JSON)만 본다. 소스가 없을 때 옆에 남은 쿠킹본(.bin)을 대신 읽으면 옮기거나 지운 프리팹이
            // 낡은 내용으로 살아나 실패가 가려진다(언리얼 · 유니티의 에디터도 쿠킹 데이터를 보지 않는다).
            const bool bJSON         = FileUtil::hasExtension( resolvedPath, ".json" );
            const bool bSourceLoaded = bJSON ? asset->loadFromJSONFile( AssetID{ resolvedPath } ) : asset->loadFromXMLFile( AssetID{ resolvedPath } );
            if ( bSourceLoaded == false )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.PrefabSourceCouldNot", "Prefab source could not be loaded: %#", resolvedPath );
                return nullptr;
            }
        }

        // 중첩 인스턴스가 순환하면 로드 오류다(캐시에 넣지 않는다). 원본은 여기서 재귀로 로드되어 캐시에 든다.
        if ( validateNestedInstances( *asset, cacheKey ) == false )
            return nullptr;

        UniqueLock<shared_mutex> writeLock{ _mapCacheMutex };
        const auto               cacheIt = _mapCache.find( cacheKey );
        if ( cacheIt != _mapCache.end() )
            return cacheIt->second;
        // 읽는 사이 캐시를 버렸다(다시 읽기 · 비우기) — 옛 파일을 읽었을 수 있으니 넣지 않고 요청한 쪽에만 준다.
        if ( _cacheGeneration.load( std::memory_order_acquire ) != generation )
            return asset;

        _mapCache.emplace( cacheKey, asset );
        return asset;
    }

    bool PrefabCache::validateNestedInstances( const PrefabAsset& asset, const string& cacheKey )
    {
        bool bHasSourceNode = false;
        for ( const PrefabInstanceNode& node : asset.getNestedInstances() )
        {
            bHasSourceNode = bHasSourceNode || node.isResolved() == false;
        }
        if ( bHasSourceNode == false )
            return true;

        PrefabPathStack::List& stack      = t_prefabChain._loadStack;
        size_t&                cycleFloor = t_prefabChain._cycleFloor;
        const size_t           depth      = stack.size();
        {
            const PrefabPathStack::Scope scope{ stack, PrefabPathStack::makeKey( cacheKey ) };
            for ( const PrefabInstanceNode& node : asset.getNestedInstances() )
            {
                if ( node.isResolved() )
                    continue;
                const string           childPath  = string( node._prefabPath );
                const optional<size_t> stackIndex = VectorUtil::findIndex( stack, PrefabPathStack::makeKey( PrefabCache::makeCacheKey( childPath ) ) );
                if ( stackIndex.has_value() )
                {
                    cycleFloor = MathUtil::min( cycleFloor, *stackIndex );
                    SW_LOG_ERROR_ID( "PrefabAsset.CircularNestedPrefabReference", "Circular nested prefab reference: '%#' places '%#', which is already being loaded", asset.getName(), childPath );
                    continue;
                }
                // 원본은 이 스레드에서 읽는다(`loadAndCache`) — 다른 스레드의 로드 작업을 기다리면 순환한 두 프리팹을 두 워커가 읽을 때 서로를 기다린다.
                const bool bLoadFailed = loadAndCache( childPath ) == nullptr;
                if ( bLoadFailed && cycleFloor > depth )
                    SW_LOG_WARNING_ID( "PrefabAsset.NestedPrefabCouldNot2", "Nested prefab '%#' in '%#' could not be loaded - that instance is skipped when spawning", childPath, asset.getName() );
            }
        }

        const bool bInCycle = cycleFloor <= depth;
        if ( cycleFloor == depth )
            cycleFloor = PrefabCacheInternal::kNoCycle; // 순환이 여기서 시작했다 — 바깥 프리팹은 그 노드만 건너뛰고 로드된다
        if ( bInCycle )
            SW_LOG_ERROR_ID( "PrefabAsset.PrefabPartCircularNested", "Prefab '%#' is part of a circular nested prefab reference - it is not loaded", asset.getName() );
        return bInCycle == false;
    }

    GameObject* PrefabCache::spawn( GameObjectManager* pGameObjectManager, string_view assetRelativePath, const utf8* pInstanceName )
    {
        SW_MEMORY_SCOPE( Scene );
        // 이 함수는 나머지 포인터를 모두 검사한다(`pAsset` · `pGameObject` · `pInstanceName`).
        // 활성 씬이 없을 때 `getObjectManager()` 는 nullptr 를 반환한다.
        if ( pGameObjectManager == nullptr )
        {
            SW_LOG_WARNING_ID( "PrefabAsset.CannotSpawnPrefabWithout", "Cannot spawn prefab '%#' without a GameObjectManager.", assetRelativePath );
            return nullptr;
        }

        const string        resolvedPath = string( assetRelativePath );
        const hashed_string pathKey      = PrefabPathStack::makeKey( PrefabCache::makeCacheKey( resolvedPath ) );

        PrefabPathStack::List& spawnStack = t_prefabChain._spawnStack;
        if ( VectorUtil::contains( spawnStack, pathKey ) )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.CircularPrefabReferenceDetected", "Circular prefab reference detected for '%#' — spawn aborted to prevent recursion overflow",
                             assetRelativePath );
            return nullptr;
        }
        const PrefabPathStack::Scope scope{ spawnStack, pathKey };

        const shared_ptr<const PrefabAsset> pAsset = loadPrefab( assetRelativePath );
        if ( pAsset == nullptr || pAsset->isValid() == false )
            return nullptr;

        const utf8* pInstanceNameUTF8 = pInstanceName != nullptr ? pInstanceName : pAsset->getName().c_str();
        if ( StringUtil::isNullOrEmpty( pInstanceNameUTF8 ) )
            pInstanceNameUTF8 = PrefabCacheInternal::kDefaultInstance;

        GameObject* pGameObject = pGameObjectManager->createGameObject( hashed_string( pInstanceNameUTF8 ) );
        if ( pGameObject == nullptr )
            return nullptr;

        // 컴포넌트 틱 중이면 오브젝트는 지금 만들어 돌려주고(부르는 쪽이 핸들을 든다), 상태는 틱 직후 구조 변경 큐에서 채운다. 그 자리에서
        // 상태를 읽으면 컴포넌트 추가가 모두 미뤄져(nullptr) **프리팹의 값이 버려지고** 빈 오브젝트만 남는다.
        if ( pGameObjectManager->isStructuralMutationFrozen() )
        {
            const uint64 objectID = pGameObject->getObjectID();
            const string instanceName( pInstanceNameUTF8 );
            // 캐시의 프리팹은 그 사이 다시 읽힐 수 있다(에디터 핫 리로드) — 포인터가 아니라 경로를 들고 그때 다시 찾는다.
            pGameObjectManager->deferStructuralChange( [this, pGameObjectManager, objectID, resolvedPath, instanceName, pathKey]()
            {
                // 미룬 자리에서도 이 프리팹을 펼치는 중이라고 적는다 — 중첩 스폰의 순환 검사가 이 경로를 본다.
                const PrefabPathStack::Scope        laterScope{ t_prefabChain._spawnStack, pathKey };
                GameObject*                         pSpawned      = pGameObjectManager->findGameObjectByID( objectID );
                const shared_ptr<const PrefabAsset> pLaterAsset   = ( pSpawned != nullptr ) ? loadPrefab( resolvedPath ) : nullptr;
                const bool                          bStateWritten = pLaterAsset != nullptr && applySpawnState( pSpawned, *pLaterAsset, instanceName );
                if ( pSpawned != nullptr && bStateWritten == false )
                    pGameObjectManager->destroyGameObject( pSpawned );
            } );
            return pGameObject;
        }

        if ( applySpawnState( pGameObject, *pAsset, pInstanceNameUTF8 ) == false )
        {
            pGameObjectManager->destroyGameObject( pGameObject );
            return nullptr;
        }
        return pGameObject;
    }

    bool PrefabCache::applySpawnState( GameObject* pGameObject, const PrefabAsset& asset, string_view instanceName )
    {
        if ( StringUtil::trim( asset.getStateData() ).empty() == false )
        {
            if ( asset.applyStateTo( pGameObject ) == false )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.ObjectStateApplyFailedSpawn", "ObjectState apply failed for '%#' — spawn aborted", instanceName );
                return false;
            }
            pGameObject->setName( hashed_string( string( instanceName ).c_str() ) );
        }
        (void)spawnNestedInstances( pGameObject, asset, {} ); // 건너뛴 노드는 노드마다 경고했다 — 바깥 인스턴스는 남는다
        return true;
    }

    bool PrefabCache::makeNestedInstanceState( const PrefabInstanceNode& node, string& outStateXML, shared_ptr<const PrefabAsset>& outSource )
    {
        outSource.reset();
        outStateXML.clear();
        if ( node.isResolved() )
        {
            outStateXML = node._resolvedStateXML;
            return true;
        }

        const shared_ptr<const PrefabAsset> pSource = loadPrefab( node._prefabPath );
        if ( pSource == nullptr || pSource->isValid() == false )
        {
            SW_LOG_WARNING_ID( "PrefabAsset.NestedPrefabCouldNot3", "Nested prefab '%#' could not be loaded - instance '%#' is skipped", node._prefabPath, node._name );
            return false;
        }
        // XML 본문은 그대로 원형으로 쓴다(스폰마다 임시 매니저를 만들지 않는다). 오버라이드의 덮어쓴 칸은 이름으로 얹히므로 원형에 없는 칸은 더해진다.
        string      convertedBase;
        string_view baseState = pSource->getStateData();
        if ( pSource->getStateFormat() != PrefabStateFormat::XML )
        {
            if ( PrefabOverrides::makeBaseState( *pSource, convertedBase ) == false )
            {
                SW_LOG_WARNING_ID( "PrefabAsset.NestedPrefabCouldNot4", "Nested prefab '%#' could not be built as a base state - instance '%#' is skipped", node._prefabPath, node._name );
                return false;
            }
            baseState = convertedBase;
        }
        if ( PrefabOverrides::makeInstanceState( baseState, node._overrideXML, node._name, outStateXML ) == false )
        {
            SW_LOG_WARNING_ID( "PrefabAsset.OverridesNestedPrefabInstance2", "Overrides of nested prefab instance '%#' (%#) could not be applied - it is skipped", node._name, node._prefabPath );
            return false;
        }
        outSource = pSource;
        return true;
    }

    uint32 PrefabCache::spawnNestedInstances( GameObject* pRoot, const PrefabAsset& asset, const vector<PrefabPartOverride>& listPartOverride )
    {
        // 변형이 물려받은 부품에 덮어쓴 것이 앞이다 — 인스턴스가 덮어쓴 것이 나중에 얹혀 이긴다.
        vector<PrefabPartOverride> listAllPart = asset.getVariantPartOverrides();
        listAllPart.insert( listAllPart.end(), listPartOverride.begin(), listPartOverride.end() );
        vector<string> listSpawnedPath;
        const uint32   createdCount = spawnNestedInstancesUnder( pRoot, asset, {}, listAllPart, listSpawnedPath );
        for ( const PrefabPartOverride& part : listAllPart )
        {
            if ( VectorUtil::contains( listSpawnedPath, part._partPath ) == false )
                SW_LOG_WARNING_ID( "Prefab.NestedPartOverrideDropped", "Override of nested prefab part '%#' in '%#' is dropped - '%#' no longer spawns that part", part._partPath,
                                   pRoot != nullptr ? pRoot->getName().c_str() : "?", asset.getName() );
        }
        return createdCount;
    }

    uint32 PrefabCache::spawnNestedInstancesUnder( GameObject* pRoot, const PrefabAsset& asset, string_view rootPath, const vector<PrefabPartOverride>& listPartOverride,
                                                   vector<string>& inoutListSpawnedPath )
    {
        const vector<PrefabInstanceNode>& listNode = asset.getNestedInstances();
        GameObjectManager*                pManager = pRoot != nullptr ? pRoot->getManager() : nullptr;
        if ( listNode.empty() || pManager == nullptr )
            return 0;
        SW_MEMORY_SCOPE( Scene );

        PrefabInstanceRegistry& registry   = pManager->getSubsystem<PrefabInstanceRegistry>();
        PrefabPathStack::List&  spawnStack = t_prefabChain._spawnStack;
        vector<GameObject*>     listCreated( listNode.size(), nullptr );
        uint32                  createdCount = 0;
        for ( size_t index = 0; index < listNode.size(); ++index )
        {
            const PrefabInstanceNode& node    = listNode[index];
            GameObject*               pParent = node._parentIndex < 0 ? pRoot : listCreated[static_cast<size_t>( node._parentIndex )];
            if ( pParent == nullptr )
                continue; // 부모 노드를 건너뛰었다 — 붙을 곳이 없다

            hashed_string sourceKey{};
            if ( node.isResolved() == false )
            {
                sourceKey = PrefabPathStack::makeKey(
                    PrefabCache::makeCacheKey( string( node._prefabPath ) ) );
                if ( VectorUtil::contains( spawnStack, sourceKey ) )
                {
                    SW_LOG_ERROR_ID( "PrefabAsset.CircularNestedPrefabInstance", "Circular nested prefab '%#' in '%#' - instance '%#' is not spawned", node._prefabPath, asset.getName(), node._name );
                    continue;
                }
            }

            string                        state;
            shared_ptr<const PrefabAsset> pSource;
            if ( makeNestedInstanceState( node, state, pSource ) == false )
                continue;
            const string partKey  = node.makePartKey( index );
            const string partPath = PrefabPartOverride::joinPath( rootPath, partKey );
            if ( PrefabCacheInternal::applyPartOverrides( listPartOverride, partPath, state ) == false )
            {
                SW_LOG_WARNING_ID( "PrefabAsset.OverridesNestedPrefabPart", "Overrides of nested prefab part '%#' (%#) could not be applied - it is skipped", partPath, node._prefabPath );
                continue;
            }

            const string& sourceName = pSource != nullptr ? pSource->getName() : node._name;
            const utf8*   pName      = node._name.empty() == false ? node._name.c_str() : sourceName.c_str();
            GameObject*   pPart      = pManager->createGameObject( hashed_string( StringUtil::isNullOrEmpty( pName ) ? PrefabCacheInternal::kDefaultInstance : pName ) );
            if ( pPart == nullptr )
                continue;
            ObjectLoadContext context{ SerializePurpose::Authoring };
            context._bExternalParentAllowed = false; // 부모는 노드의 자리가 정한다(아래에서 붙인다)
            if ( ObjectStateSerializer::loadFromXMLString( pPart, state, context ) == false )
            {
                SW_LOG_WARNING_ID( "PrefabAsset.NestedPrefabInstanceState", "Nested prefab instance '%#' (%#) state could not be applied - it is skipped", node._name, node._prefabPath );
                pManager->destroyGameObject( pPart );
                continue;
            }
            PrefabCacheInternal::attachNestedInstance( pPart, pParent, node._attachKey );
            registry.registerNestedPart( pPart->getObjectID(), pRoot->getObjectID(), node._prefabPath, partKey );
            inoutListSpawnedPath.push_back( partPath );
            listCreated[index] = pPart;
            ++createdCount;

            // 원본 노드면 원본의 중첩 인스턴스를 이 부품 아래에 스폰한다(펼친 노드는 그것이 뒤 노드로 이미 있다). 노드가 부품 아래에 덮어쓴 것(`<Part>`)을
            // 바깥 것보다 앞에 둔다 — 같은 부품이면 바깥 것이 나중에 얹혀 이긴다.
            if ( pSource != nullptr && pSource->getNestedInstances().empty() == false )
            {
                vector<PrefabPartOverride> listInnerPart;
                if ( PrefabOverrides::collectPartOverrides( node._overrideXML, listInnerPart ) == false )
                    listInnerPart.clear();                                                                                                           // 오류는 알렸다 — 안쪽 덮어쓴 것 없이 스폰한다
                listInnerPart.insert( listInnerPart.begin(), pSource->getVariantPartOverrides().begin(), pSource->getVariantPartOverrides().end() ); // 원본이 변형이면 그것이 가장 안쪽이다
                const PrefabPathStack::Scope scope{ spawnStack, sourceKey };
                if ( listInnerPart.empty() )
                {
                    createdCount += spawnNestedInstancesUnder( pPart, *pSource, partPath, listPartOverride, inoutListSpawnedPath );
                    continue;
                }
                PrefabCacheInternal::prependInnerParts( partPath, listInnerPart, listPartOverride );
                createdCount += spawnNestedInstancesUnder( pPart, *pSource, partPath, listInnerPart, inoutListSpawnedPath );
            }
        }
        return createdCount;
    }

    bool PrefabCache::collectPartOverrides( const GameObject& root, const PrefabAsset& asset, vector<PrefabPartOverride>& outListPartOverride )
    {
        outListPartOverride.clear();
        GameObjectManager* pManager = root.getManager();
        if ( asset.getNestedInstances().empty() || pManager == nullptr )
            return true;
        const PrefabInstanceRegistry& registry = pManager->getSubsystem<PrefabInstanceRegistry>();
        vector<uint64>                listPartID;
        PrefabCacheInternal::collectPartTree( registry, root.getObjectID(), listPartID );
        if ( listPartID.empty() )
            return true;

        // 기준: 같은 프리팹을 덮어쓴 것 없이 씬 밖에서 스폰한 부품(스폰과 같은 식이라 고치지 않은 부품은 차이가 없다).
        GameObjectManager scratch;
        GameObject*       pScratchRoot = scratch.createGameObject( hashed_string( asset.getName().empty() ? PrefabCacheInternal::kDefaultInstance : asset.getName().c_str() ) );
        if ( pScratchRoot == nullptr || applySpawnState( pScratchRoot, asset, asset.getName() ) == false )
            return false;
        const PrefabInstanceRegistry& scratchRegistry = scratch.getSubsystem<PrefabInstanceRegistry>();
        vector<uint64>                listScratchPartID;
        PrefabCacheInternal::collectPartTree( scratchRegistry, pScratchRoot->getObjectID(), listScratchPartID );
        ObjectSaveOptions options{ SerializePurpose::Authoring };
        options._bOmitExternalParent = SW_TRUE; // 부모는 노드의 자리가 정한다
        unordered_map<string, string> mapBaseState;
        for ( const uint64 scratchPartID : listScratchPartID )
        {
            string partPath;
            if ( scratchRegistry.makePartPath( scratchPartID, pScratchRoot->getObjectID(), partPath ) )
                mapBaseState[partPath] = ObjectStateSerializer::saveToXMLString( scratch.findGameObjectByID( scratchPartID ), options );
        }

        for ( const uint64 partID : listPartID )
        {
            const GameObject* pPart       = pManager->findGameObjectByID( partID );
            const bool        bLivingPart = pPart != nullptr && pPart->isPendingDestroy() == false && registry.isAttachedNestedPart( pPart );
            string            partPath;
            if ( bLivingPart == false || registry.makePartPath( partID, root.getObjectID(), partPath ) == false )
                continue; // 지운 · 옮긴 부품, 키 없이 들인 부품은 이 인스턴스의 덮어쓴 것이 아니다
            const auto baseIt = mapBaseState.find( partPath );
            if ( baseIt == mapBaseState.end() )
                continue; // 프리팹이 더는 스폰하지 않는 부품 — 다음 로드에 다시 생기지 않는다
            PrefabPartOverride part{ partPath, {} };
            if ( const expected<void> overrides = PrefabOverrides::computeOverrides( ObjectStateSerializer::saveToXMLString( pPart, options ), baseIt->second, part._overrideXML );
                 overrides.has_value() == false )
            {
                SW_LOG_WARNING_ID( "PrefabAsset.NestedPartOverridesCould", "Nested part '%#' overrides could not be computed: %# (%#)", partPath.c_str(), overrides.error().message(), overrides.error().detail() );
                return false;
            }
            if ( part._overrideXML.empty() == false )
                outListPartOverride.push_back( std::move( part ) );
        }
        return true;
    }

    bool PrefabCache::flattenNestedInstances( PrefabAsset& inoutAsset )
    {
        if ( inoutAsset.getNestedInstances().empty() )
            return true;
        vector<PrefabInstanceNode> listFlat;
        if ( appendFlattenedNodes( inoutAsset, PrefabInstanceNode::kRootParent, {}, inoutAsset.getVariantPartOverrides(), listFlat ) == false )
            return false;
        return inoutAsset.setNestedInstances( std::move( listFlat ) );
    }

    bool PrefabCache::appendFlattenedNodes( const PrefabAsset& asset, int32 parentIndex, string_view parentPath, const vector<PrefabPartOverride>& listPartOverride,
                                            vector<PrefabInstanceNode>& inoutListNode )
    {
        const vector<PrefabInstanceNode>& listNode = asset.getNestedInstances();
        vector<int32>                     listFlatIndex( listNode.size(), PrefabInstanceNode::kRootParent );
        PrefabPathStack::List&            spawnStack = t_prefabChain._spawnStack;
        for ( size_t index = 0; index < listNode.size(); ++index )
        {
            const PrefabInstanceNode& node       = listNode[index];
            const int32               flatParent = node._parentIndex < 0 ? parentIndex : listFlatIndex[static_cast<size_t>( node._parentIndex )];
            const string              partPath   = PrefabPartOverride::joinPath( parentPath, node.makePartKey( index ) );
            if ( node.isResolved() )
            {
                PrefabInstanceNode copied = node;
                copied._parentIndex       = flatParent;
                copied._partPath          = partPath;
                if ( PrefabCacheInternal::applyPartOverrides( listPartOverride, partPath, copied._resolvedStateXML ) == false )
                {
                    SW_LOG_ERROR_ID( "PrefabAsset.OverridesNestedPrefabPart2", "Overrides of nested prefab part '%#' in '%#' could not be applied - the prefab is not flattened", partPath, asset.getName() );
                    return false;
                }
                listFlatIndex[index] = static_cast<int32>( inoutListNode.size() );
                inoutListNode.push_back( std::move( copied ) );
                continue;
            }

            const string                        sourcePath = string( node._prefabPath );
            const hashed_string                 sourceKey  = PrefabPathStack::makeKey( PrefabCache::makeCacheKey( sourcePath ) );
            const shared_ptr<const PrefabAsset> pSource    = loadPrefab( sourcePath );
            if ( pSource == nullptr || VectorUtil::contains( spawnStack, sourceKey ) )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.NestedPrefabCouldNot5", "Nested prefab '%#' in '%#' could not be loaded (missing or circular) - the prefab is not flattened", sourcePath, asset.getName() );
                return false;
            }
            string baseState;
            if ( PrefabOverrides::makeBaseState( *pSource, baseState ) == false )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.NestedPrefabCouldNot6", "Nested prefab '%#' could not be built as a base state - '%#' is not flattened", sourcePath, asset.getName() );
                return false;
            }
            PrefabInstanceNode flat{};
            flat._name        = node._name.empty() ? pSource->getName() : node._name;
            flat._prefabPath  = node._prefabPath;
            flat._attachKey   = node._attachKey;
            flat._partPath    = partPath;
            flat._parentIndex = flatParent;
            vector<PrefabPartOverride> listInnerPart;
            const bool                 bStateMade = PrefabOverrides::makeInstanceState( baseState, node._overrideXML, flat._name, flat._resolvedStateXML ) &&
                                    PrefabCacheInternal::applyPartOverrides( listPartOverride, partPath, flat._resolvedStateXML ) &&
                                    PrefabOverrides::collectPartOverrides( node._overrideXML, listInnerPart );
            if ( bStateMade == false || flat.isResolved() == false )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.OverridesNestedPrefabInstance3", "Overrides of nested prefab instance '%#' in '%#' could not be applied - the prefab is not flattened", flat._name, asset.getName() );
                return false;
            }
            const int32 flatIndex = static_cast<int32>( inoutListNode.size() );
            listFlatIndex[index]  = flatIndex;
            inoutListNode.push_back( std::move( flat ) );

            // 노드가 부품 아래에 덮어쓴 것(`<Part>`)은 스폰과 같은 순서(안쪽 먼저, 바깥 것이 이김)로 아래 노드의 펼친 상태에 베이크한다.
            listInnerPart.insert( listInnerPart.begin(), pSource->getVariantPartOverrides().begin(), pSource->getVariantPartOverrides().end() );
            PrefabCacheInternal::prependInnerParts( partPath, listInnerPart, listPartOverride );
            const PrefabPathStack::Scope scope{ spawnStack, sourceKey };
            if ( appendFlattenedNodes( *pSource, flatIndex, partPath, listInnerPart, inoutListNode ) == false )
                return false;
        }
        return true;
    }

    uint32 PrefabCache::cookAllPrefabs( string_view sourceRoot, string_view cookedDir, const CookStore& store, uint32& outFailedCount )
    {
        outFailedCount = 0;
        if ( sourceRoot.empty() || cookedDir.empty() )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.PrefabCookNeedsSource", "Prefab cook needs a source root and an output directory (--cooked-dir)." );
            return 0;
        }

        vector<string> listFile;
        if ( AssetPaths::collectContentFiles( sourceRoot, {}, listFile ) == false )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.PrefabCookCouldNot", "Prefab cook could not list '%#' - nothing is cooked", sourceRoot );
            ++outFailedCount;
            return 0;
        }
        // 순서를 정한다 — 같은 쿠킹본을 쓰는 두 소스 가운데 어느 것이 먼저인지가 실행마다 같아야 한다.
        VectorUtil::sort( listFile );

        const string root = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( sourceRoot ) );
        string       out  = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( cookedDir ) );
        out += '/';

        unordered_set<string> uniqueSetWrittenKey; // 중복 판정만 한다 — 출력 순서는 정렬된 `listFile` 가 정한다
        CookStats             tally;
        const CookJobDesc     desc = makeCookJobDesc();
        for ( const string& filePath : listFile )
        {
            const string normalized = FileUtil::normalizeSeparators( filePath );
            const bool   bXML       = StringUtil::endsWith( normalized, ".prefab.xml", true );
            const bool   bJSON      = StringUtil::endsWith( normalized, ".prefab.json", true );
            if ( ( bXML || bJSON ) == false || normalized.size() <= root.size() + 1 )
                continue;

            // 출력은 `<cookedDir>/<소스 루트 기준 상대 경로>` 의 쿠킹본 이름이다 — 런타임 `loadPrefab` 과 같은 규칙 하나(`AssetCookPath`).
            const string outputID   = AssetCookPath::toCookedPath( normalized.substr( root.size() + 1 ) );
            const string outputPath = out + outputID;
            const string outputKey  = StringUtil::toLower( outputPath.c_str() );
            if ( uniqueSetWrittenKey.contains( outputKey ) )
            {
                SW_LOG_WARNING_ID( "PrefabAsset.PrefabCookWouldOverwrite", "Prefab cook: '%#' would overwrite the cooked file another source already wrote ('%#') - skipped", normalized, outputPath );
                ++outFailedCount;
                continue;
            }

            const CookResult result = store.cookFile( desc, outputID, outputPath, [&]()
            {
                // 중첩 인스턴스의 원본을 읽는 캐시는 작업마다 새로 둔다 — 앞 작업이 읽어 둔 원본을 쓰면 그 읽기가 이 작업의 입력에서 빠진다.
                PrefabCache nestedPrefabs;
                PrefabAsset asset;
                // 디스크로 훑은 원본이다 — 디스크 입구로 읽는다(리소스 id 입구는 팩 전용 구성에서 리소스 루트 안 절대 경로를 거절한다).
                const bool                   bLoaded    = asset.loadFromDiskFile( normalized );
                const bool                   bFlattened = bLoaded && asset.isValid() && nestedPrefabs.flattenNestedInstances( asset );
                const optional<AbsolutePath> outputFile = FileUtil::requireAbsolutePath( outputPath );
                if ( outputFile.has_value() == false || bFlattened == false || asset.saveToBinaryFile( *outputFile ) == false )
                {
                    SW_LOG_WARNING_ID( "PrefabAsset.PrefabCookFailed", "Prefab cook failed for '%#'", normalized );
                    return CookStep::Failed;
                }
                return CookStep::Written;
            } );
            if ( result == CookResult::Failed )
            {
                ++outFailedCount;
                continue;
            }
            uniqueSetWrittenKey.insert( outputKey );
            tally.add( result );
        }
        SW_LOG_INFO_ID( "PrefabAsset.PrefabCookReusedDDC", "Prefab cook: %# reused from the DDC, %# cooked", tally._reusedCount, tally._cookedCount );
        return tally.getWrittenCount();
    }

    CookJobDesc PrefabCache::makeCookJobDesc()
    {
        CookJobDesc desc;
        desc._cookerName      = "Prefab";
        desc._codeFingerprint = CookCodeFingerprint::find( "Prefab" );
        desc._target          = "Any";
        desc._settingsHash    = 0; // 프리팹 쿠킹에는 설정이 없다
        return desc;
    }

    string PrefabCache::findFieldOrigin( string_view assetRelativePath, string_view componentKey, string_view fieldName )
    {
        string path{ assetRelativePath };
        for ( uint32 depth = 0; depth <= DataInheritance::getMaxDepth(); ++depth ) // 읽힌 사슬은 상한 안이다 — 읽는 사이 파일이 바뀌어도 끝난다
        {
            const shared_ptr<const PrefabAsset> pAsset = loadPrefab( path );
            if ( pAsset == nullptr )
                return {};
            if ( pAsset->getBasePath().empty() || PrefabOverrides::writesField( pAsset->getVariantOverrides(), componentKey, fieldName ) )
                return path;
            path = pAsset->getBasePath();
        }
        return {};
    }

    void PrefabCache::reload( const AssetID& assetID )
    {
        const string_view assetRelativePath = assetID.view();
        if ( assetRelativePath.empty() )
            return;

        const string cacheKey = makeCacheKey( assetRelativePath );

        UniqueLock<shared_mutex> writeLock{ _mapCacheMutex };
        // 읽는 중인 로드는 옛 파일을 읽었을 수 있다 — 세대를 올려 그 결과가 캐시에 들지 않게 하고, 다음 요청은 새로 읽게 진행 중 표에서 뗀다.
        _cacheGeneration.fetch_add( 1, std::memory_order_acq_rel );
        _mapLoading.clear();
        _mapCache.erase( cacheKey );
        // 중첩 인스턴스(원본 경로)를 가진 프리팹은 다음 로드가 순환을 다시 보게 버린다. 고친 원본이 상위를 놓으면 순환이 된다.
        for ( auto cacheIt = _mapCache.begin(); cacheIt != _mapCache.end(); )
        {
            const vector<PrefabInstanceNode>& listNode       = cacheIt->second->getNestedInstances();
            bool                              bHasSourceNode = false;
            for ( const PrefabInstanceNode& node : listNode )
            {
                bHasSourceNode = bHasSourceNode || node.isResolved() == false;
            }
            if ( bHasSourceNode || cacheIt->second->getBasePath().empty() == false )
                cacheIt = _mapCache.erase( cacheIt );
            else
                ++cacheIt;
        }
    }

    shared_ptr<const PrefabAsset> AssetRefCache<PrefabAsset>::acquire( string_view path )
    {
        return engine::getSubsystem<AssetManager>().getAssetCache<PrefabCache>().loadPrefab( path );
    }

    PrefabHandle AssetRefCache<PrefabAsset>::request( string_view path )
    {
        return engine::getSubsystem<AssetManager>().getAssetCache<PrefabCache>().request( path );
    }
} // namespace sw
