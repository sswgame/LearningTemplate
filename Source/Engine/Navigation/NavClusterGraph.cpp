#include "pch.h"

#include "Engine/Navigation/NavClusterGraph.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/ByteStream.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct NavClusterGraphInternal
        {
            /** @brief A* 열린 목록의 한 칸입니다(추정 총비용이 작은 것이 먼저, 같으면 자리가 작은 것 — 결정적). */
            struct OpenNode
            {
                float32 _estimate{ 0.0f };
                uint32  _cluster{ 0 };

                bool operator>( const OpenNode& other ) const { return _estimate != other._estimate ? _estimate > other._estimate : _cluster > other._cluster; }
            };

            /** @brief 낡은 열린 칸을 가리는 여유입니다(넣을 때 더한 추정과 지금 비용의 부동소수 차). */
            static constexpr float32 kStaleEstimateTolerance = 1.0e-3f;

            static bool isTileBefore( const NavCluster& lhs, const NavCluster& rhs ) { return lhs._tileZ != rhs._tileZ ? lhs._tileZ < rhs._tileZ : lhs._tileX < rhs._tileX; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void NavClusterGraph::clear()
    {
        _listCluster.clear();
        _listEdge.clear();
    }

    bool NavClusterGraph::build( vector<NavCluster> listCluster, const vector<NavClusterLink>& listLink )
    {
        clear();
        const uint32           clusterCount = static_cast<uint32>( listCluster.size() );
        vector<NavClusterLink> listSorted;
        listSorted.reserve( listLink.size() );
        for ( const NavClusterLink& link : listLink )
        {
            if ( link._from >= clusterCount || link._to >= clusterCount )
            {
                SW_LOG_ERROR_ID( "NavClusterGraph.LinkOutOfRange", "Navmesh cluster link %# -> %# is outside the %# clusters - the cluster graph is left empty", link._from, link._to, clusterCount );
                return false;
            }
            if ( link._from != link._to )
                listSorted.push_back( link );
        }
        VectorUtil::sort( listSorted, []( const NavClusterLink& lhs, const NavClusterLink& rhs )
        {
            return lhs._from != rhs._from ? lhs._from < rhs._from : lhs._to < rhs._to;
        } );
        listSorted.erase( std::unique( listSorted.begin(), listSorted.end(), []( const NavClusterLink& lhs, const NavClusterLink& rhs )
        {
            return lhs._from == rhs._from && lhs._to == rhs._to;
        } ),
                          listSorted.end() );
        _listCluster = std::move( listCluster );
        _listEdge.reserve( listSorted.size() );
        size_t linkIndex = 0;
        for ( uint32 clusterIndex = 0; clusterIndex < clusterCount; ++clusterIndex )
        {
            NavCluster& cluster = _listCluster[clusterIndex];
            cluster._firstEdge  = static_cast<uint32>( _listEdge.size() );
            for ( ; linkIndex < listSorted.size() && listSorted[linkIndex]._from == clusterIndex; ++linkIndex )
            {
                const uint32 target = listSorted[linkIndex]._to;
                _listEdge.push_back( NavClusterEdge{ target, float3::getDistance( cluster._center, _listCluster[target]._center ) } );
            }
            cluster._edgeCount = static_cast<uint32>( _listEdge.size() ) - cluster._firstEdge;
        }
        if ( isWellFormed() == false )
        {
            SW_LOG_ERROR_ID( "NavClusterGraph.ClustersOutOfOrder", "Navmesh clusters are not in tile order or hold non-finite centers - the cluster graph is left empty" );
            clear();
            return false;
        }
        return true;
    }

    uint32 NavClusterGraph::findCluster( int32 tileX, int32 tileZ, const float3& position ) const
    {
        NavCluster key{};
        key._tileX       = tileX;
        key._tileZ       = tileZ;
        const auto first = std::lower_bound( _listCluster.begin(), _listCluster.end(), key, &NavClusterGraphInternal::isTileBefore );
        uint32     found = kNoCluster;
        float32    best  = MathUtil::kMaxFloat;
        // shortcut: 같은 타일의 덩어리는 중심이 가까운 것으로 고른다(다리 위 · 아래처럼 겹친 층은 높이까지 잰다). 폴리곤 소속표를 싣게 되면 그것으로 바꾼다.
        for ( auto it = first; it != _listCluster.end() && it->_tileX == tileX && it->_tileZ == tileZ; ++it )
        {
            const float32 distanceSquared = float3::getDistanceSquared( it->_center, position );
            if ( distanceSquared < best )
            {
                best  = distanceSquared;
                found = static_cast<uint32>( it - _listCluster.begin() );
            }
        }
        return found;
    }

    bool NavClusterGraph::findRoute( uint32 start, uint32 goal, vector<uint32>& outListRoute ) const
    {
        using Internal = NavClusterGraphInternal;
        outListRoute.clear();
        const uint32 clusterCount = static_cast<uint32>( _listCluster.size() );
        if ( start >= clusterCount || goal >= clusterCount )
            return false;
        if ( start == goal )
        {
            outListRoute.push_back( start );
            return true;
        }
        const float3&              goalCenter = _listCluster[goal]._center;
        vector<float32>            listCost( clusterCount, MathUtil::kMaxFloat );
        vector<uint32>             listParent( clusterCount, kNoCluster );
        vector<Internal::OpenNode> listOpen;
        listCost[start] = 0.0f;
        listOpen.push_back( Internal::OpenNode{ float3::getDistance( _listCluster[start]._center, goalCenter ), start } );
        const std::greater<Internal::OpenNode> compare{};
        while ( listOpen.empty() == false )
        {
            std::pop_heap( listOpen.begin(), listOpen.end(), compare );
            const Internal::OpenNode node = listOpen.back();
            listOpen.pop_back();
            if ( node._cluster == goal )
                break;
            const NavCluster& cluster = _listCluster[node._cluster];
            // 낡은 칸(더 싼 길로 다시 넣은 뒤 남은 것)은 건너뛴다.
            if ( node._estimate > listCost[node._cluster] + float3::getDistance( cluster._center, goalCenter ) + Internal::kStaleEstimateTolerance )
                continue;
            for ( uint32 edgeIndex = cluster._firstEdge; edgeIndex < cluster._firstEdge + cluster._edgeCount; ++edgeIndex )
            {
                const NavClusterEdge& edge = _listEdge[edgeIndex];
                const float32         cost = listCost[node._cluster] + edge._cost;
                if ( cost >= listCost[edge._target] )
                    continue;
                listCost[edge._target]   = cost;
                listParent[edge._target] = node._cluster;
                listOpen.push_back( Internal::OpenNode{ cost + float3::getDistance( _listCluster[edge._target]._center, goalCenter ), edge._target } );
                std::push_heap( listOpen.begin(), listOpen.end(), compare );
            }
        }
        if ( listParent[goal] == kNoCluster )
            return false;
        for ( uint32 cluster = goal; cluster != kNoCluster; cluster = listParent[cluster] )
        {
            outListRoute.push_back( cluster );
        }
        std::reverse( outListRoute.begin(), outListRoute.end() );
        return true;
    }

    void NavClusterGraph::write( ByteWriter& writer ) const
    {
        writer.writeUint32( static_cast<uint32>( _listCluster.size() ) );
        for ( const NavCluster& cluster : _listCluster )
        {
            writer.writePod( cluster );
        }
        writer.writeUint32( static_cast<uint32>( _listEdge.size() ) );
        for ( const NavClusterEdge& edge : _listEdge )
        {
            writer.writePod( edge );
        }
    }

    bool NavClusterGraph::read( ByteReader& reader )
    {
        clear();
        const bool bRead = reader.readCountedList( _listCluster, sizeof( NavCluster ), [&reader]( NavCluster& cluster ) -> bool
        {
            cluster = reader.readPod<NavCluster>();
            return reader.hasOverflowed() == false;
        } ) && reader.readCountedList( _listEdge, sizeof( NavClusterEdge ), [&reader]( NavClusterEdge& edge ) -> bool
        {
            edge = reader.readPod<NavClusterEdge>();
            return reader.hasOverflowed() == false;
        } );
        if ( bRead == false || isWellFormed() == false )
        {
            clear();
            return false;
        }
        return true;
    }

    bool NavClusterGraph::isWellFormed() const
    {
        using Internal         = NavClusterGraphInternal;
        const uint64 edgeCount = _listEdge.size();
        uint64       nextEdge  = 0;
        for ( size_t clusterIndex = 0; clusterIndex < _listCluster.size(); ++clusterIndex )
        {
            const NavCluster& cluster = _listCluster[clusterIndex];
            const bool        bOrder  = clusterIndex == 0 || Internal::isTileBefore( _listCluster[clusterIndex - 1], cluster ) ||
                                ( _listCluster[clusterIndex - 1]._tileX == cluster._tileX && _listCluster[clusterIndex - 1]._tileZ == cluster._tileZ );
            // 간선 묶음은 클러스터 순으로 빈틈없이 잇는다.
            const bool bEdges = cluster._firstEdge == nextEdge && static_cast<uint64>( cluster._edgeCount ) <= edgeCount - nextEdge;
            if ( bOrder == false || bEdges == false || cluster._center.isFinite() == false )
                return false;
            nextEdge += cluster._edgeCount;
        }
        if ( nextEdge != edgeCount )
            return false;
        for ( const NavClusterEdge& edge : _listEdge )
        {
            if ( edge._target >= _listCluster.size() || MathUtil::isFinite( edge._cost ) == false || edge._cost < 0.0f )
                return false;
        }
        return true;
    }
} // namespace sw
