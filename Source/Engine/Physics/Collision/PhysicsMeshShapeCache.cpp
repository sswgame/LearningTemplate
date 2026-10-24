#include "pch.h"

#include "Engine/Physics/Collision/PhysicsMeshShapeCache.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/LifetimeScope.h"
#include "Core/Spatial/PositionWelder.h"

#include "Engine/Physics/Collision/PhysicsShape.h"

namespace sw
{
    SW_LOG_CALLER( "PhysicsMeshShapeCache" );

    namespace
    {
        struct PhysicsMeshShapeCacheInternal
        {
            /** @brief 물리 셰이프 캐시의 계측 칸 이름입니다(`CacheStats`). `IAssetCache` 가 아니라 레지스트리에 종류 이름이 없어 이 이름으로 올린다. */
            static constexpr const utf8* kStatsName = "PhysicsMeshShape";

            /** @brief 물리 셰이프 캐시의 계측 칸입니다. 표는 엔진 수명 범위로 다시 서도 칸은 프로세스 끝까지 살아 이어 센다. */
            static const CacheStatsHandle& getStats()
            {
                static const CacheStatsHandle s_stats = CacheStats::registerCache( kStatsName );
                return s_stats;
            }

            /**
             * @brief 프로세스에 하나인 표입니다(Engine.dll 안). 경로 → 기하, 읽지 못한 경로, 그리고 `.mesh` 를 읽는 함수.
             * @details **상한은 두지 않습니다.** 표의 크기는 경로 수이고, 경로는 물리 셰이프 서술자가 적은 `.mesh` 리소스 경로뿐이라 콘텐츠에 있는 메시 파일 수가 위쪽 한계입니다
             *          (런타임에 만들어지는 경로 · 키가 없고, 같은 경로는 한 항목). 항목은 메시를 합친 점 · 인덱스(원본 메시 정점보다 작다)이고 씬을 바꿔도 같은 메시를 다시 읽지
             *          않으려고 남깁니다 — 씬 교체 때 비우면 재방문마다 파일 읽기와 합치기를 다시 한다. 비우는 때는 메시가 다시 읽힐 때(`MeshCache::reload`)와 종료(`MeshCache::clear`)뿐이다.
             *          읽지 못한 경로 기억도 같은 한계(콘텐츠의 깨진 경로 수)다. 크기는 `[CacheStats] PhysicsMeshShape` 줄의 항목 수로 보고, 콘텐츠가 수천 메시로 자라 문제가 되면 그때 바이트 상한을 둔다.
             */
            struct Table
            {
                mutex                                                         _mutex;
                unordered_map<string, shared_ptr<const PhysicsMeshShapeData>> _mapPathToData;
                unordered_set<string>                                         _uniqueFailedPath;
                /** @brief 삼각형 목록을 읽는 함수입니다(`setLoadFunction`). 표와 함께 엔진 수명 범위가 소유한다 — 메시 캐시가 서면 다시 건다. */
                atomic<PhysicsMeshShapeCache::LoadTriangleListFunction> _pfnLoad{ nullptr };

                /** @brief 표가 사라지면(엔진 수명 범위가 닫힘) 계측의 항목 수에서 자기 몫을 뺍니다. */
                ~Table() { getStats().addItemCount( -static_cast<int64>( _mapPathToData.size() ) ); }
            };

            /** @brief 표는 엔진 수명 범위가 소유해 엔진 종료 때 저장소째 사라집니다(처음 찾을 때 만들어진다). */
            inline static ScopeOwned<Table> s_table{};

            /** @brief 표의 키입니다. 리소스 id 는 소문자로 찾으므로(`normalizePath`) 다른 철자도 같은 칸에 닿습니다. */
            static string makeKey( string_view path ) { return FileUtil::normalizePath( path ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void PhysicsMeshShapeCache::setLoadFunction( LoadTriangleListFunction pfnLoad )
    {
        PhysicsMeshShapeCacheInternal::Table* const pTable = PhysicsMeshShapeCacheInternal::s_table.getOrCreate();
        // nullptr 은 엔진 수명 범위가 닫힌 뒤의 접근이다(Debug 는 `getOrCreate` 가 단언한다) — 읽는 함수를 맡길 표가 없다.
        if ( pTable != nullptr )
            pTable->_pfnLoad.store( pfnLoad, std::memory_order_release );
    }

    PhysicsMeshShapeCache::LoadTriangleListFunction PhysicsMeshShapeCache::getLoadFunction()
    {
        const PhysicsMeshShapeCacheInternal::Table* const pTable = PhysicsMeshShapeCacheInternal::s_table.find();
        return pTable != nullptr ? pTable->_pfnLoad.load( std::memory_order_acquire ) : nullptr; // 표가 없으면 아무도 함수를 걸지 않았다
    }

    shared_ptr<const PhysicsMeshShapeData> PhysicsMeshShapeCache::acquire( string_view path )
    {
        shared_ptr<const PhysicsMeshShapeData> result;
        if ( path.empty() )
            return result;

        PhysicsMeshShapeCacheInternal::Table* const pTable = PhysicsMeshShapeCacheInternal::s_table.getOrCreate();
        // nullptr 은 엔진 수명 범위가 닫힌 뒤의 접근이다(Debug 는 `getOrCreate` 가 단언한다) — 셰이프를 만들 엔진이 없다.
        if ( pTable == nullptr )
            return result;
        PhysicsMeshShapeCacheInternal::Table& table = *pTable;
        const string                          key   = PhysicsMeshShapeCacheInternal::makeKey( path );
        {
            std::scoped_lock<mutex>                                                             lock{ table._mutex };
            const unordered_map<string, shared_ptr<const PhysicsMeshShapeData>>::const_iterator found = table._mapPathToData.find( key );
            if ( found != table._mapPathToData.end() )
            {
                PhysicsMeshShapeCacheInternal::getStats().recordHit();
                return found->second;
            }
            if ( table._uniqueFailedPath.count( key ) != 0 )
            {
                PhysicsMeshShapeCacheInternal::getStats().recordHit(); // 읽지 못한 경로를 기억한 답도 적중이다(같은 파일을 다시 읽지 않는다)
                return result;
            }
        }
        PhysicsMeshShapeCacheInternal::getStats().recordMiss();

        // 읽기와 합치기는 잠금 밖에서 한다(파일 IO). 둘이 같은 경로를 동시에 읽으면 먼저 넣은 쪽이 남고 다른 쪽은 그것을 받는다.
        const LoadTriangleListFunction pfnLoad = table._pfnLoad.load( std::memory_order_acquire );
        vector<float3>                 listPosition;
        const bool                     bLoaded = pfnLoad != nullptr && pfnLoad( path, listPosition ) && listPosition.size() >= 3;
        if ( bLoaded == false )
        {
            std::scoped_lock<mutex> lock{ table._mutex };
            if ( table._uniqueFailedPath.insert( key ).second )
            {
                if ( pfnLoad == nullptr )
                    SW_LOG_ERROR( "Physics shape mesh '%#' needs a load function - PhysicsMeshShapeCache::setLoadFunction was never called", path );
                else
                    SW_LOG_ERROR( "Physics shape mesh '%#' could not be loaded or has no triangle - the shape is not created", path );
            }
            return result;
        }

        shared_ptr<PhysicsMeshShapeData> loaded = make_shared<PhysicsMeshShapeData>();
        makeShapeData( span<const float3>{ listPosition.data(), listPosition.size() }, *loaded );

        std::scoped_lock<mutex>                 lock{ table._mutex };
        shared_ptr<const PhysicsMeshShapeData>& slot = table._mapPathToData[key];
        if ( slot == nullptr )
        {
            slot = std::move( loaded );
            PhysicsMeshShapeCacheInternal::getStats().addItemCount( 1 );
        }
        result = slot;
        return result;
    }

    PhysicsShapeGeometry PhysicsMeshShapeCache::acquireGeometry( const PhysicsShapeDesc3D& shape )
    {
        PhysicsShapeGeometry geometry;
        const bool           bFromMesh = shape._meshPath.empty() == false && shape._listPoint.empty();
        if ( bFromMesh == false )
        {
            geometry._listPoint = span<const float3>{ shape._listPoint.data(), shape._listPoint.size() };
            geometry._listIndex = span<const uint32>{ shape._listIndex.data(), shape._listIndex.size() };
            return geometry;
        }
        geometry._pMeshData = acquire( shape._meshPath );
        if ( geometry._pMeshData == nullptr )
            return geometry;
        geometry._listPoint = span<const float3>{ geometry._pMeshData->_listPoint.data(), geometry._pMeshData->_listPoint.size() };
        geometry._listIndex = span<const uint32>{ geometry._pMeshData->_listIndex.data(), geometry._pMeshData->_listIndex.size() };
        return geometry;
    }

    void PhysicsMeshShapeCache::makeShapeData( span<const float3> listTrianglePosition, PhysicsMeshShapeData& outData )
    {
        outData._listPoint.clear();
        outData._listIndex.clear();
        const size_t positionCount = listTrianglePosition.size() - listTrianglePosition.size() % 3;
        if ( positionCount == 0 )
            return;

        // 허용 오차 안의 위치를 한 점으로 합친다. 점 순서는 처음 본 순서라 같은 입력이면 같은 점 · 인덱스가 나온다(`PositionWelder`).
        PositionWelder welder;
        vector<uint32> listRemap;
        welder.weldAll( listTrianglePosition.first( positionCount ), listRemap );
        outData._listPoint = welder.getPoints();

        outData._listIndex.reserve( positionCount );
        for ( size_t triangleStart = 0; triangleStart < positionCount; triangleStart += 3 )
        {
            const uint32 indexA      = listRemap[triangleStart];
            const uint32 indexB      = listRemap[triangleStart + 1];
            const uint32 indexC      = listRemap[triangleStart + 2];
            const bool   bDegenerate = indexA == indexB || indexB == indexC || indexA == indexC;
            if ( bDegenerate )
                continue;
            outData._listIndex.push_back( indexA );
            outData._listIndex.push_back( indexB );
            outData._listIndex.push_back( indexC );
        }
    }

    void PhysicsMeshShapeCache::clear()
    {
        // 메시가 다시 읽히면 기하를 버린다. 종료 때의 반납은 표를 소유한 엔진 수명 범위가 한다.
        PhysicsMeshShapeCacheInternal::Table* const pTable = PhysicsMeshShapeCacheInternal::s_table.find();
        if ( pTable == nullptr )
            return;
        std::scoped_lock<mutex> lock{ pTable->_mutex };
        PhysicsMeshShapeCacheInternal::getStats().addItemCount( -static_cast<int64>( pTable->_mapPathToData.size() ) );
        pTable->_mapPathToData    = unordered_map<string, shared_ptr<const PhysicsMeshShapeData>>{};
        pTable->_uniqueFailedPath = unordered_set<string>{};
        PhysicsMeshShapeCacheInternal::getStats().recordClear();
    }

    size_t PhysicsMeshShapeCache::getCachedCount()
    {
        PhysicsMeshShapeCacheInternal::Table* const pTable = PhysicsMeshShapeCacheInternal::s_table.find();
        if ( pTable == nullptr )
            return 0;
        std::scoped_lock<mutex> lock{ pTable->_mutex };
        return pTable->_mapPathToData.size();
    }
} // namespace sw
