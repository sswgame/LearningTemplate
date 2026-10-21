#include "pch.h"

#include "Engine/Graphics/Mesh/MeshCache.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
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
                mutex                 _mutex;
                unordered_set<string> _uniquePath;
            };

            /** @brief 프로세스에 하나인 공유 테이블입니다(Engine.dll 안에 있어 모듈 핫 리로드에 사라지지 않습니다). 경로를 키로 약한 참조를 보관합니다. */
            static WeakInternTable<string, Mesh>& getSharedTable()
            {
                static WeakInternTable<string, Mesh> s_table;
                return s_table;
            }

            static WarnedPathSet& getWarnedPathSet()
            {
                static WarnedPathSet s_set;
                return s_set;
            }

            /** @brief 읽는 중인 로드입니다. 같은 경로를 동시에 요청하면 같은 블록을 받습니다(이중 읽기 없음). 로드 단계가 끝나면 지웁니다. */
            struct InFlightTable
            {
                mutex                                          _mutex;
                unordered_map<string, weak_ptr<MeshLoadBlock>> _mapKeyToBlock;
            };

            static InFlightTable& getInFlightTable()
            {
                static InFlightTable s_table;
                return s_table;
            }

            /** @brief 테이블의 키입니다. 리소스 id 는 소문자로 찾으므로(`normalizePath`) 핫 리로드가 넘긴 철자도 같은 항목에 닿습니다. */
            static string makeKey( string_view path ) { return FileUtil::normalizePath( path ); }

            /** @brief 그 경로를 지금 보관 중인 메시입니다. 없으면 nullptr 입니다. */
            static shared_ptr<Mesh> findLive( string_view path ) { return getSharedTable().findLive( makeKey( path ) ); }

            /** @brief 읽지 못한 경로를 처음 한 번만 경고합니다. */
            static void warnLoadFailureOnce( string_view path )
            {
                WarnedPathSet&          warned = getWarnedPathSet();
                std::scoped_lock<mutex> lock{ warned._mutex };
                if ( warned._uniquePath.insert( makeKey( path ) ).second )
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
        const bool     bLoaded = MeshAssetFormat::loadFromResource( path, data );
        if ( bLoaded )
        {
            loadScope.setBytes( data._listVertex.size() * sizeof( RHIVertex ) + data._listSkinVertex.size() * sizeof( MeshSkinVertex ) );
            loadScope.setSucceeded();
            shared_ptr<Mesh> loaded = Mesh::create();
            loaded->setVerticesWithTangents( std::move( data._listVertex ) );
            if ( data._skinBoneCount > 0 )
                loaded->setSkin( std::move( data._listSkinVertex ), data._skinBoneCount );
            if ( data._listMorphTarget.empty() == false )
                loaded->setMorphTargets( std::move( data._listMorphTarget ) );
            _pMesh = MeshCacheInternal::getSharedTable().insertOrGetLive( key, std::move( loaded ) );
        }

        // 결과가 캐시에 든 **뒤에** 진행 중 표에서 뺀다 — 그 사이에 온 요청은 진행 중 블록이나 캐시 둘 중 하나를 반드시 본다.
        {
            MeshCacheInternal::InFlightTable& inFlight = MeshCacheInternal::getInFlightTable();
            std::scoped_lock<mutex>           lock{ inFlight._mutex };
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
            shared_ptr<MeshLoadBlock> pFailed = make_shared<MeshLoadBlock>( path );
            pFailed->markFailedWithoutLoad( "the mesh path is empty" );
            return MeshHandle{ std::move( pFailed ) };
        }

        const string                      key      = MeshCacheInternal::makeKey( path );
        MeshCacheInternal::InFlightTable& inFlight = MeshCacheInternal::getInFlightTable();
        shared_ptr<MeshLoadBlock>         pBlock;
        {
            std::scoped_lock<mutex> lock{ inFlight._mutex };
            const auto              it = inFlight._mapKeyToBlock.find( key );
            if ( it != inFlight._mapKeyToBlock.end() )
                pBlock = it->second.lock();
            if ( pBlock != nullptr )
                return MeshHandle{ std::move( pBlock ) };

            // 진행 중 표를 잡은 채 캐시를 본다 — 로드는 캐시에 넣은 뒤 표에서 빼므로, 여기서 둘 다 없으면 정말 아무도 읽지 않았다.
            pBlock                = make_shared<MeshLoadBlock>( path );
            shared_ptr<Mesh> live = MeshCacheInternal::getSharedTable().findLive( key );
            if ( live != nullptr )
            {
                pBlock->_pMesh = std::move( live );
                pBlock->markReadyWithoutLoad();
                return MeshHandle{ std::move( pBlock ) };
            }
            inFlight._mapKeyToBlock[key] = pBlock;
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
        return true;
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
        return MeshCacheInternal::getSharedTable().countLive();
    }

    void MeshCache::clear()
    {
        MeshCacheInternal::getSharedTable().clear();
        MeshCacheInternal::WarnedPathSet& warned = MeshCacheInternal::getWarnedPathSet();
        std::scoped_lock<mutex>           lock{ warned._mutex };
        warned._uniquePath = unordered_set<string>{};
        PhysicsMeshShapeCache::clear();
    }
} // namespace sw

namespace sw
{
    SW_BUILT_IN_ASSET_CACHE( MeshCache );
} // namespace sw
