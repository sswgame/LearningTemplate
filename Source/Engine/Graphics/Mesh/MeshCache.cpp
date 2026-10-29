#include "pch.h"

#include "Engine/Graphics/Mesh/MeshCache.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetLoadProfiler.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/BuiltInAssetCache.h"
#include "Engine/Asset/Cache/WeakInternTable.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Mesh/IMeshDerivedCache.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    SW_LOG_CALLER( "MeshCache" );

    /**
     * @struct MeshCache::State
     * @brief 메시 캐시의 상태 전부입니다 — 공유 테이블(경로 → 약한 참조), 읽는 중인 로드, 경고한 경로.
     * @details 캐시(에셋 매니저가 소유한 내장 캐시)가 든다. 정적 창구(`request` · `acquire` · `reloadShared`)와 로드 블록은 엔진의 에셋 매니저에서 찾은 캐시의 것을 쓴다.
     *          예전에는 소유자 없는 엔진 수명 값(`ScopeOwned`)이었다.
     */
    struct MeshCache::State
    {
        /** @brief 읽는 중인 로드입니다. 같은 경로를 동시에 요청하면 같은 블록을 받습니다(이중 읽기 없음). 로드 단계가 끝나면 지웁니다. */
        struct InFlightTable
        {
            mutex                                          _mutex;
            unordered_map<string, weak_ptr<MeshLoadBlock>> _mapKeyToBlock;
        };

        WeakInternTable<string, Mesh> _sharedTable{};
        InFlightTable                 _inFlight{};
        OnceKeySet                    _warned{}; ///< 읽지 못해 경고한 경로(캐시를 비우면 잊는다)
    };

    namespace
    {
        struct MeshCacheInternal
        {
            using InFlightTable = MeshCache::State::InFlightTable;

            /** @brief 엔진의 메시 캐시 상태입니다. 엔진 서비스가 없으면(캐시를 쓸 엔진이 없다) 멈춘다. */
            static MeshCache::State& getState() { return engine::getSubsystem<AssetManager>().getAssetCache<MeshCache>().getState(); }

            /** @brief 메시 캐시의 계측 칸입니다(`CacheStats`). 엔진을 다시 세워 캐시가 새로 서도 칸은 프로세스 끝까지 살아 이어 센다. */
            static const CacheStatsHandle& getStats()
            {
                static const CacheStatsHandle s_stats = CacheStats::getOrCreate( MeshCache::kAssetKindName );
                return s_stats;
            }

            /** @brief 테이블의 키입니다. 리소스 id 는 소문자로 찾으므로(`normalizePath`) 핫 리로드가 넘긴 철자도 같은 항목에 닿습니다. */
            static string makeKey( string_view path ) { return FileUtil::normalizePath( path ); }

            /** @brief 그 경로를 지금 보관 중인 메시입니다. 없으면 nullptr 입니다. */
            static shared_ptr<Mesh> findLive( string_view path ) { return getState()._sharedTable.findLive( makeKey( path ) ); }

            /** @brief 읽지 못한 경로를 처음 한 번만 경고합니다. */
            static void warnLoadFailureOnce( string_view path )
            {
                if ( getState()._warned.tryClaim( HashUtil::computeStringHash64( makeKey( path ), false ) ) )
                    SW_LOG_WARNING( "Mesh asset '%#' could not be loaded - nothing is drawn for it", path );
            }

            /** @brief 캐시 키(획득 · 리로드가 넘긴 경로)를 로더에 넘길 리소스 id 로 바꾸는 한 곳입니다. 메시 캐시의 키는 리소스 id 만 담는다. */
            [[nodiscard]] static AssetID toAssetID( string_view path ) { return AssetID{ path }; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MeshLoadBlock::MeshLoadBlock( string_view path )
        : AssetLoadBlock{ MeshCache::kAssetKindName, path }
        , _pMesh{}
    {
    }

    bool MeshLoadBlock::runLoad( string& outError )
    {
        SW_MEMORY_SCOPE( Mesh );
        const string_view path = getPath();
        const string      key  = MeshCacheInternal::makeKey( path );

        // 읽기는 잠금 밖에서 한다(파일 IO). 다른 길(`reloadShared` 뒤의 새 요청)이 먼저 넣었으면 그것을 받는다.
        MeshAssetData  data{};
        AssetLoadScope loadScope( "Mesh", path );
        bool           bLoaded = MeshAssetFormat::loadFromAsset( MeshCacheInternal::toAssetID( path ), data );
        if ( bLoaded )
        {
            loadScope.setBytes( data._listVertex.size() * sizeof( RHIVertex ) + data._listSkinVertex.size() * sizeof( MeshSkinVertex ) );
            shared_ptr<Mesh> loaded = Mesh::create();
            loaded->setVerticesWithTangents( std::move( data._listVertex ) );
            if ( data._skinBoneCount > 0 )
                loaded->setSkin( std::move( data._listSkinVertex ), data._skinBoneCount );
            if ( data._listMorphTarget.empty() == false )
                loaded->setMorphTargets( std::move( data._listMorphTarget ) );
            // 구간 · LOD 가 정점과 맞지 않으면 파일이 깨진 것이다 — 구간 · LOD 없는 메시로 두지 않고 읽기 실패로 알린다.
            bLoaded = loaded->setSections( std::move( data._listSection ) ) && loaded->setLODs( std::move( data._listLOD ) );
            if ( bLoaded )
            {
                loadScope.setSucceeded();
                _pMesh = MeshCacheInternal::getState()._sharedTable.insertOrGetLive( key, std::move( loaded ) );
            }
        }

        // 결과가 캐시에 든 **뒤에** 진행 중 표에서 뺀다 — 그 사이에 온 요청은 진행 중 블록이나 캐시 둘 중 하나를 반드시 본다.
        {
            MeshCacheInternal::InFlightTable& inFlight = MeshCacheInternal::getState()._inFlight;
            ScopedLock<mutex>                 lock{ inFlight._mutex };
            inFlight._mapKeyToBlock.erase( key );
        }
        if ( bLoaded == false )
        {
            MeshCacheInternal::warnLoadFailureOnce( path );
            outError = "the mesh asset could not be read";
            return false;
        }
        return true;
    }
} // namespace sw

namespace sw
{
    MeshCache::MeshCache()
        : IAssetCache{ CacheTracking::counted( kAssetKindName ) }
        , _pState{ make_unique<State>() }
    {
    }

    MeshCache::~MeshCache() = default;

    MeshHandle MeshCache::request( string_view path )
    {
        return requestWith( path, AssetLoadStart::Async );
    }

    MeshHandle MeshCache::requestWith( string_view path, AssetLoadStart start )
    {
        if ( path.empty() )
        {
            shared_ptr<MeshLoadBlock> pFailed = sw::make_shared<MeshLoadBlock>( path );
            pFailed->markFailedWithoutLoad( "the mesh path is empty" );
            return MeshHandle{ std::move( pFailed ) };
        }

        const string                      key      = MeshCacheInternal::makeKey( path );
        MeshCacheInternal::InFlightTable& inFlight = MeshCacheInternal::getState()._inFlight;
        shared_ptr<MeshLoadBlock>         pBlock;
        {
            ScopedLock<mutex> lock{ inFlight._mutex };
            const auto        it = inFlight._mapKeyToBlock.find( key );
            if ( it != inFlight._mapKeyToBlock.end() )
                pBlock = it->second.lock();
            if ( pBlock != nullptr )
            {
                MeshCacheInternal::getStats().recordHit();
                return MeshHandle{ std::move( pBlock ) };
            }

            // 진행 중 표를 잡은 채 캐시를 본다 — 로드는 캐시에 넣은 뒤 표에서 빼므로, 여기서 둘 다 없으면 정말 아무도 읽지 않았다.
            pBlock                = sw::make_shared<MeshLoadBlock>( path );
            shared_ptr<Mesh> live = MeshCacheInternal::getState()._sharedTable.findLive( key );
            if ( live != nullptr )
            {
                pBlock->_pMesh = std::move( live );
                pBlock->markReadyWithoutLoad();
                MeshCacheInternal::getStats().recordHit();
                return MeshHandle{ std::move( pBlock ) };
            }
            inFlight._mapKeyToBlock[key] = pBlock;
            MeshCacheInternal::getStats().recordMiss();
        }
        AssetLoadBlock::startLoad( pBlock, start );
        return MeshHandle{ std::move( pBlock ) };
    }

    shared_ptr<Mesh> MeshCache::acquire( string_view path )
    {
        return requestWith( path, AssetLoadStart::OnCallingThread ).wait();
    }

    shared_ptr<Mesh> MeshCache::findLiveMesh( string_view path )
    {
        return MeshCacheInternal::findLive( path );
    }

    bool MeshCache::isCached( string_view relativePath ) const
    {
        return _pState->_sharedTable.findLive( MeshCacheInternal::makeKey( relativePath ) ) != nullptr;
    }

    size_t MeshCache::getCachedCount() const
    {
        return _pState->_sharedTable.countLive();
    }

    shared_ptr<const void> MeshCache::pinAsset( string_view relativePath )
    {
        if ( MeshAssetFormat::isMeshAssetPath( relativePath ) == false )
            return {};
        return request( relativePath ).getBlock();
    }

    void MeshCache::clear()
    {
        // 자기 상태를 비운다(매니저 종료 때도 불려 서비스 칸을 거치지 않는다). 다시 읽으면 다시 알리도록 경고 기록도 잊는다.
        _pState->_sharedTable.clear();
        _pState->_warned.clear();
        MeshCacheInternal::getStats().recordClear();
        invalidateDerivedCaches();
    }
} // namespace sw

namespace sw
{
    SW_BUILT_IN_ASSET_CACHE( MeshCache );
} // namespace sw

namespace sw
{
    void MeshCache::invalidateDerivedCaches()
    {
        IMeshDerivedCache* const pDerived = engine::findSubsystem<IMeshDerivedCache>();
        if ( pDerived != nullptr )
            pDerived->onMeshesInvalidated();
    }
} // namespace sw
