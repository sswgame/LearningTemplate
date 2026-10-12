#include "pch.h"

#include "Engine/Physics/Collision/PhysicsMeshShapeCache.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"

#include "Engine/Physics/Collision/PhysicsShape.h"

namespace sw
{
    SW_LOG_CALLER( "PhysicsMeshShapeCache" );

    namespace
    {
        struct PhysicsMeshShapeCacheInternal
        {
            /** @brief 프로세스에 하나인 표입니다(Engine.dll 안). 경로 → 기하, 그리고 읽지 못한 경로. */
            struct Table
            {
                mutex                                                         _mutex;
                unordered_map<string, shared_ptr<const PhysicsMeshShapeData>> _mapPathToData;
                unordered_set<string>                                         _uniqueFailedPath;
            };

            static Table& getTable()
            {
                static Table s_table;
                return s_table;
            }

            static atomic<PhysicsMeshShapeCache::LoadTriangleListFunction>& getLoadFunction()
            {
                static atomic<PhysicsMeshShapeCache::LoadTriangleListFunction> s_pfnLoad{ nullptr };
                return s_pfnLoad;
            }

            /** @brief 표의 키입니다. 리소스 id 는 소문자로 찾으므로(`normalizePath`) 다른 철자도 같은 칸에 닿습니다. */
            static string makeKey( string_view path ) { return FileUtil::normalizePath( path ); }

            /** @brief 위치 둘이 같은지 봅니다. 합치기는 비트가 같은 위치만 합친다(임포터가 삼각형마다 같은 값을 복사한다). */
            static bool isSamePosition( const float3& lhs, const float3& rhs ) { return lhs._x == rhs._x && lhs._y == rhs._y && lhs._z == rhs._z; }

            static bool isLessPosition( const float3& lhs, const float3& rhs )
            {
                if ( lhs._x != rhs._x )
                    return lhs._x < rhs._x;
                if ( lhs._y != rhs._y )
                    return lhs._y < rhs._y;
                return lhs._z < rhs._z;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void PhysicsMeshShapeCache::setLoadFunction( LoadTriangleListFunction pfnLoad )
    {
        PhysicsMeshShapeCacheInternal::getLoadFunction().store( pfnLoad, std::memory_order_release );
    }

    bool PhysicsMeshShapeCache::hasLoadFunction()
    {
        return PhysicsMeshShapeCacheInternal::getLoadFunction().load( std::memory_order_acquire ) != nullptr;
    }

    shared_ptr<const PhysicsMeshShapeData> PhysicsMeshShapeCache::acquire( string_view path )
    {
        shared_ptr<const PhysicsMeshShapeData> result;
        if ( path.empty() )
            return result;

        PhysicsMeshShapeCacheInternal::Table& table = PhysicsMeshShapeCacheInternal::getTable();
        const string                          key   = PhysicsMeshShapeCacheInternal::makeKey( path );
        {
            std::scoped_lock<mutex>                                                             lock{ table._mutex };
            const unordered_map<string, shared_ptr<const PhysicsMeshShapeData>>::const_iterator found = table._mapPathToData.find( key );
            if ( found != table._mapPathToData.end() )
                return found->second;
            if ( table._uniqueFailedPath.count( key ) != 0 )
                return result;
        }

        // 읽기와 합치기는 잠금 밖에서 한다(파일 IO). 둘이 같은 경로를 동시에 읽으면 먼저 넣은 쪽이 남고 다른 쪽은 그것을 받는다.
        const LoadTriangleListFunction pfnLoad = PhysicsMeshShapeCacheInternal::getLoadFunction().load( std::memory_order_acquire );
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
            slot = std::move( loaded );
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

        // 위치 순서로 정렬해 같은 위치를 이웃으로 모은 뒤 번호를 매긴다(해시 없이 결정적이다 — 점 순서는 위치 순서다).
        vector<uint32> listOrder( positionCount );
        for ( size_t index = 0; index < positionCount; ++index )
        {
            listOrder[index] = static_cast<uint32>( index );
        }
        std::sort( listOrder.begin(), listOrder.end(), [&listTrianglePosition]( uint32 lhs, uint32 rhs )
        {
            const float3& positionA = listTrianglePosition[lhs];
            const float3& positionB = listTrianglePosition[rhs];
            if ( PhysicsMeshShapeCacheInternal::isSamePosition( positionA, positionB ) )
                return lhs < rhs;
            return PhysicsMeshShapeCacheInternal::isLessPosition( positionA, positionB );
        } );

        vector<uint32> listRemap( positionCount );
        for ( const uint32 positionIndex : listOrder )
        {
            const float3& position  = listTrianglePosition[positionIndex];
            const bool    bNewPoint = outData._listPoint.empty() || PhysicsMeshShapeCacheInternal::isSamePosition( outData._listPoint.back(), position ) == false;
            if ( bNewPoint )
                outData._listPoint.push_back( position );
            listRemap[positionIndex] = static_cast<uint32>( outData._listPoint.size() - 1 );
        }

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
        PhysicsMeshShapeCacheInternal::Table& table = PhysicsMeshShapeCacheInternal::getTable();
        std::scoped_lock<mutex>               lock{ table._mutex };
        table._mapPathToData    = unordered_map<string, shared_ptr<const PhysicsMeshShapeData>>{};
        table._uniqueFailedPath = unordered_set<string>{};
    }

    size_t PhysicsMeshShapeCache::getCachedCount()
    {
        PhysicsMeshShapeCacheInternal::Table& table = PhysicsMeshShapeCacheInternal::getTable();
        std::scoped_lock<mutex>               lock{ table._mutex };
        return table._mapPathToData.size();
    }
} // namespace sw
