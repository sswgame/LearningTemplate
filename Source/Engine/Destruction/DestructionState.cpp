#include "pch.h"

#include "Engine/Destruction/DestructionState.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Destruction/FractureGraph.h"

namespace sw
{
    namespace
    {
        struct DestructionStateInternal
        {
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
                if ( rootLhs < rootRhs )
                    inoutListParent[rootRhs] = rootLhs;
                else
                    inoutListParent[rootLhs] = rootRhs;
            }

            static uint64 mixHash( uint64 hash, uint64 value )
            {
                hash ^= value + 0x9E3779B97F4A7C15ull + ( hash << 6 ) + ( hash >> 2 );
                hash = ( hash ^ ( hash >> 31 ) ) * 0xBF58476D1CE4E5B9ull;
                return hash;
            }

            static uint64 toBits( float32 value )
            {
                uint32 bits = 0;
                Memory::copy( &bits, &value, sizeof( bits ) );
                return bits;
            }

            /** @brief 오름차순 목록에서 값의 자리입니다. 없으면 -1 입니다. */
            static int32 findSorted( const vector<uint32>& listSorted, uint32 value )
            {
                const auto iter = std::lower_bound( listSorted.begin(), listSorted.end(), value );
                if ( iter == listSorted.end() || *iter != value )
                    return -1;
                return static_cast<int32>( iter - listSorted.begin() );
            }

            static void addUnique( vector<uint32>& inoutList, uint32 value )
            {
                for ( const uint32 existing : inoutList )
                {
                    if ( existing == value )
                        return;
                }
                inoutList.push_back( value );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    DestructionState::DestructionState()
        : _pGraph{ nullptr }
        , _profile{}
        , _listInitialAnchor{}
        , _listNodeStrain{}
        , _listNodeBroken{}
        , _listLeafActive{}
        , _listLeafGroup{}
        , _listLeafAnchored{}
        , _listLinkStrain{}
        , _listLinkLoad{}
        , _listLinkBroken{}
        , _listLeafLinkStart{}
        , _listLeafLinkIndex{}
        , _listGroup{}
        , _nextGroupId{ 1 }
    {
    }

    void DestructionState::initialize( const FractureGraph& graph, const DestructionProfile& profile, vector_reference<const uint8> listAnchoredLeaf )
    {
        _pGraph  = &graph;
        _profile = profile;
        _listInitialAnchor.assign( graph._leafCount, SW_FALSE );
        for ( size_t leaf = 0; leaf < listAnchoredLeaf.size() && leaf < graph._leafCount; ++leaf )
            _listInitialAnchor[leaf] = listAnchoredLeaf[leaf] != 0 ? SW_TRUE : SW_FALSE;

        // 잎 → 연결 표(CSR).
        _listLeafLinkStart.assign( graph._leafCount + 1, 0 );
        for ( const FractureLink& link : graph._listLink )
        {
            ++_listLeafLinkStart[link._leafA + 1];
            ++_listLeafLinkStart[link._leafB + 1];
        }
        for ( uint32 leaf = 0; leaf < graph._leafCount; ++leaf )
            _listLeafLinkStart[leaf + 1] += _listLeafLinkStart[leaf];
        _listLeafLinkIndex.assign( graph._listLink.size() * 2, 0 );
        vector<uint32> listCursor( _listLeafLinkStart.begin(), _listLeafLinkStart.end() - 1 );
        for ( uint32 link = 0; link < static_cast<uint32>( graph._listLink.size() ); ++link )
        {
            _listLeafLinkIndex[listCursor[graph._listLink[link]._leafA]++] = link;
            _listLeafLinkIndex[listCursor[graph._listLink[link]._leafB]++] = link;
        }
        reset();
    }

    void DestructionState::reset()
    {
        if ( _pGraph == nullptr )
            return;
        const FractureGraph& graph = *_pGraph;
        const uint32         root  = graph.getRootNode();
        _listNodeStrain.assign( graph._listNode.size(), 0.0f );
        _listNodeBroken.assign( graph._listNode.size(), SW_FALSE );
        _listLeafActive.assign( graph._leafCount, root );
        _listLeafGroup.assign( graph._leafCount, 1 );
        _listLeafAnchored = _listInitialAnchor;
        _listLinkStrain.assign( graph._listLink.size(), 0.0f );
        _listLinkLoad.assign( graph._listLink.size(), 0.0f );
        _listLinkBroken.assign( graph._listLink.size(), SW_FALSE );
        _listGroup.clear();
        DestructionGroup group;
        group._listNode.push_back( root );
        group._id        = 1;
        group._parentId  = 0;
        group._leafCount = graph._leafCount;
        for ( const uint8 bAnchored : _listLeafAnchored )
            group._bAnchored = ( group._bAnchored == SW_TRUE || bAnchored != SW_FALSE ) ? SW_TRUE : SW_FALSE;
        _listGroup.push_back( std::move( group ) );
        _nextGroupId = 2;
    }

    vector_reference<const uint32> DestructionState::getLeafLinks( uint32 leaf ) const
    {
        return vector_reference<const uint32>{ _listLeafLinkIndex.data() + _listLeafLinkStart[leaf], _listLeafLinkStart[leaf + 1] - _listLeafLinkStart[leaf] };
    }

    const DestructionGroup* DestructionState::findGroup( uint32 groupId ) const
    {
        const int32 index = findGroupIndex( groupId );
        return index >= 0 ? &_listGroup[static_cast<size_t>( index )] : nullptr;
    }

    int32 DestructionState::findGroupIndex( uint32 groupId ) const
    {
        size_t low  = 0;
        size_t high = _listGroup.size();
        while ( low < high )
        {
            const size_t middle = ( low + high ) / 2;
            if ( _listGroup[middle]._id < groupId )
                low = middle + 1;
            else
                high = middle;
        }
        return ( low < _listGroup.size() && _listGroup[low]._id == groupId ) ? static_cast<int32>( low ) : -1;
    }

    bool DestructionState::hasAnchoredLeaf( uint32 node ) const
    {
        const FractureNode& data = _pGraph->_listNode[node];
        for ( uint32 leaf = data._firstLeaf; leaf < data._firstLeaf + data._leafCount; ++leaf )
        {
            if ( _listLeafAnchored[leaf] != SW_FALSE )
                return true;
        }
        return false;
    }

    void DestructionState::openNode( uint32 node, DestructionChange& outChange )
    {
        const FractureGraph& graph = *_pGraph;
        _listNodeBroken[node]      = SW_TRUE;
        ++outChange._brokenNodeCount;
        for ( const uint32 child : graph.getChildren( node ) )
        {
            const FractureNode& childData = graph._listNode[child];
            for ( uint32 leaf = childData._firstLeaf; leaf < childData._firstLeaf + childData._leafCount; ++leaf )
                _listLeafActive[leaf] = child;
        }
        // 그 그룹의 노드 목록에서 갈린 노드를 자식들로 바꾼다.
        const int32 groupIndex = findGroupIndex( _listLeafGroup[graph._listNode[node]._firstLeaf] );
        if ( groupIndex < 0 )
            return;
        vector<uint32>& listNode = _listGroup[static_cast<size_t>( groupIndex )]._listNode;
        const auto      iter     = std::lower_bound( listNode.begin(), listNode.end(), node );
        if ( iter != listNode.end() && *iter == node )
            listNode.erase( iter );
        for ( const uint32 child : graph.getChildren( node ) )
            listNode.insert( std::lower_bound( listNode.begin(), listNode.end(), child ), child );
    }

    void DestructionState::severLeaf( uint32 leaf, DestructionChange& outChange )
    {
        for ( const uint32 link : getLeafLinks( leaf ) )
        {
            if ( _listLinkBroken[link] != SW_FALSE )
                continue;
            _listLinkBroken[link] = SW_TRUE;
            ++outChange._brokenLinkCount;
        }
        _listLeafAnchored[leaf] = SW_FALSE;
    }

    bool DestructionState::breakNode( uint32 node, DestructionChange& outChange )
    {
        if ( _pGraph == nullptr || node >= _pGraph->_listNode.size() )
            return false;
        const FractureNode& data    = _pGraph->_listNode[node];
        const bool          bActive = _listLeafActive[data._firstLeaf] == node;
        if ( data.isLeaf() || bActive == false )
            return false;
        vector<uint32> listDirty{ _listLeafGroup[data._firstLeaf] };
        openNode( node, outChange );
        regroup( listDirty, outChange );
        return true;
    }

    bool DestructionState::detachLeaf( uint32 leaf, DestructionChange& outChange )
    {
        if ( _pGraph == nullptr || leaf >= _pGraph->_leafCount )
            return false;
        vector<uint32> listDirty{ _listLeafGroup[leaf] };
        // 그 잎이 활성이 될 때까지 위에서부터 가른다.
        while ( _listLeafActive[leaf] != leaf )
            openNode( _listLeafActive[leaf], outChange );
        severLeaf( leaf, outChange );
        regroup( listDirty, outChange );
        return true;
    }

    bool DestructionState::breakLink( uint32 link, DestructionChange& outChange )
    {
        if ( _pGraph == nullptr || link >= _pGraph->_listLink.size() || _listLinkBroken[link] != SW_FALSE )
            return false;
        const FractureLink& data = _pGraph->_listLink[link];
        if ( _listLeafActive[data._leafA] == _listLeafActive[data._leafB] )
            return false;
        _listLinkBroken[link] = SW_TRUE;
        ++outChange._brokenLinkCount;
        vector<uint32> listDirty{ _listLeafGroup[data._leafA] };
        DestructionStateInternal::addUnique( listDirty, _listLeafGroup[data._leafB] );
        regroup( listDirty, outChange );
        return true;
    }

    void DestructionState::splitComponents( const vector<uint32>& listNode, vector<vector<uint32>>& outListComponent ) const
    {
        outListComponent.clear();
        const FractureGraph& graph = *_pGraph;
        vector<uint32>       listParent( listNode.size() );
        for ( uint32 index = 0; index < static_cast<uint32>( listNode.size() ); ++index )
            listParent[index] = index;
        for ( uint32 index = 0; index < static_cast<uint32>( listNode.size() ); ++index )
        {
            const FractureNode& data = graph._listNode[listNode[index]];
            for ( uint32 leaf = data._firstLeaf; leaf < data._firstLeaf + data._leafCount; ++leaf )
            {
                for ( const uint32 link : getLeafLinks( leaf ) )
                {
                    if ( _listLinkBroken[link] != SW_FALSE )
                        continue;
                    const FractureLink& linkData = graph._listLink[link];
                    const uint32        other    = linkData._leafA == leaf ? linkData._leafB : linkData._leafA;
                    const int32         otherAt  = DestructionStateInternal::findSorted( listNode, _listLeafActive[other] );
                    if ( otherAt >= 0 && static_cast<uint32>( otherAt ) != index )
                        DestructionStateInternal::unite( listParent, index, static_cast<uint32>( otherAt ) );
                }
            }
        }
        // 덩어리 순서 = 가장 작은 노드 순(노드 목록이 오름차순이므로 뿌리 = 가장 작은 자리).
        vector<int32> listComponentOfRoot( listNode.size(), -1 );
        for ( uint32 index = 0; index < static_cast<uint32>( listNode.size() ); ++index )
        {
            const uint32 root = DestructionStateInternal::findRoot( listParent, index );
            if ( listComponentOfRoot[root] < 0 )
            {
                listComponentOfRoot[root] = static_cast<int32>( outListComponent.size() );
                outListComponent.push_back( vector<uint32>{} );
            }
            outListComponent[static_cast<size_t>( listComponentOfRoot[root] )].push_back( listNode[index] );
        }
    }

    bool DestructionState::relieveOverload( const vector<uint32>& listNode, DestructionChange& outChange )
    {
        const FractureGraph& graph     = *_pGraph;
        const uint32         nodeCount = static_cast<uint32>( listNode.size() );
        // 이웃 쌍(작은 자리 → 큰 자리)마다 맞닿은 넓이와 그 사이 연결들.
        struct Neighbor
        {
            uint32  _other;
            float32 _capacity;
            uint32  _pairIndex;
        };
        vector<vector<Neighbor>> listNeighbor( nodeCount );
        vector<vector<uint32>>   listPairLink;
        vector<float32>          listPairCapacity;
        for ( uint32 index = 0; index < nodeCount; ++index )
        {
            const FractureNode& data = graph._listNode[listNode[index]];
            for ( uint32 leaf = data._firstLeaf; leaf < data._firstLeaf + data._leafCount; ++leaf )
            {
                for ( const uint32 link : getLeafLinks( leaf ) )
                {
                    const FractureLink& linkData = graph._listLink[link];
                    if ( _listLinkBroken[link] != SW_FALSE || linkData._leafA != leaf )
                        continue;
                    const int32 otherAt = DestructionStateInternal::findSorted( listNode, _listLeafActive[linkData._leafB] );
                    if ( otherAt < 0 || static_cast<uint32>( otherAt ) == index )
                        continue;
                    const uint32 other = static_cast<uint32>( otherAt );
                    uint32       pair  = 0xFFFFFFFFu;
                    for ( const Neighbor& neighbor : listNeighbor[index] )
                    {
                        if ( neighbor._other == other )
                            pair = neighbor._pairIndex;
                    }
                    if ( pair == 0xFFFFFFFFu )
                    {
                        pair = static_cast<uint32>( listPairLink.size() );
                        listPairLink.push_back( vector<uint32>{} );
                        listPairCapacity.push_back( 0.0f );
                        listNeighbor[index].push_back( Neighbor{ other, 0.0f, pair } );
                        listNeighbor[other].push_back( Neighbor{ index, 0.0f, pair } );
                    }
                    listPairLink[pair].push_back( link );
                    listPairCapacity[pair] += linkData._area * _profile._supportStrength;
                }
            }
        }
        for ( vector<Neighbor>& list : listNeighbor )
        {
            for ( Neighbor& neighbor : list )
                neighbor._capacity = listPairCapacity[neighbor._pairIndex];
            std::sort( list.begin(), list.end(), []( const Neighbor& lhs, const Neighbor& rhs )
            { return lhs._other < rhs._other; } );
        }

        // 앵커에서 너비 우선 — 깊이(앵커까지 몇 단계).
        vector<uint32> listDepth( nodeCount, 0xFFFFFFFFu );
        vector<uint32> listQueue;
        for ( uint32 index = 0; index < nodeCount; ++index )
        {
            if ( hasAnchoredLeaf( listNode[index] ) )
            {
                listDepth[index] = 0;
                listQueue.push_back( index );
            }
        }
        for ( size_t cursor = 0; cursor < listQueue.size(); ++cursor )
        {
            const uint32 current = listQueue[cursor];
            for ( const Neighbor& neighbor : listNeighbor[current] )
            {
                if ( listDepth[neighbor._other] != 0xFFFFFFFFu )
                    continue;
                listDepth[neighbor._other] = listDepth[current] + 1;
                listQueue.push_back( neighbor._other );
            }
        }

        // 깊은 노드부터 — 제 무게 + 받은 하중을 더 얕은 이웃에게 맞닿은 세기 비율로 나눠 넘긴다. 몫이 세기를 넘으면 그 사이가 끊긴다.
        vector<float32> listIncoming( nodeCount, 0.0f );
        bool            bBroke = false;
        for ( size_t order = listQueue.size(); order > 0; --order )
        {
            const uint32 current = listQueue[order - 1];
            if ( listDepth[current] == 0 )
                continue;
            const float32 load          = graph._listNode[listNode[current]]._volume * _profile._density * kGravity + listIncoming[current];
            float32       totalCapacity = 0.0f;
            for ( const Neighbor& neighbor : listNeighbor[current] )
            {
                if ( listDepth[neighbor._other] < listDepth[current] )
                    totalCapacity += neighbor._capacity;
            }
            if ( totalCapacity <= 0.0f )
                continue;
            for ( const Neighbor& neighbor : listNeighbor[current] )
            {
                if ( listDepth[neighbor._other] >= listDepth[current] )
                    continue;
                const float32 share = load * neighbor._capacity / totalCapacity;
                listIncoming[neighbor._other] += share;
                for ( const uint32 link : listPairLink[neighbor._pairIndex] )
                    _listLinkLoad[link] = share;
                if ( share <= neighbor._capacity )
                    continue;
                for ( const uint32 link : listPairLink[neighbor._pairIndex] )
                {
                    if ( _listLinkBroken[link] != SW_FALSE )
                        continue;
                    _listLinkBroken[link] = SW_TRUE;
                    ++outChange._brokenLinkCount;
                    ++outChange._overloadedLinkCount;
                }
                bBroke = true;
            }
        }
        return bBroke;
    }

    void DestructionState::regroup( vector<uint32>& inoutListDirtyGroupId, DestructionChange& outChange )
    {
        std::sort( inoutListDirtyGroupId.begin(), inoutListDirtyGroupId.end() );
        vector<DestructionGroup> listNewGroup;
        for ( const uint32 groupId : inoutListDirtyGroupId )
        {
            const int32 groupIndex = findGroupIndex( groupId );
            if ( groupIndex < 0 )
                continue;
            const DestructionGroup old = _listGroup[static_cast<size_t>( groupIndex )];
            vector<vector<uint32>> listComponent;
            // 붙은 덩어리는 무게로 끊길 수 있다 — 끊기면 다시 나눠 다시 잰다.
            for ( uint32 pass = 0; pass < kMaxSupportPass; ++pass )
            {
                splitComponents( old._listNode, listComponent );
                bool bBroke = false;
                for ( const vector<uint32>& component : listComponent )
                {
                    bool bAnchored = false;
                    for ( const uint32 node : component )
                        bAnchored = bAnchored || hasAnchoredLeaf( node );
                    if ( bAnchored && relieveOverload( component, outChange ) )
                        bBroke = true;
                }
                if ( bBroke == false )
                    break;
            }

            vector<DestructionGroup> listPart;
            for ( vector<uint32>& component : listComponent )
            {
                DestructionGroup part;
                part._listNode = std::move( component );
                part._parentId = old._id;
                for ( const uint32 node : part._listNode )
                {
                    part._leafCount += _pGraph->_listNode[node]._leafCount;
                    if ( hasAnchoredLeaf( node ) )
                        part._bAnchored = SW_TRUE;
                }
                listPart.push_back( std::move( part ) );
            }
            // 한 덩어리 그대로면(묶음이 갈라졌을 뿐) 번호를 지킨다 — 바디도 그림도 바뀌지 않는다.
            if ( listPart.size() == 1 && listPart[0]._bAnchored == old._bAnchored )
            {
                DestructionGroup& kept = _listGroup[static_cast<size_t>( groupIndex )];
                kept._listNode         = std::move( listPart[0]._listNode );
                continue;
            }
            outChange._listRemovedGroup.push_back( old._id );
            _listGroup.erase( _listGroup.begin() + groupIndex );
            for ( DestructionGroup& part : listPart )
                listNewGroup.push_back( std::move( part ) );
        }
        for ( DestructionGroup& group : listNewGroup )
        {
            group._id = _nextGroupId++;
            for ( const uint32 node : group._listNode )
            {
                const FractureNode& data = _pGraph->_listNode[node];
                for ( uint32 leaf = data._firstLeaf; leaf < data._firstLeaf + data._leafCount; ++leaf )
                    _listLeafGroup[leaf] = group._id;
            }
            outChange._listCreatedGroup.push_back( group._id );
            _listGroup.push_back( std::move( group ) );
        }
    }

    float32 DestructionState::computeGroupMass( const DestructionGroup& group, float3& outCenter ) const
    {
        float32 volume = 0.0f;
        float3  moment{};
        for ( const uint32 node : group._listNode )
        {
            const FractureNode& data = _pGraph->_listNode[node];
            volume += data._volume;
            moment += data._centroid * data._volume;
        }
        outCenter = volume > 0.0f ? moment / volume : float3{};
        return volume * _profile._density;
    }

    uint64 DestructionState::computeStateHash() const
    {
        using Internal = DestructionStateInternal;
        uint64 hash    = 1469598103934665603ull;
        for ( size_t node = 0; node < _listNodeStrain.size(); ++node )
        {
            hash = Internal::mixHash( hash, Internal::toBits( _listNodeStrain[node] ) );
            hash = Internal::mixHash( hash, _listNodeBroken[node] );
        }
        for ( size_t link = 0; link < _listLinkStrain.size(); ++link )
        {
            hash = Internal::mixHash( hash, Internal::toBits( _listLinkStrain[link] ) );
            hash = Internal::mixHash( hash, _listLinkBroken[link] );
        }
        for ( const uint8 bAnchored : _listLeafAnchored )
            hash = Internal::mixHash( hash, bAnchored );
        for ( const DestructionGroup& group : _listGroup )
        {
            hash = Internal::mixHash( hash, group._id );
            hash = Internal::mixHash( hash, group._bAnchored );
            for ( const uint32 node : group._listNode )
                hash = Internal::mixHash( hash, node );
        }
        return hash;
    }
} // namespace sw
