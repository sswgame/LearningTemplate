#include "pch.h"

#include "Engine/Destruction/DestructionDamage.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Destruction/DestructionProfile.h"
#include "Engine/Destruction/DestructionState.h"
#include "Engine/Destruction/FractureGraph.h"

namespace sw
{
    SW_LOG_CALLER( "DestructionDamage" );

    namespace
    {
        struct DestructionDamageInternal
        {
            static constexpr uint8  kArrMagic[4] = { 'S', 'W', 'D', 'E' };
            static constexpr uint32 kEventSize   = 1 + 4 + 4 + 4 * 9;

            /** @brief 노드와 그 노드에 이번 사건이 준 변형입니다. */
            struct NodeStrain
            {
                uint32  _node;
                float32 _strain;
            };

            static void appendUint32( vector<uint8>& outBytes, uint32 value )
            {
                for ( uint32 shift = 0; shift < 32; shift += 8 )
                {
                    outBytes.push_back( static_cast<uint8>( ( value >> shift ) & 0xFFu ) );
                }
            }

            static void appendFloat32( vector<uint8>& outBytes, float32 value )
            {
                uint32 bits = 0;
                Memory::copy( &bits, &value, sizeof( bits ) );
                appendUint32( outBytes, bits );
            }

            static uint32 readUint32( const uint8* pData )
            {
                return static_cast<uint32>( pData[0] ) | ( static_cast<uint32>( pData[1] ) << 8 ) | ( static_cast<uint32>( pData[2] ) << 16 ) |
                       ( static_cast<uint32>( pData[3] ) << 24 );
            }

            static float32 readFloat32( const uint8* pData )
            {
                const uint32 bits  = readUint32( pData );
                float32      value = 0.0f;
                Memory::copy( &value, &bits, sizeof( value ) );
                return value;
            }

            /** @brief 노드가 품은 잎들의 이번 변형 중 가장 큰 것입니다. */
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
    void DestructionDamageUtil::computeLeafStrain( const FractureGraph& graph, const DestructionDamageEvent& event, vector_reference<const uint32> listLeafGroup,
                                                   vector<DestructionLeafStrain>& outListStrain )
    {
        outListStrain.clear();
        if ( event._strain <= 0.0f || graph._leafCount == 0 )
            return;
        const bool bFilter = event._groupID != 0 && listLeafGroup.size() == graph._leafCount;
        const bool bHint   = event._leafHint >= 0 && static_cast<uint32>( event._leafHint ) < graph._leafCount &&
                           ( bFilter == false || listLeafGroup[static_cast<uint32>( event._leafHint )] == event._groupID );
        if ( event._radius <= 0.0f )
        {
            uint32  target = bHint ? static_cast<uint32>( event._leafHint ) : 0u;
            float32 best   = MathUtil::kMaxFloat;
            if ( bHint == false )
            {
                for ( uint32 leaf = 0; leaf < graph._leafCount; ++leaf )
                {
                    if ( bFilter && listLeafGroup[leaf] != event._groupID )
                        continue;
                    const float32 distance = float3::getDistanceSquared( graph._listNode[leaf]._centroid, event._position );
                    if ( distance < best )
                    {
                        best   = distance;
                        target = leaf;
                    }
                }
            }
            if ( best < MathUtil::kMaxFloat || bHint )
                outListStrain.push_back( DestructionLeafStrain{ target, event._strain } );
            return;
        }
        for ( uint32 leaf = 0; leaf < graph._leafCount; ++leaf )
        {
            if ( bFilter && listLeafGroup[leaf] != event._groupID )
                continue;
            float32 strain = 0.0f;
            if ( bHint && static_cast<uint32>( event._leafHint ) == leaf )
            {
                strain = event._strain;
            }
            else
            {
                const float32 distance = float3::getDistance( graph._listNode[leaf]._centroid, event._position );
                if ( distance < event._radius )
                    strain = event._strain * ( 1.0f - distance / event._radius );
            }
            if ( strain > 0.0f )
                outListStrain.push_back( DestructionLeafStrain{ leaf, strain } );
        }
    }

    DestructionDamageEvent DestructionDamageUtil::makeImpactEvent( const DestructionProfile& profile, const float3& position, const float3& normal, float32 impulse )
    {
        DestructionDamageEvent event;
        event._kind      = DestructionDamageKind::Impact;
        event._position  = position;
        event._direction = normal;
        event._radius    = profile._impactRadius;
        event._impulse   = 0.0f;
        event._strain    = impulse > profile._minImpulse ? ( impulse - profile._minImpulse ) * profile._impulseToStrain : 0.0f;
        return event;
    }

    void DestructionEventLog::makeBytes( vector<uint8>& outBytes ) const
    {
        using Internal = DestructionDamageInternal;
        outBytes.clear();
        outBytes.reserve( 20 + _listEvent.size() * Internal::kEventSize );
        for ( const uint8 magic : Internal::kArrMagic )
        {
            outBytes.push_back( magic );
        }
        Internal::appendUint32( outBytes, kVersion );
        Internal::appendUint32( outBytes, static_cast<uint32>( _seed & 0xFFFFFFFFull ) );
        Internal::appendUint32( outBytes, static_cast<uint32>( _seed >> 32 ) );
        Internal::appendUint32( outBytes, static_cast<uint32>( _listEvent.size() ) );
        for ( const DestructionDamageEvent& event : _listEvent )
        {
            outBytes.push_back( static_cast<uint8>( event._kind ) );
            Internal::appendUint32( outBytes, static_cast<uint32>( event._leafHint ) );
            Internal::appendUint32( outBytes, event._groupID );
            const float32 arrValue[9] = { event._position._x, event._position._y, event._position._z, event._direction._x, event._direction._y,
                                          event._direction._z, event._strain, event._radius, event._impulse };
            for ( const float32 value : arrValue )
            {
                Internal::appendFloat32( outBytes, value );
            }
        }
    }

    bool DestructionEventLog::readFromBytes( const uint8* pData, size_t size )
    {
        using Internal = DestructionDamageInternal;
        _listEvent.clear();
        _seed = 0;
        if ( pData == nullptr || size < 20 || Memory::compare( pData, Internal::kArrMagic, 4 ) != 0 || Internal::readUint32( pData + 4 ) != kVersion )
        {
            SW_LOG_ERROR( "Not a destruction event log (bad magic or version)" );
            return false;
        }
        const uint64 seedLow  = Internal::readUint32( pData + 8 );
        const uint64 seedHigh = Internal::readUint32( pData + 12 );
        const uint32 count    = Internal::readUint32( pData + 16 );
        if ( static_cast<uint64>( count ) * Internal::kEventSize != size - 20 )
        {
            SW_LOG_ERROR( "Destruction event log length does not match its event count" );
            return false;
        }
        _listEvent.reserve( count );
        const uint8* pCursor = pData + 20;
        for ( uint32 index = 0; index < count; ++index, pCursor += Internal::kEventSize )
        {
            if ( pCursor[0] >= static_cast<uint8>( DestructionDamageKind::Count ) )
            {
                SW_LOG_ERROR( "Destruction event %# has an unknown kind %#", index, pCursor[0] );
                _listEvent.clear();
                return false;
            }
            DestructionDamageEvent event;
            event._kind      = static_cast<DestructionDamageKind>( pCursor[0] );
            event._leafHint  = static_cast<int32>( Internal::readUint32( pCursor + 1 ) );
            event._groupID   = Internal::readUint32( pCursor + 5 );
            const uint8* pF  = pCursor + 9;
            event._position  = float3{ Internal::readFloat32( pF ), Internal::readFloat32( pF + 4 ), Internal::readFloat32( pF + 8 ) };
            event._direction = float3{ Internal::readFloat32( pF + 12 ), Internal::readFloat32( pF + 16 ), Internal::readFloat32( pF + 20 ) };
            event._strain    = Internal::readFloat32( pF + 24 );
            event._radius    = Internal::readFloat32( pF + 28 );
            event._impulse   = Internal::readFloat32( pF + 32 );
            _listEvent.push_back( event );
        }
        _seed = seedLow | ( seedHigh << 32 );
        return true;
    }

    bool DestructionState::applyDamage( const DestructionDamageEvent& event, DestructionChange& outChange )
    {
        using Internal = DestructionDamageInternal;
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
            if ( std::find( listDirtyGroup.begin(), listDirtyGroup.end(), groupID ) == listDirtyGroup.end() )
                listDirtyGroup.push_back( groupID );
        }
        const uint32 brokenNodeBefore = outChange._brokenNodeCount;
        const uint32 brokenLinkBefore = outChange._brokenLinkCount;

        // 활성 노드마다 그 잎들의 최대 변형 — 노드 번호 순으로 쌓고, 갈라지면 넘친 비율만큼 자식에게.
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
            std::sort( listQueue.begin(), listQueue.end(), []( const Internal::NodeStrain& lhs, const Internal::NodeStrain& rhs )
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

        // 연결 — 활성 노드가 다른 두 잎 사이만, 두 잎 변형의 평균을 쌓는다(연결 번호 순으로 한 번씩).
        vector<uint32> listTouchedLink;
        for ( const DestructionLeafStrain& entry : listLeafStrain )
        {
            for ( const uint32 link : getLeafLinks( entry._leaf ) )
            {
                listTouchedLink.push_back( link );
            }
        }
        std::sort( listTouchedLink.begin(), listTouchedLink.end() );
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
            if ( std::find( listDirtyGroup.begin(), listDirtyGroup.end(), otherGroup ) == listDirtyGroup.end() )
                listDirtyGroup.push_back( otherGroup );
        }

        const bool bChanged = outChange._brokenNodeCount != brokenNodeBefore || outChange._brokenLinkCount != brokenLinkBefore;
        if ( bChanged )
            regroup( listDirtyGroup, outChange );
        return bChanged;
    }
} // namespace sw
