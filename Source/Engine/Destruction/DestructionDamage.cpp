#include "pch.h"

#include "Engine/Destruction/DestructionDamage.h"

#include "Core/Container/LittleEndianUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Destruction/DestructionProfile.h"
#include "Engine/Destruction/DestructionState.h"
#include "Engine/Destruction/Fracture/FractureGraph.h"

namespace sw
{
    SW_LOG_CALLER( "DestructionDamage" );

    namespace
    {
        struct DestructionDamageInternal
        {
            static constexpr uint8  kArrMagic[4] = { 'S', 'W', 'D', 'E' };
            static constexpr uint32 kEventSize   = 1 + 4 + 4 + 4 * 9;
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
        LittleEndianUtil::appendUint32( outBytes, kVersion );
        LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( _seed & 0xFFFFFFFFull ) );
        LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( _seed >> 32 ) );
        LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( _listEvent.size() ) );
        for ( const DestructionDamageEvent& event : _listEvent )
        {
            outBytes.push_back( static_cast<uint8>( event._kind ) );
            LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( event._leafHint ) );
            LittleEndianUtil::appendUint32( outBytes, event._groupID );
            const float32 arrValue[9] = { event._position._x, event._position._y, event._position._z, event._direction._x, event._direction._y,
                                          event._direction._z, event._strain, event._radius, event._impulse };
            for ( const float32 value : arrValue )
            {
                LittleEndianUtil::appendFloat32( outBytes, value );
            }
        }
    }

    bool DestructionEventLog::readFromBytes( const uint8* pData, size_t size )
    {
        using Internal = DestructionDamageInternal;
        _listEvent.clear();
        _seed                       = 0;
        const bool bInvalidEventLog = pData == nullptr || size < 20 || Memory::compare( pData, Internal::kArrMagic, 4 ) != 0 || LittleEndianUtil::readUint32( pData + 4 ) != kVersion;
        if ( bInvalidEventLog )
        {
            SW_LOG_ERROR( "Not a destruction event log (bad magic or version)" );
            return false;
        }
        const uint64 seedLow  = LittleEndianUtil::readUint32( pData + 8 );
        const uint64 seedHigh = LittleEndianUtil::readUint32( pData + 12 );
        const uint32 count    = LittleEndianUtil::readUint32( pData + 16 );
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
            event._leafHint  = static_cast<int32>( LittleEndianUtil::readUint32( pCursor + 1 ) );
            event._groupID   = LittleEndianUtil::readUint32( pCursor + 5 );
            const uint8* pF  = pCursor + 9;
            event._position  = float3{ LittleEndianUtil::readFloat32( pF ), LittleEndianUtil::readFloat32( pF + 4 ), LittleEndianUtil::readFloat32( pF + 8 ) };
            event._direction = float3{ LittleEndianUtil::readFloat32( pF + 12 ), LittleEndianUtil::readFloat32( pF + 16 ), LittleEndianUtil::readFloat32( pF + 20 ) };
            event._strain    = LittleEndianUtil::readFloat32( pF + 24 );
            event._radius    = LittleEndianUtil::readFloat32( pF + 28 );
            event._impulse   = LittleEndianUtil::readFloat32( pF + 32 );
            _listEvent.push_back( event );
        }
        _seed = seedLow | ( seedHigh << 32 );
        return true;
    }
} // namespace sw
