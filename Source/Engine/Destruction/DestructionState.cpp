#include "pch.h"

#include "Engine/Destruction/DestructionState.h"

#include "Core/Container/UnionFindUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Foundation/Defines.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/FourCcUtil.h"

#include "Engine/Destruction/DestructionDamage.h"
#include "Engine/Destruction/Fracture/FractureGraph.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Physics/PhysicsSystem.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct DestructionStateInternal
        {
            static constexpr uint32 kMagic        = FourCcUtil::make( "SWDS" );
            static constexpr uint32 kVersion      = 1;
            static constexpr uint32 kNoActiveNode = invalid_index::kUint32;

            /** @brief 0 또는 1 인 바이트 목록을 비트로 씁니다. */
            static void writeFlagBits( BitWriter& writer, const vector<uint8>& listFlag )
            {
                for ( const uint8 flag : listFlag )
                {
                    writer.writeBits( flag != 0 ? 1u : 0u, 1 );
                }
            }

            static void readFlagBits( BitReader& reader, vector<uint8>& outListFlag, size_t count )
            {
                outListFlag.assign( count, SW_FALSE );
                for ( size_t index = 0; index < count; ++index )
                {
                    outListFlag[index] = reader.readBits( 1 ) != 0 ? SW_TRUE : SW_FALSE;
                }
            }

            /** @brief 0 이 아닌 값만 (앞 인덱스와의 차이, 비트 그대로) 형태로 씁니다. */
            static void writeSparseFloats( BitWriter& writer, const vector<float32>& listValue )
            {
                uint64 count = 0;
                for ( const float32 value : listValue )
                {
                    count += value != 0.0f ? 1u : 0u;
                }
                writer.writeVarUint( count );
                size_t previous = 0;
                for ( size_t index = 0; index < listValue.size(); ++index )
                {
                    if ( listValue[index] == 0.0f )
                        continue;
                    writer.writeVarUint( index - previous );
                    writer.writeFloat( listValue[index] );
                    previous = index;
                }
            }

            [[nodiscard]] static bool readSparseFloats( BitReader& reader, vector<float32>& outListValue, size_t count )
            {
                outListValue.assign( count, 0.0f );
                const uint64 valueCount = reader.readVarUint();
                if ( valueCount > count )
                    return false;
                uint64 index = 0;
                for ( uint64 entry = 0; entry < valueCount; ++entry )
                {
                    // 간격은 남은 칸 안이어야 한다 — 더해 보고 비교하면 큰 간격이 uint64 를 감아 앞 칸으로 돌아온다. 첫 칸 뒤의 간격 0 은 같은 칸을 두 번 쓴 것이다.
                    const uint64 step       = reader.readVarUint();
                    const bool   bValidStep = ( entry == 0 || step > 0 ) && step < static_cast<uint64>( count ) - index;
                    if ( bValidStep == false )
                        return false;
                    index += step;
                    float32 value{ 0.0f };
                    if ( reader.readFiniteFloat( value ) == false )
                        return false;
                    outListValue[static_cast<size_t>( index )] = value;
                }
                return reader.hasOverflowed() == false;
            }

            static uint64 mixHash( uint64 hash, uint64 value )
            {
                hash = HashUtil::combine( hash, value );
                hash = ( hash ^ ( hash >> 31 ) ) * HashUtil::kSplitMixMultiplier0;
                return hash;
            }

            /** @brief 오름차순 목록에서 값의 인덱스를 반환합니다. 없으면 -1 입니다. */
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

            /** @brief 노드와 그 노드에 이번 사건이 준 변형입니다. */
            struct NodeStrain
            {
                uint32  _node;
                float32 _strain;
            };

            /** @brief 노드가 포함한 잎들의 이번 변형 중 가장 큰 값입니다. */
            static float32 findMaxStrain( const FractureNode& node, const vector<float32>& listLeafStrain )
            {
                float32 maxStrain = 0.0f;
                for ( uint32 leaf = node._firstLeaf; leaf < node._firstLeaf + node._leafCount; ++leaf )
                {
                    maxStrain = MathUtil::max( maxStrain, listLeafStrain[leaf] );
                }
                return maxStrain;
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
        , _nextGroupID{ 1 }
        , _eventCount{ 0 }
        , _listScratchStrain{}
    {
    }

    void DestructionState::initialize( const FractureGraph& graph, const DestructionProfile& profile, vector_reference<const uint8> listAnchoredLeaf )
    {
        _pGraph  = &graph;
        _profile = profile;
        _listInitialAnchor.assign( graph._leafCount, SW_FALSE );
        for ( size_t leaf = 0; leaf < listAnchoredLeaf.size() && leaf < graph._leafCount; ++leaf )
        {
            _listInitialAnchor[leaf] = listAnchoredLeaf[leaf] != 0 ? SW_TRUE : SW_FALSE;
        }

        // 잎에서 연결로 가는 테이블(CSR)을 만든다.
        _listLeafLinkStart.assign( graph._leafCount + 1, 0 );
        for ( const FractureLink& link : graph._listLink )
        {
            ++_listLeafLinkStart[link._leafA + 1];
            ++_listLeafLinkStart[link._leafB + 1];
        }
        for ( uint32 leaf = 0; leaf < graph._leafCount; ++leaf )
        {
            _listLeafLinkStart[leaf + 1] += _listLeafLinkStart[leaf];
        }
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
        group._parentID  = 0;
        group._leafCount = graph._leafCount;
        for ( const uint8 bAnchored : _listLeafAnchored )
        {
            group._bAnchored = ( group._bAnchored == SW_TRUE || bAnchored != SW_FALSE ) ? SW_TRUE : SW_FALSE;
        }
        _listGroup.push_back( std::move( group ) );
        _nextGroupID = 2;
        _eventCount  = 0;
    }

    vector_reference<const uint32> DestructionState::getLeafLinks( uint32 leaf ) const
    {
        return vector_reference<const uint32>{ _listLeafLinkIndex.data() + _listLeafLinkStart[leaf], _listLeafLinkStart[leaf + 1] - _listLeafLinkStart[leaf] };
    }

    const DestructionGroup* DestructionState::findGroup( uint32 groupID ) const
    {
        const int32 index = findGroupIndex( groupID );
        return index >= 0 ? &_listGroup[static_cast<size_t>( index )] : nullptr;
    }

    int32 DestructionState::findGroupIndex( uint32 groupID ) const
    {
        const auto found = std::lower_bound( _listGroup.begin(), _listGroup.end(), groupID, []( const auto& group, uint32 id )
        { return group._id < id; } );
        return ( found != _listGroup.end() && found->_id == groupID ) ? static_cast<int32>( found - _listGroup.begin() ) : -1;
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
            {
                _listLeafActive[leaf] = child;
            }
        }
        // 그 그룹의 노드 목록에서 갈라진 노드를 자식들로 바꾼다.
        const int32 groupIndex = findGroupIndex( _listLeafGroup[graph._listNode[node]._firstLeaf] );
        if ( groupIndex < 0 )
            return;
        vector<uint32>& listNode = _listGroup[static_cast<size_t>( groupIndex )]._listNode;
        const auto      iter     = std::lower_bound( listNode.begin(), listNode.end(), node );
        if ( iter != listNode.end() && *iter == node )
            listNode.erase( iter );
        for ( const uint32 child : graph.getChildren( node ) )
        {
            listNode.insert( std::lower_bound( listNode.begin(), listNode.end(), child ), child );
        }
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
        {
            openNode( _listLeafActive[leaf], outChange );
        }
        severLeaf( leaf, outChange );
        regroup( listDirty, outChange );
        return true;
    }

    bool DestructionState::breakLink( uint32 link, DestructionChange& outChange )
    {
        const bool bCannotBreakLink = _pGraph == nullptr || link >= _pGraph->_listLink.size() || _listLinkBroken[link] != SW_FALSE;
        if ( bCannotBreakLink )
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
        {
            listParent[index] = index;
        }
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
                        UnionFindUtil::unite( listParent, index, static_cast<uint32>( otherAt ) );
                }
            }
        }
        // 덩어리 순서는 가장 작은 노드 순이다(노드 목록이 오름차순이므로 루트가 가장 작은 인덱스다).
        vector<int32> listComponentOfRoot( listNode.size(), -1 );
        for ( uint32 index = 0; index < static_cast<uint32>( listNode.size() ); ++index )
        {
            const uint32 root = UnionFindUtil::findRoot( listParent, index );
            if ( listComponentOfRoot[root] < 0 )
            {
                listComponentOfRoot[root] = static_cast<int32>( outListComponent.size() );
                outListComponent.push_back( vector<uint32>{} );
            }
            outListComponent[static_cast<size_t>( listComponentOfRoot[root] )].push_back( listNode[index] );
        }
    }

    void DestructionState::computeOverloadGraph( const vector<uint32>& listNode, OverloadGraph& outGraph ) const
    {
        const FractureGraph& graph     = *_pGraph;
        const uint32         nodeCount = static_cast<uint32>( listNode.size() );
        outGraph._listNeighbor.assign( nodeCount, vector<OverloadNeighbor>{} );
        outGraph._listPairLink.clear();
        outGraph._listPairCapacity.clear();
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
                    for ( const OverloadNeighbor& neighbor : outGraph._listNeighbor[index] )
                    {
                        if ( neighbor._other == other )
                            pair = neighbor._pairIndex;
                    }
                    if ( pair == 0xFFFFFFFFu )
                    {
                        pair = static_cast<uint32>( outGraph._listPairLink.size() );
                        outGraph._listPairLink.push_back( vector<uint32>{} );
                        outGraph._listPairCapacity.push_back( 0.0f );
                        outGraph._listNeighbor[index].push_back( OverloadNeighbor{ other, 0.0f, pair } );
                        outGraph._listNeighbor[other].push_back( OverloadNeighbor{ index, 0.0f, pair } );
                    }
                    outGraph._listPairLink[pair].push_back( link );
                    outGraph._listPairCapacity[pair] += linkData._area * _profile._supportStrength;
                }
            }
        }
        for ( vector<OverloadNeighbor>& list : outGraph._listNeighbor )
        {
            for ( OverloadNeighbor& neighbor : list )
            {
                neighbor._capacity = outGraph._listPairCapacity[neighbor._pairIndex];
            }
            VectorUtil::sort( list, []( const OverloadNeighbor& lhs, const OverloadNeighbor& rhs )
            { return lhs._other < rhs._other; } );
        }
    }

    void DestructionState::measureAnchorDepth( const vector<uint32>& listNode, const OverloadGraph& graph, vector<uint32>& outListDepth, vector<uint32>& outListQueue ) const
    {
        const uint32 nodeCount = static_cast<uint32>( listNode.size() );
        outListDepth.assign( nodeCount, 0xFFFFFFFFu );
        outListQueue.clear();
        for ( uint32 index = 0; index < nodeCount; ++index )
        {
            if ( hasAnchoredLeaf( listNode[index] ) )
            {
                outListDepth[index] = 0;
                outListQueue.push_back( index );
            }
        }
        for ( size_t cursor = 0; cursor < outListQueue.size(); ++cursor )
        {
            const uint32 current = outListQueue[cursor];
            for ( const OverloadNeighbor& neighbor : graph._listNeighbor[current] )
            {
                if ( outListDepth[neighbor._other] != 0xFFFFFFFFu )
                    continue;
                outListDepth[neighbor._other] = outListDepth[current] + 1;
                outListQueue.push_back( neighbor._other );
            }
        }
    }

    bool DestructionState::spreadLoad( const vector<uint32>& listNode, const OverloadGraph& graph, const vector<uint32>& listDepth, const vector<uint32>& listQueue,
                                       DestructionChange& outChange )
    {
        const FractureGraph& fractureGraph = *_pGraph;
        vector<float32>      listIncoming( listNode.size(), 0.0f );
        bool                 bBroke  = false;
        const float32        gravity = PhysicsSystem::getConfiguredGravityMagnitude(); // 설정된 물리 중력이다. 서버와 클라이언트가 같은 설정을 읽는다.
        for ( size_t order = listQueue.size(); order > 0; --order )
        {
            const uint32 current = listQueue[order - 1];
            if ( listDepth[current] == 0 )
                continue;
            const float32 load          = fractureGraph._listNode[listNode[current]]._volume * _profile._density * gravity + listIncoming[current];
            float32       totalCapacity = 0.0f;
            for ( const OverloadNeighbor& neighbor : graph._listNeighbor[current] )
            {
                if ( listDepth[neighbor._other] < listDepth[current] )
                    totalCapacity += neighbor._capacity;
            }
            if ( totalCapacity <= 0.0f )
                continue;
            for ( const OverloadNeighbor& neighbor : graph._listNeighbor[current] )
            {
                if ( listDepth[neighbor._other] >= listDepth[current] )
                    continue;
                const float32 share = load * neighbor._capacity / totalCapacity;
                listIncoming[neighbor._other] += share;
                for ( const uint32 link : graph._listPairLink[neighbor._pairIndex] )
                {
                    _listLinkLoad[link] = share;
                }
                if ( share <= neighbor._capacity )
                    continue;
                for ( const uint32 link : graph._listPairLink[neighbor._pairIndex] )
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

    bool DestructionState::relieveOverload( const vector<uint32>& listNode, DestructionChange& outChange )
    {
        OverloadGraph graph;
        computeOverloadGraph( listNode, graph );
        vector<uint32> listDepth;
        vector<uint32> listQueue;
        measureAnchorDepth( listNode, graph, listDepth, listQueue );
        return spreadLoad( listNode, graph, listDepth, listQueue, outChange );
    }

    void DestructionState::regroup( vector<uint32>& inoutListDirtyGroupID, DestructionChange& outChange )
    {
        VectorUtil::sort( inoutListDirtyGroupID );
        vector<DestructionGroup> listNewGroup;
        for ( const uint32 groupID : inoutListDirtyGroupID )
        {
            const int32 groupIndex = findGroupIndex( groupID );
            if ( groupIndex < 0 )
                continue;
            const DestructionGroup old = _listGroup[static_cast<size_t>( groupIndex )];
            vector<vector<uint32>> listComponent;
            // 붙은 덩어리는 무게로 끊길 수 있다. 끊기면 다시 나누고 지지를 다시 계산한다.
            for ( uint32 pass = 0; pass < kMaxSupportPass; ++pass )
            {
                splitComponents( old._listNode, listComponent );
                bool bBroke = false;
                for ( const vector<uint32>& component : listComponent )
                {
                    bool bAnchored = false;
                    for ( const uint32 node : component )
                    {
                        bAnchored = bAnchored || hasAnchoredLeaf( node );
                    }
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
                part._parentID = old._id;
                for ( const uint32 node : part._listNode )
                {
                    part._leafCount += _pGraph->_listNode[node]._leafCount;
                    if ( hasAnchoredLeaf( node ) )
                        part._bAnchored = SW_TRUE;
                }
                listPart.push_back( std::move( part ) );
            }
            // 한 덩어리 그대로면(묶음이 갈라졌을 뿐이면) 번호를 유지한다. 바디도 렌더링도 바뀌지 않는다.
            if ( listPart.size() == 1 && listPart[0]._bAnchored == old._bAnchored )
            {
                DestructionGroup& kept = _listGroup[static_cast<size_t>( groupIndex )];
                kept._listNode         = std::move( listPart[0]._listNode );
                continue;
            }
            outChange._listRemovedGroup.push_back( old._id );
            _listGroup.erase( _listGroup.begin() + groupIndex );
            for ( DestructionGroup& part : listPart )
            {
                listNewGroup.push_back( std::move( part ) );
            }
        }
        for ( DestructionGroup& group : listNewGroup )
        {
            group._id = _nextGroupID++;
            for ( const uint32 node : group._listNode )
            {
                const FractureNode& data = _pGraph->_listNode[node];
                for ( uint32 leaf = data._firstLeaf; leaf < data._firstLeaf + data._leafCount; ++leaf )
                {
                    _listLeafGroup[leaf] = group._id;
                }
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
        uint64 hash    = HashUtil::kFNVOffset64;
        for ( size_t node = 0; node < _listNodeStrain.size(); ++node )
        {
            hash = Internal::mixHash( hash, BitUtil::floatToBits( _listNodeStrain[node] ) );
            hash = Internal::mixHash( hash, _listNodeBroken[node] );
        }
        for ( size_t link = 0; link < _listLinkStrain.size(); ++link )
        {
            hash = Internal::mixHash( hash, BitUtil::floatToBits( _listLinkStrain[link] ) );
            hash = Internal::mixHash( hash, _listLinkBroken[link] );
        }
        for ( const uint8 bAnchored : _listLeafAnchored )
        {
            hash = Internal::mixHash( hash, bAnchored );
        }
        hash = Internal::mixHash( hash, _eventCount );
        for ( const DestructionGroup& group : _listGroup )
        {
            hash = Internal::mixHash( hash, group._id );
            hash = Internal::mixHash( hash, group._bAnchored );
            for ( const uint32 node : group._listNode )
            {
                hash = Internal::mixHash( hash, node );
            }
        }
        return hash;
    }

    void DestructionState::writeSnapshot( vector<uint8>& outBytes ) const
    {
        using Internal = DestructionStateInternal;
        BitWriter writer;
        writer.writeUint32( Internal::kMagic );
        writer.writeVarUint( Internal::kVersion );
        writer.writeVarUint( _listNodeBroken.size() );
        writer.writeVarUint( _listLinkBroken.size() );
        writer.writeVarUint( _listLeafAnchored.size() );
        writer.writeVarUint( _eventCount );
        writer.writeVarUint( _nextGroupID );
        Internal::writeFlagBits( writer, _listNodeBroken );
        Internal::writeFlagBits( writer, _listLinkBroken );
        Internal::writeFlagBits( writer, _listLeafAnchored );
        Internal::writeSparseFloats( writer, _listNodeStrain );
        Internal::writeSparseFloats( writer, _listLinkStrain );
        writer.writeVarUint( _listGroup.size() );
        for ( const DestructionGroup& group : _listGroup )
        {
            writer.writeVarUint( group._id );
            writer.writeVarUint( group._parentID );
            writer.writeBits( group._bAnchored == SW_TRUE ? 1u : 0u, 1 );
            writer.writeVarUint( group._listNode.size() );
            uint32 previous = 0;
            for ( const uint32 node : group._listNode )
            {
                writer.writeVarUint( node - previous ); // 활성 노드는 오름차순
                previous = node;
            }
        }
        outBytes = writer.releaseBytes();
    }

    bool DestructionState::readSnapshot( const uint8* pData, size_t size )
    {
        using Internal                = DestructionStateInternal;
        const bool bInvalidStateBytes = _pGraph == nullptr || pData == nullptr || size == 0 || size > static_cast<size_t>( MathUtil::kMaxInt32 );
        if ( bInvalidStateBytes )
            return false;
        const FractureGraph& graph = *_pGraph;
        BitReader            reader( pData, static_cast<int32>( size ) );
        if ( reader.readUint32() != Internal::kMagic || reader.readVarUint() != Internal::kVersion )
            return false;
        const uint64 nodeCount           = reader.readVarUint();
        const uint64 linkCount           = reader.readVarUint();
        const uint64 leafCount           = reader.readVarUint();
        const bool   bGraphShapeMismatch = nodeCount != graph._listNode.size() || linkCount != graph._listLink.size() || leafCount != graph._leafCount;
        if ( bGraphShapeMismatch )
            return false;
        const uint32 eventCount  = reader.readVarUint32();
        const uint32 nextGroupID = reader.readVarUint32();

        vector<uint8>   listNodeBroken;
        vector<uint8>   listLinkBroken;
        vector<uint8>   listLeafAnchored;
        vector<float32> listNodeStrain;
        vector<float32> listLinkStrain;
        Internal::readFlagBits( reader, listNodeBroken, graph._listNode.size() );
        Internal::readFlagBits( reader, listLinkBroken, graph._listLink.size() );
        Internal::readFlagBits( reader, listLeafAnchored, graph._leafCount );
        const bool bNodeStrainRead = Internal::readSparseFloats( reader, listNodeStrain, graph._listNode.size() );
        const bool bLinkStrainRead = Internal::readSparseFloats( reader, listLinkStrain, graph._listLink.size() );
        if ( bNodeStrainRead == false || bLinkStrainRead == false )
            return false;

        // 그룹: 활성 노드가 잎을 하나씩 정확히 덮어야 한다.
        const uint64 groupCount = reader.readVarUint();
        if ( groupCount == 0 || groupCount > graph._listNode.size() )
            return false;
        vector<DestructionGroup> listGroup;
        vector<uint32>           listLeafActive( graph._leafCount, Internal::kNoActiveNode );
        vector<uint32>           listLeafGroup( graph._leafCount, 0 );
        for ( uint64 groupIndex = 0; groupIndex < groupCount; ++groupIndex )
        {
            DestructionGroup group;
            group._id                = reader.readVarUint32();
            group._parentID          = reader.readVarUint32();
            group._bAnchored         = reader.readBits( 1 ) != 0 ? SW_TRUE : SW_FALSE;
            const uint64 activeCount = reader.readVarUint();
            const bool   bBadGroup   = reader.hasOverflowed() || activeCount == 0 || activeCount > graph._listNode.size() || group._id == 0 || group._id >= nextGroupID;
            if ( bBadGroup )
                return false;
            uint64 node = 0;
            for ( uint64 entry = 0; entry < activeCount; ++entry )
            {
                node += reader.readVarUint();
                if ( node >= graph._listNode.size() || reader.hasOverflowed() )
                    return false;
                const FractureNode& data = graph._listNode[static_cast<size_t>( node )];
                for ( uint32 leaf = data._firstLeaf; leaf < data._firstLeaf + data._leafCount; ++leaf )
                {
                    if ( leaf >= graph._leafCount || listLeafActive[leaf] != Internal::kNoActiveNode )
                        return false;
                    listLeafActive[leaf] = static_cast<uint32>( node );
                    listLeafGroup[leaf]  = group._id;
                }
                group._leafCount += data._leafCount;
                group._listNode.push_back( static_cast<uint32>( node ) );
            }
            listGroup.push_back( std::move( group ) );
        }
        if ( reader.hasOverflowed() )
            return false;
        for ( const uint32 active : listLeafActive )
        {
            if ( active == Internal::kNoActiveNode )
                return false;
        }

        _listNodeBroken   = std::move( listNodeBroken );
        _listLinkBroken   = std::move( listLinkBroken );
        _listLeafAnchored = std::move( listLeafAnchored );
        _listNodeStrain   = std::move( listNodeStrain );
        _listLinkStrain   = std::move( listLinkStrain );
        _listLinkLoad.assign( graph._listLink.size(), 0.0f );
        _listLeafActive = std::move( listLeafActive );
        _listLeafGroup  = std::move( listLeafGroup );
        _listGroup      = std::move( listGroup );
        _nextGroupID    = nextGroupID;
        _eventCount     = eventCount;
        return true;
    }

    bool DestructionState::applyDamage( const DestructionDamageEvent& event, DestructionChange& outChange )
    {
        using Internal = DestructionStateInternal;
        if ( _pGraph == nullptr )
            return false;
        const FractureGraph& graph = *_pGraph;
        ++_eventCount;
        vector<DestructionLeafStrain> listLeafStrain;
        DestructionDamageUtil::computeLeafStrain( graph, event, _listLeafGroup, listLeafStrain );
        if ( listLeafStrain.empty() )
            return false;
        _listScratchStrain.assign( graph._leafCount, 0.0f );
        vector<uint32> listDirtyGroup;
        for ( const DestructionLeafStrain& entry : listLeafStrain )
        {
            _listScratchStrain[entry._leaf] = entry._strain;
            const uint32 groupID            = _listLeafGroup[entry._leaf];
            if ( VectorUtil::contains( listDirtyGroup, groupID ) == false )
                listDirtyGroup.push_back( groupID );
        }
        const uint32 brokenNodeBefore = outChange._brokenNodeCount;
        const uint32 brokenLinkBefore = outChange._brokenLinkCount;

        // 활성 노드마다 그 잎들의 최대 변형을 노드 번호 순으로 쌓고, 갈라지면 넘친 비율만큼 자식에게 넘긴다.
        vector<Internal::NodeStrain> listQueue;
        for ( const DestructionLeafStrain& entry : listLeafStrain )
        {
            const uint32 node  = _listLeafActive[entry._leaf];
            bool         bSeen = false;
            for ( Internal::NodeStrain& queued : listQueue )
            {
                if ( queued._node == node )
                {
                    queued._strain = MathUtil::max( queued._strain, entry._strain );
                    bSeen          = true;
                }
            }
            if ( bSeen == false )
                listQueue.push_back( Internal::NodeStrain{ node, entry._strain } );
        }
        while ( listQueue.empty() == false )
        {
            VectorUtil::sort( listQueue, []( const Internal::NodeStrain& lhs, const Internal::NodeStrain& rhs )
            { return lhs._node > rhs._node; } );
            const Internal::NodeStrain current = listQueue.back();
            listQueue.pop_back();
            const FractureNode& data       = graph._listNode[current._node];
            const float32       threshold  = _profile.getStrainThreshold( data._depth );
            const float32       after      = _listNodeStrain[current._node] + current._strain;
            _listNodeStrain[current._node] = after;
            if ( after < threshold || _listNodeBroken[current._node] != SW_FALSE )
                continue;
            if ( data.isLeaf() )
            {
                _listNodeBroken[current._node] = SW_TRUE;
                ++outChange._brokenNodeCount;
                severLeaf( current._node, outChange );
                continue;
            }
            const float32 excess = MathUtil::clamp( ( after - threshold ) / current._strain, 0.0f, 1.0f );
            openNode( current._node, outChange );
            if ( excess <= 0.0f )
                continue;
            for ( const uint32 child : graph.getChildren( current._node ) )
            {
                const float32 childStrain = Internal::findMaxStrain( graph._listNode[child], _listScratchStrain ) * excess;
                if ( childStrain > 0.0f )
                    listQueue.push_back( Internal::NodeStrain{ child, childStrain } );
            }
        }

        // 연결: 활성 노드가 서로 다른 두 잎 사이만, 두 잎 변형의 평균을 쌓는다(연결 번호 순으로 한 번씩).
        vector<uint32> listTouchedLink;
        for ( const DestructionLeafStrain& entry : listLeafStrain )
        {
            for ( const uint32 link : getLeafLinks( entry._leaf ) )
            {
                listTouchedLink.push_back( link );
            }
        }
        VectorUtil::sort( listTouchedLink );
        listTouchedLink.erase( std::unique( listTouchedLink.begin(), listTouchedLink.end() ), listTouchedLink.end() );
        for ( const uint32 link : listTouchedLink )
        {
            const FractureLink& data = graph._listLink[link];
            if ( _listLinkBroken[link] != SW_FALSE || _listLeafActive[data._leafA] == _listLeafActive[data._leafB] )
                continue;
            _listLinkStrain[link] += 0.5f * ( _listScratchStrain[data._leafA] + _listScratchStrain[data._leafB] );
            if ( _listLinkStrain[link] < data._area * _profile._linkStrength )
                continue;
            _listLinkBroken[link] = SW_TRUE;
            ++outChange._brokenLinkCount;
            const uint32 otherGroup = _listLeafGroup[data._leafB];
            if ( VectorUtil::contains( listDirtyGroup, otherGroup ) == false )
                listDirtyGroup.push_back( otherGroup );
        }

        const bool bChanged = outChange._brokenNodeCount != brokenNodeBefore || outChange._brokenLinkCount != brokenLinkBefore;
        if ( bChanged )
            regroup( listDirtyGroup, outChange );
        return bChanged;
    }
} // namespace sw
