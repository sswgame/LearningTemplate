#include "pch.h"

#include "Engine/Graphics/Mesh/MeshCache.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/LifetimeScope.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Physics/Collision/PhysicsMeshShapeCache.h"
#include "Engine/Resource/AssetLoadProfiler.h"
#include "Engine/Resource/Cache/BuiltInAssetCache.h"
#include "Engine/Resource/Cache/WeakInternTable.h"

namespace sw
{
    SW_LOG_CALLER( "MeshCache" );

    namespace
    {
        struct MeshCacheInternal
        {
            /** @brief 이미 경고한 경로입니다. 잠금과 함께 둡니다. */
            struct WarnedPathSet
            {
                mutex                             _mutex;
                unordered_set<string> _uniquePath SW_GUARDED_BY( _mutex );
            };

            /** @brief 읽는 중인 로드입니다. 같은 경로를 동시에 요청하면 같은 블록을 받습니다(이중 읽기 없음). 로드 단계가 끝나면 지웁니다. */
            struct InFlightTable
            {
                mutex                                          _mutex;
                unordered_map<string, weak_ptr<MeshLoadBlock>> _mapKeyToBlock;
            };

            /**
             * @brief 메시 캐시의 상태 전부입니다 — 공유 테이블(경로 → 약한 참조), 읽는 중인 로드, 경고한 경로.
             * @details 캐시 함수(`request` · `acquire` · `reloadShared`)가 정적이라 부르는 쪽이 범위를 들고 있지 않다. 그래서 엔진 수명 범위(`LifetimeScope`)가 소유하고
             *          엔진 종료 때 저장소째 사라진다(처음 쓸 때 만들어진다). 함수 안 정적 지역으로 두면 종료 누수 보고 뒤까지 남고 엔진을 다시 세워도 비지 않는다.
             *          Engine.dll 안에 있어 모듈 핫 리로드에 사라지지 않는다.
             */
            struct State
            {
                WeakInternTable<string, Mesh> _sharedTable{};
                InFlightTable                 _inFlight{};
                WarnedPathSet                 _warned{};
            };

            inline static ScopeOwned<State> s_state{};

            /** @brief 상태입니다. 엔진 수명 범위가 닫힌 뒤의 접근은 부른 쪽의 잘못이라 모든 구성에서 멈춥니다(캐시를 쓸 엔진이 없다). */
            static State& getState()
            {
                return s_state.getRequired();
            }

            /** @brief 메시 캐시의 계측 칸입니다(`CacheStats`). 상태가 엔진 수명 범위로 다시 서도 칸은 프로세스 끝까지 살아 이어 센다. */
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
                WarnedPathSet& warned = getState()._warned;
                string         key    = makeKey( path );
                bool           bFirst = false;
                {
                    // 잠금 안에서는 기록만 하고 경고는 놓은 뒤에 남긴다(잠금은 잎 잠금, 결정 5-3).
                    ScopedLock<mutex> lock{ warned._mutex };
                    bFirst = warned._uniquePath.insert( std::move( key ) ).second;
                }
                if ( bFirst )
                    SW_LOG_WARNING( "Mesh asset '%#' could not be loaded - nothing is drawn for it", path );
            }

            /** @brief 물리 셰이프 캐시의 읽기 함수입니다. `.mesh` 정점(삼각형 목록)의 위치만 꺼냅니다. */
            [[nodiscard]] static bool loadTrianglePositions( string_view path, vector<float3>& outListPosition )
            {
                vector<RHIVertex> listVertex;
                if ( MeshAssetFormat::loadFromResource( path, listVertex ) == false )
                    return false;
                outListPosition.clear();
                outListPosition.reserve( listVertex.size() );
                for ( const RHIVertex& vertex : listVertex )
                {
                    outListPosition.push_back( float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] } );
                }
                return true;
            }
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
        bool           bLoaded = MeshAssetFormat::loadFromResource( path, data );
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
    {
        PhysicsMeshShapeCache::setLoadFunction( &MeshCacheInternal::loadTrianglePositions );
    }

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

    bool MeshCache::reloadShared( string_view path, IRHIDevice* pDevice )
    {
        if ( path.empty() )
            return false;
        shared_ptr<Mesh> live = MeshCacheInternal::findLive( path );
        if ( live == nullptr )
            return false;

        // 읽기에 실패하면 이전 정점을 유지한다. 저장 중이라 반쯤 쓰인 파일을 읽은 경우이며, 다음 감시 이벤트가 다시 읽는다.
        MeshAssetData data{};
        if ( MeshAssetFormat::loadFromResource( path, data ) == false )
        {
            SW_LOG_ERROR( "Hot-Reload failed for Mesh %#", path );
            return false;
        }

        // 지난 프레임이 아직 이전 정점 버퍼로 그리고 있을 수 있다. setVertices 는 버퍼를 곧바로 반환하므로 먼저 기다린다(TextureCache::reload 와 같은 이유).
        if ( pDevice != nullptr )
            pDevice->waitIdle();
        live->setVerticesWithTangents( std::move( data._listVertex ) );
        live->setSkin( std::move( data._listSkinVertex ), data._skinBoneCount );
        live->setMorphTargets( std::move( data._listMorphTarget ) );
        return live->setSections( std::move( data._listSection ) ) && live->setLODs( std::move( data._listLOD ) );
    }

    bool MeshCache::isCached( string_view relativePath ) const
    {
        return MeshCacheInternal::findLive( relativePath ) != nullptr;
    }

    void MeshCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)reloadShared( relativePath, pDevice ); // 사용 중인 메시가 없으면 다시 읽을 것이 없다. 다음에 읽는 쪽이 새 내용을 읽는다
        PhysicsMeshShapeCache::clear();              // 물리 캐시는 렌더 메시와 따로 보관한다. 이미 만든 바디는 이전 기하 그대로다
    }

    size_t MeshCache::getCachedCount() const
    {
        return MeshCacheInternal::getState()._sharedTable.countLive();
    }

    shared_ptr<const void> MeshCache::pinAsset( string_view relativePath )
    {
        if ( MeshAssetFormat::isMeshAssetPath( relativePath ) == false )
            return {};
        return request( relativePath ).getBlock();
    }

    void MeshCache::clear()
    {
        // 상태를 만들지 않고 찾는다 — 아직 아무것도 읽지 않았으면 비울 것이 없다. 종료 때의 반납은 상태를 소유한 엔진 수명 범위가 한다.
        MeshCacheInternal::State* const pState = MeshCacheInternal::s_state.find();
        if ( pState != nullptr )
        {
            pState->_sharedTable.clear();
            // 다시 읽으면 다시 알리도록 경고 기록을 잊는다.
            ScopedLock<mutex> lock{ pState->_warned._mutex };
            pState->_warned._uniquePath = unordered_set<string>{};
        }
        MeshCacheInternal::getStats().recordClear();
        PhysicsMeshShapeCache::clear();
    }
} // namespace sw

namespace sw
{
    SW_BUILT_IN_ASSET_CACHE( MeshCache );
} // namespace sw
