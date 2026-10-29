#include "pch.h"

#include "Engine/Physics/Collision/PhysicsMeshShapeCache.h"

#include "Core/Diagnostics/CacheStats.h"
#include "Core/File/FileUtil.h"
#include "Core/Spatial/PositionWelder.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/RHI/RHITypes.h"
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

            /** @brief 셰이프 캐시 키(메시 리소스 id 의 글)를 읽기 함수에 넘길 리소스 id 로 바꾸는 한 곳입니다. 이 캐시의 키는 리소스 id 만 담는다. */
            [[nodiscard]] static AssetID toAssetID( string_view path ) { return AssetID{ path }; }

            /** @brief 물리 셰이프 캐시의 계측 칸입니다. 표는 엔진을 다시 세우면 새로 서도 칸은 프로세스 끝까지 살아 이어 센다. */
            static const CacheStatsHandle& getStats()
            {
                static const CacheStatsHandle s_stats = CacheStats::registerCache( kStatsName );
                return s_stats;
            }

            /** @brief `.mesh` 정점(삼각형 목록)의 위치만 꺼냅니다. 읽지 못하면 false 입니다. */
            [[nodiscard]] static bool loadTrianglePositions( const AssetID& assetID, vector<float3>& outListPosition )
            {
                vector<RHIVertex> listVertex;
                if ( MeshAssetFormat::loadFromAsset( assetID, listVertex ) == false )
                    return false;
                outListPosition.clear();
                outListPosition.reserve( listVertex.size() );
                for ( const RHIVertex& vertex : listVertex )
                {
                    outListPosition.push_back( float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] } );
                }
                return true;
            }

            /** @brief 표의 키입니다. 리소스 id 는 소문자로 찾으므로(`normalizePath`) 다른 철자도 같은 칸에 닿습니다. */
            static string makeKey( string_view path ) { return FileUtil::normalizePath( path ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    shared_ptr<const PhysicsMeshShapeData> PhysicsMeshShapeCache::acquire( string_view path )
    {
        if ( path.empty() )
            return nullptr;
        // 칸이 비면 셰이프를 만들 엔진이 없다(엔진 없는 도구 · 테스트, 엔진이 내려간 뒤).
        PhysicsMeshShapeTable* const pTable = engine::findSubsystem<PhysicsMeshShapeTable>();
        if ( pTable == nullptr )
            return nullptr;
        return pTable->acquire( path );
    }

    shared_ptr<const PhysicsMeshShapeData> PhysicsMeshShapeTable::acquire( string_view path )
    {
        shared_ptr<const PhysicsMeshShapeData> result;
        const string                           key = PhysicsMeshShapeCacheInternal::makeKey( path );
        // 만든 횟수는 읽기 전에 잡는다 — 읽는 동안 파일이 만들어졌으면 이 결과로 남기는 "읽지 못함" 은 다음 조회에서 낡은 것으로 버려진다.
        const uint64 creationEpoch = FileUtil::getCreationEpoch();
        {
            ScopedLock<mutex>                                                                   lock{ _mutex };
            const unordered_map<string, shared_ptr<const PhysicsMeshShapeData>>::const_iterator found = _mapPathToData.find( key );
            if ( found != _mapPathToData.end() )
            {
                PhysicsMeshShapeCacheInternal::getStats().recordHit();
                return found->second;
            }
            const unordered_map<string, uint64>::const_iterator failed = _mapFailedPathToEpoch.find( key );
            if ( failed != _mapFailedPathToEpoch.end() && failed->second == creationEpoch )
            {
                PhysicsMeshShapeCacheInternal::getStats().recordHit(); // 읽지 못한 경로를 기억한 답도 적중이다(파일이 만들어지지 않은 동안 같은 파일을 다시 읽지 않는다)
                return result;
            }
        }
        PhysicsMeshShapeCacheInternal::getStats().recordMiss();

        // 읽기와 합치기는 잠금 밖에서 한다(파일 IO). 둘이 같은 경로를 동시에 읽으면 먼저 넣은 쪽이 남고 다른 쪽은 그것을 받는다.
        vector<float3> listPosition;
        const bool     bLoaded = PhysicsMeshShapeCacheInternal::loadTrianglePositions( PhysicsMeshShapeCacheInternal::toAssetID( path ), listPosition ) && listPosition.size() >= 3;
        if ( bLoaded == false )
        {
            bool bFirstFailure = false;
            {
                // 잠금 안에서는 기록만 하고 오류는 놓은 뒤에 남긴다(잠금은 잎 잠금, 결정 5-3).
                ScopedLock<mutex> lock{ _mutex };
                const auto        failedInserted = _mapFailedPathToEpoch.try_emplace( key, creationEpoch );
                failedInserted.first->second     = creationEpoch; // 다시 읽어도 못 읽었다 — 새 횟수로 기억한다(오류는 처음 한 번만 남긴다)
                bFirstFailure                    = failedInserted.second;
            }
            if ( bFirstFailure )
                SW_LOG_ERROR( "Physics shape mesh '%#' could not be loaded or has no triangle - the shape is not created", path );
            return result;
        }

        shared_ptr<PhysicsMeshShapeData> loaded = make_shared<PhysicsMeshShapeData>();
        PhysicsMeshShapeCache::makeShapeData( span<const float3>{ listPosition.data(), listPosition.size() }, *loaded );

        ScopedLock<mutex> lock{ _mutex };
        (void)_mapFailedPathToEpoch.erase( key ); // 이제 읽혔다 — 앞서 못 읽은 기억은 버린다
        shared_ptr<const PhysicsMeshShapeData>& slot = _mapPathToData[key];
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

} // namespace sw

namespace sw
{
    PhysicsMeshShapeTable::~PhysicsMeshShapeTable()
    {
        // 표가 사라지면(서비스 범위가 닫힘) 계측의 항목 수에서 자기 몫을 뺀다 — 칸은 프로세스 끝까지 산다.
        ScopedLock<mutex> lock{ _mutex };
        PhysicsMeshShapeCacheInternal::getStats().addItemCount( -static_cast<int64>( _mapPathToData.size() ) );
    }

    void PhysicsMeshShapeTable::clear()
    {
        // 메시가 다시 읽히면 기하를 버린다.
        ScopedLock<mutex> lock{ _mutex };
        PhysicsMeshShapeCacheInternal::getStats().addItemCount( -static_cast<int64>( _mapPathToData.size() ) );
        _mapPathToData        = unordered_map<string, shared_ptr<const PhysicsMeshShapeData>>{};
        _mapFailedPathToEpoch = unordered_map<string, uint64>{};
        PhysicsMeshShapeCacheInternal::getStats().recordClear();
    }

    size_t PhysicsMeshShapeTable::getCachedCount()
    {
        ScopedLock<mutex> lock{ _mutex };
        return _mapPathToData.size();
    }
} // namespace sw
