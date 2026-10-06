#include "pch.h"

#include "Engine/Destruction/FractureGraph.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct FractureGraphInternal
        {
            static constexpr uint32 kKMeansIterations = 4;

            /** @brief 짓는 동안의 노드입니다. 잎이면 `_leaf` 가 옛 잎 번호입니다. */
            struct BuildNode
            {
                vector<uint32> _listChild;
                float3         _centroid{};
                float32        _volume{ 0.0f };
                uint32         _leaf{ 0xFFFFFFFFu };
            };

            /** @brief 합집합-찾기(경로 압축)입니다. */
            static uint32 findRoot( vector<uint32>& inoutListParent, uint32 item )
            {
                uint32 root = item;
                while ( inoutListParent[root] != root )
                    root = inoutListParent[root];
                while ( inoutListParent[item] != root )
                {
                    const uint32 next     = inoutListParent[item];
                    inoutListParent[item] = root;
                    item                  = next;
                }
                return root;
            }

            static void unite( vector<uint32>& inoutListParent, uint32 lhs, uint32 rhs )
            {
                const uint32 rootLhs = findRoot( inoutListParent, lhs );
                const uint32 rootRhs = findRoot( inoutListParent, rhs );
                if ( rootLhs == rootRhs )
                    return;
                // 작은 번호를 뿌리로 — 결과가 합치는 순서에 달리지 않는다.
                if ( rootLhs < rootRhs )
                    inoutListParent[rootRhs] = rootLhs;
                else
                    inoutListParent[rootLhs] = rootRhs;
            }

            /** @brief 자식들을 묶는 노드를 더하고 그 번호를 돌려줍니다(부피 가중 중심). */
            static uint32 makeCluster( vector<BuildNode>& inoutListBuild, vector<uint32> listChild )
            {
                BuildNode cluster;
                float3    sum{};
                float32   weight = 0.0f;
                for ( const uint32 child : listChild )
                {
                    const float32 mass = MathUtil::max( inoutListBuild[child]._volume, 1.0e-12f );
                    sum += inoutListBuild[child]._centroid * mass;
                    weight += mass;
                    cluster._volume += inoutListBuild[child]._volume;
                }
                cluster._centroid  = sum / weight;
                cluster._listChild = std::move( listChild );
                inoutListBuild.push_back( std::move( cluster ) );
                return static_cast<uint32>( inoutListBuild.size() - 1 );
            }

            static void collectLeaves( const vector<BuildNode>& listBuild, uint32 node, vector<uint32>& outListLeaf )
            {
                const BuildNode& build = listBuild[node];
                if ( build._listChild.empty() )
                {
                    outListLeaf.push_back( build._leaf );
                    return;
                }
                for ( const uint32 child : build._listChild )
                    collectLeaves( listBuild, child, outListLeaf );
            }

            /** @brief 노드들을 @p clusterCount 개로 나눕니다(가장 먼 점 고르기 + k-평균). 결과는 묶음마다 노드 목록이고 빈 묶음은 없습니다. */
            static void partitionNodes( const vector<BuildNode>& listBuild, const vector<uint32>& listCurrent, uint32 clusterCount, vector<vector<uint32>>& outListGroup )
            {
                const uint32 count = static_cast<uint32>( listCurrent.size() );
                float3       weightedCenter{};
                float32      totalVolume = 0.0f;
                for ( const uint32 node : listCurrent )
                {
                    weightedCenter += listBuild[node]._centroid * listBuild[node]._volume;
                    totalVolume += listBuild[node]._volume;
                }
                weightedCenter = totalVolume > 0.0f ? weightedCenter / totalVolume : listBuild[listCurrent[0]]._centroid;

                // 가장 먼 점 고르기 — 첫 씨앗은 무게 중심에 가장 가까운 노드.
                vector<float3>  listSite;
                vector<float32> listNearest( count, MathUtil::kMaxFloat );
                uint32          first     = 0;
                float32         firstDist = MathUtil::kMaxFloat;
                for ( uint32 index = 0; index < count; ++index )
                {
                    const float32 distance = float3::getDistanceSquared( listBuild[listCurrent[index]]._centroid, weightedCenter );
                    if ( distance < firstDist )
                    {
                        firstDist = distance;
                        first     = index;
                    }
                }
                uint32 pick = first;
                while ( static_cast<uint32>( listSite.size() ) < clusterCount )
                {
                    const float3 site = listBuild[listCurrent[pick]]._centroid;
                    listSite.push_back( site );
                    float32 farthest = -1.0f;
                    for ( uint32 index = 0; index < count; ++index )
                    {
                        listNearest[index] = MathUtil::min( listNearest[index], float3::getDistanceSquared( listBuild[listCurrent[index]]._centroid, site ) );
                        if ( listNearest[index] > farthest )
                        {
                            farthest = listNearest[index];
                            pick     = index;
                        }
                    }
                    if ( farthest <= 0.0f )
                        break;
                }

                vector<uint32> listAssign( count, 0 );
                for ( uint32 iteration = 0; iteration < kKMeansIterations; ++iteration )
                {
                    for ( uint32 index = 0; index < count; ++index )
                    {
                        float32 best = MathUtil::kMaxFloat;
                        for ( uint32 site = 0; site < static_cast<uint32>( listSite.size() ); ++site )
                        {
                            const float32 distance = float3::getDistanceSquared( listBuild[listCurrent[index]]._centroid, listSite[site] );
                            if ( distance < best )
                            {
                                best              = distance;
                                listAssign[index] = site;
                            }
                        }
                    }
                    vector<float3>  listSum( listSite.size(), float3{} );
                    vector<float32> listWeight( listSite.size(), 0.0f );
                    for ( uint32 index = 0; index < count; ++index )
                    {
                        const BuildNode& build = listBuild[listCurrent[index]];
                        const float32    mass  = MathUtil::max( build._volume, 1.0e-9f );
                        listSum[listAssign[index]] += build._centroid * mass;
                        listWeight[listAssign[index]] += mass;
                    }
                    for ( size_t site = 0; site < listSite.size(); ++site )
                    {
                        if ( listWeight[site] > 0.0f )
                            listSite[site] = listSum[site] / listWeight[site];
                    }
                }

                outListGroup.assign( listSite.size(), vector<uint32>{} );
                for ( uint32 index = 0; index < count; ++index )
                    outListGroup[listAssign[index]].push_back( listCurrent[index] );
                for ( size_t group = outListGroup.size(); group > 0; --group )
                {
                    if ( outListGroup[group - 1].empty() )
                        outListGroup.erase( outListGroup.begin() + static_cast<std::ptrdiff_t>( group - 1 ) );
                }
            }

            /** @brief 묶음마다 연결로 이어진 덩어리로 다시 나눕니다. */
            static void splitDisconnected( const vector<BuildNode>& listBuild, const vector<FractureLink>& listLink, uint32 leafCount, vector<vector<uint32>>& inoutListGroup )
            {
                // 옛 잎 → 지금 노드 번호(current 안의 자리 대신 빌드 노드 번호).
                vector<uint32> listOwner( leafCount, 0xFFFFFFFFu );
                vector<uint32> listGroupOf( listBuild.size(), 0xFFFFFFFFu );
                vector<uint32> listLeaf;
                for ( uint32 group = 0; group < static_cast<uint32>( inoutListGroup.size() ); ++group )
                {
                    for ( const uint32 node : inoutListGroup[group] )
                    {
                        listGroupOf[node] = group;
                        listLeaf.clear();
                        collectLeaves( listBuild, node, listLeaf );
                        for ( const uint32 leaf : listLeaf )
                            listOwner[leaf] = node;
                    }
                }
                vector<uint32> listParent( listBuild.size() );
                for ( uint32 index = 0; index < static_cast<uint32>( listParent.size() ); ++index )
                    listParent[index] = index;
                for ( const FractureLink& link : listLink )
                {
                    const uint32 ownerA = listOwner[link._leafA];
                    const uint32 ownerB = listOwner[link._leafB];
                    if ( ownerA == 0xFFFFFFFFu || ownerB == 0xFFFFFFFFu || ownerA == ownerB )
                        continue;
                    if ( listGroupOf[ownerA] == listGroupOf[ownerB] )
                        unite( listParent, ownerA, ownerB );
                }
                vector<vector<uint32>> listSplit;
                for ( const vector<uint32>& group : inoutListGroup )
                {
                    vector<uint32>         listRootSeen;
                    vector<vector<uint32>> listPart;
                    for ( const uint32 node : group )
                    {
                        const uint32 root = findRoot( listParent, node );
                        size_t       part = 0;
                        while ( part < listRootSeen.size() && listRootSeen[part] != root )
                            ++part;
                        if ( part == listRootSeen.size() )
                        {
                            listRootSeen.push_back( root );
                            listPart.push_back( vector<uint32>{} );
                        }
                        listPart[part].push_back( node );
                    }
                    for ( vector<uint32>& part : listPart )
                        listSplit.push_back( std::move( part ) );
                }
                inoutListGroup = std::move( listSplit );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    vector_reference<const uint32> FractureGraph::getChildren( uint32 node ) const
    {
        if ( node >= _listNode.size() || _listNode[node]._childCount == 0 )
            return {};
        return vector_reference<const uint32>{ _listChildNode.data() + _listNode[node]._firstChild, _listNode[node]._childCount };
    }

    uint32 FractureGraph::getDepthCount() const
    {
        uint32 maxDepth = 0;
        for ( const FractureNode& node : _listNode )
            maxDepth = MathUtil::max( maxDepth, static_cast<uint32>( node._depth ) );
        return _listNode.empty() ? 0u : maxDepth + 1;
    }

    bool FractureGraph::isValid( string* pOutError ) const
    {
        const auto fail = [pOutError]( const utf8* pReason )
        {
            if ( pOutError != nullptr )
                *pOutError = pReason;
            return false;
        };
        if ( _leafCount == 0 || _listNode.size() < _leafCount )
            return fail( "no leaves" );
        const uint32 root = getRootNode();
        if ( _listNode[root]._parent != -1 || _listNode[root]._firstLeaf != 0 || _listNode[root]._leafCount != _leafCount )
            return fail( "root must hold every leaf" );
        for ( uint32 nodeIndex = 0; nodeIndex < static_cast<uint32>( _listNode.size() ); ++nodeIndex )
        {
            const FractureNode& node      = _listNode[nodeIndex];
            const bool          bLeafSlot = nodeIndex < _leafCount;
            if ( bLeafSlot != node.isLeaf() )
                return fail( "leaves must be the first nodes" );
            if ( bLeafSlot && ( node._firstLeaf != nodeIndex || node._leafCount != 1 ) )
                return fail( "a leaf must hold itself" );
            if ( node._firstLeaf + node._leafCount > _leafCount )
                return fail( "leaf range out of bounds" );
            if ( nodeIndex != root && ( node._parent < 0 || static_cast<uint32>( node._parent ) <= nodeIndex || static_cast<uint32>( node._parent ) >= _listNode.size() ) )
                return fail( "a parent must come after its child" );
            if ( node._firstChild + node._childCount > _listChildNode.size() )
                return fail( "child range out of bounds" );
            uint32 leafSum = 0;
            for ( uint32 offset = 0; offset < node._childCount; ++offset )
            {
                const uint32 child = _listChildNode[node._firstChild + offset];
                if ( child >= _listNode.size() || _listNode[child]._parent != static_cast<int32>( nodeIndex ) )
                    return fail( "child does not point back at its parent" );
                if ( _listNode[child]._firstLeaf != node._firstLeaf + leafSum )
                    return fail( "children must cover the leaf range in order" );
                leafSum += _listNode[child]._leafCount;
            }
            if ( node.isLeaf() == false && leafSum != node._leafCount )
                return fail( "children must cover the whole leaf range" );
        }
        for ( const FractureLink& link : _listLink )
        {
            if ( link._leafA >= link._leafB || link._leafB >= _leafCount || link._area < 0.0f )
                return fail( "bad link" );
        }
        return true;
    }

    void FractureGraph::clear()
    {
        _listNode.clear();
        _listChildNode.clear();
        _listLink.clear();
        _leafCount = 0;
    }

    void FractureGraphUtil::normalizeLinks( vector<FractureLink>& inoutListLink )
    {
        for ( FractureLink& link : inoutListLink )
        {
            if ( link._leafB < link._leafA )
            {
                const uint32 swapped = link._leafA;
                link._leafA          = link._leafB;
                link._leafB          = swapped;
            }
        }
        std::sort( inoutListLink.begin(), inoutListLink.end(), []( const FractureLink& lhs, const FractureLink& rhs )
        {
            return lhs._leafA < rhs._leafA || ( lhs._leafA == rhs._leafA && lhs._leafB < rhs._leafB );
        } );
        vector<FractureLink> listMerged;
        listMerged.reserve( inoutListLink.size() );
        for ( const FractureLink& link : inoutListLink )
        {
            if ( link._leafA == link._leafB )
                continue;
            if ( listMerged.empty() == false && listMerged.back()._leafA == link._leafA && listMerged.back()._leafB == link._leafB )
                listMerged.back()._area += link._area;
            else
                listMerged.push_back( link );
        }
        inoutListLink = std::move( listMerged );
    }

    void FractureGraphUtil::buildHierarchy( FractureGraph& inoutGraph, vector_reference<const uint32> listLevelCount, vector<uint32>& outListLeafOrder )
    {
        using BuildNode        = FractureGraphInternal::BuildNode;
        const uint32 leafCount = inoutGraph._leafCount;
        outListLeafOrder.clear();
        if ( leafCount == 0 )
            return;

        vector<BuildNode> listBuild( leafCount );
        for ( uint32 leaf = 0; leaf < leafCount; ++leaf )
        {
            listBuild[leaf]._centroid = inoutGraph._listNode[leaf]._centroid;
            listBuild[leaf]._volume   = inoutGraph._listNode[leaf]._volume;
            listBuild[leaf]._leaf     = leaf;
        }
        const vector<FractureNode> listLeafNode( inoutGraph._listNode.begin(), inoutGraph._listNode.begin() + static_cast<std::ptrdiff_t>( leafCount ) );

        vector<uint32> listCurrent( leafCount );
        for ( uint32 leaf = 0; leaf < leafCount; ++leaf )
            listCurrent[leaf] = leaf;

        for ( size_t level = listLevelCount.size(); level > 0; --level )
        {
            const uint32 clusterCount = listLevelCount[level - 1];
            if ( clusterCount <= 1 || clusterCount >= listCurrent.size() )
                continue;
            vector<vector<uint32>> listGroup;
            FractureGraphInternal::partitionNodes( listBuild, listCurrent, clusterCount, listGroup );
            FractureGraphInternal::splitDisconnected( listBuild, inoutGraph._listLink, leafCount, listGroup );
            vector<uint32> listNext;
            for ( vector<uint32>& group : listGroup )
            {
                if ( group.size() == 1 )
                    listNext.push_back( group[0] );
                else
                    listNext.push_back( FractureGraphInternal::makeCluster( listBuild, std::move( group ) ) );
            }
            listCurrent = std::move( listNext );
        }
        const uint32 buildRoot = listCurrent.size() == 1 ? listCurrent[0] : FractureGraphInternal::makeCluster( listBuild, listCurrent );

        // 깊이 우선 — 잎 순서와 묶음의 뒤 순서(자식이 먼저 → 뿌리가 마지막)를 함께 매긴다.
        vector<uint32> listFinalOf( listBuild.size(), 0xFFFFFFFFu );
        vector<uint32> listClusterOrder;
        {
            struct Frame
            {
                uint32 _node;
                uint32 _nextChild;
            };
            vector<Frame> listStack;
            listStack.push_back( Frame{ buildRoot, 0 } );
            while ( listStack.empty() == false )
            {
                Frame&           frame = listStack.back();
                const BuildNode& build = listBuild[frame._node];
                if ( build._listChild.empty() )
                {
                    listFinalOf[frame._node] = static_cast<uint32>( outListLeafOrder.size() );
                    outListLeafOrder.push_back( build._leaf );
                    listStack.pop_back();
                    continue;
                }
                if ( frame._nextChild < build._listChild.size() )
                {
                    const uint32 child = build._listChild[frame._nextChild];
                    ++frame._nextChild;
                    listStack.push_back( Frame{ child, 0 } );
                    continue;
                }
                listClusterOrder.push_back( frame._node );
                listStack.pop_back();
            }
        }
        for ( uint32 order = 0; order < static_cast<uint32>( listClusterOrder.size() ); ++order )
            listFinalOf[listClusterOrder[order]] = leafCount + order;

        const uint32  nodeCount = leafCount + static_cast<uint32>( listClusterOrder.size() );
        FractureGraph result;
        result._leafCount = leafCount;
        result._listNode.assign( nodeCount, FractureNode{} );
        for ( uint32 leaf = 0; leaf < leafCount; ++leaf )
        {
            FractureNode& node = result._listNode[leaf];
            node               = listLeafNode[outListLeafOrder[leaf]];
            node._firstLeaf    = leaf;
            node._leafCount    = 1;
            node._firstChild   = 0;
            node._childCount   = 0;
            node._parent       = -1;
        }
        for ( const uint32 buildIndex : listClusterOrder )
        {
            const BuildNode& build = listBuild[buildIndex];
            FractureNode&    node  = result._listNode[listFinalOf[buildIndex]];
            node._centroid         = build._centroid;
            node._volume           = build._volume;
            node._firstChild       = static_cast<uint32>( result._listChildNode.size() );
            node._childCount       = static_cast<uint32>( build._listChild.size() );
            for ( const uint32 child : build._listChild )
            {
                result._listChildNode.push_back( listFinalOf[child] );
                result._listNode[listFinalOf[child]]._parent = static_cast<int32>( listFinalOf[buildIndex] );
            }
        }
        // 잎 구간 · 깊이 — 묶음은 자식 뒤에 있으므로 앞에서부터 구간을, 뒤에서부터 깊이를 채운다.
        for ( uint32 nodeIndex = leafCount; nodeIndex < nodeCount; ++nodeIndex )
        {
            FractureNode& node   = result._listNode[nodeIndex];
            uint32        first  = 0xFFFFFFFFu;
            uint32        summed = 0;
            for ( uint32 offset = 0; offset < node._childCount; ++offset )
            {
                const FractureNode& child = result._listNode[result._listChildNode[node._firstChild + offset]];
                first                     = MathUtil::min( first, child._firstLeaf );
                summed += child._leafCount;
            }
            node._firstLeaf = first;
            node._leafCount = summed;
        }
        result._listNode[nodeCount - 1]._depth = 0;
        for ( uint32 nodeIndex = nodeCount; nodeIndex > 0; --nodeIndex )
        {
            FractureNode& node = result._listNode[nodeIndex - 1];
            if ( node._parent >= 0 )
                node._depth = static_cast<uint8>( result._listNode[static_cast<uint32>( node._parent )]._depth + 1 );
        }

        vector<uint32> listNewOfOld( leafCount, 0 );
        for ( uint32 leaf = 0; leaf < leafCount; ++leaf )
            listNewOfOld[outListLeafOrder[leaf]] = leaf;
        result._listLink = inoutGraph._listLink;
        for ( FractureLink& link : result._listLink )
        {
            link._leafA = listNewOfOld[link._leafA];
            link._leafB = listNewOfOld[link._leafB];
        }
        normalizeLinks( result._listLink );
        inoutGraph = std::move( result );
    }
} // namespace sw
