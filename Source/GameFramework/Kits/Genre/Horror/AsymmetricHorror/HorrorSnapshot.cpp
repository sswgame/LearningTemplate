#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/HorrorSnapshot.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

namespace sw
{
    namespace
    {
        struct HorrorSnapshotInternal
        {
            static constexpr uint32  kMagic         = FourCcUtil::make( "DBD1" );
            static constexpr int32   kMaxCount      = 255;
            static constexpr float32 kWorldHalfSize = 1000.0f;
            static constexpr float32 kPositionStep  = 0.01f;
            static constexpr float32 kRatioStep     = 1.0f / 1023.0f;
            static constexpr float32 kMaxTime       = 10000.0f;
            static constexpr float32 kTimeStep      = 0.01f;

            static void writePosition( BitWriter& outWriter, const float3& position )
            {
                outWriter.writeQuantizedFloat( position._x, -kWorldHalfSize, kWorldHalfSize, kPositionStep );
                outWriter.writeQuantizedFloat( position._z, -kWorldHalfSize, kWorldHalfSize, kPositionStep );
            }

            static float3 readPosition( BitReader& reader )
            {
                float3 position;
                position._x = reader.readQuantizedFloat( -kWorldHalfSize, kWorldHalfSize, kPositionStep );
                position._z = reader.readQuantizedFloat( -kWorldHalfSize, kWorldHalfSize, kPositionStep );
                return position;
            }

            static void    writeTime( BitWriter& outWriter, float32 seconds ) { outWriter.writeQuantizedFloat( seconds, 0.0f, kMaxTime, kTimeStep ); }
            static float32 readTime( BitReader& reader ) { return reader.readQuantizedFloat( 0.0f, kMaxTime, kTimeStep ); }
            static void    writeRatio( BitWriter& outWriter, float32 ratio ) { outWriter.writeQuantizedFloat( ratio, 0.0f, 1.0f, kRatioStep ); }
            static float32 readRatio( BitReader& reader ) { return reader.readQuantizedFloat( 0.0f, 1.0f, kRatioStep ); }

            static int32 writeCount( BitWriter& outWriter, size_t size )
            {
                const int32 count = MathUtil::min( static_cast<int32>( size ), kMaxCount );
                outWriter.writeInt( count, 0, kMaxCount );
                return count;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void HorrorSnapshotCodec::write( const HorrorSnapshot& snapshot, BitWriter& outWriter )
    {
        using Internal = HorrorSnapshotInternal;
        outWriter.writeUint32( Internal::kMagic );
        outWriter.writeUint32( snapshot._tick );
        outWriter.writeInt( static_cast<int32>( snapshot._phase ), 0, static_cast<int32>( MatchPhase::Ended ) );
        outWriter.writeBool( snapshot._bGatesPowered == SW_TRUE );
        outWriter.writeBool( snapshot._bHatchOpen == SW_TRUE );
        outWriter.writeBool( snapshot._bCollapseStarted == SW_TRUE );
        Internal::writeTime( outWriter, snapshot._collapseRemaining );

        const int32 generatorCount = Internal::writeCount( outWriter, snapshot._listGeneratorProgress.size() );
        for ( int32 index = 0; index < generatorCount; ++index )
        {
            Internal::writeRatio( outWriter, snapshot._listGeneratorProgress[static_cast<size_t>( index )] );
            const uint8 flag = static_cast<size_t>( index ) < snapshot._listGeneratorFlag.size() ? snapshot._listGeneratorFlag[static_cast<size_t>( index )] : 0;
            outWriter.writeBits( flag, 3 );
        }
        const int32 gateCount = Internal::writeCount( outWriter, snapshot._listGateProgress.size() );
        for ( int32 index = 0; index < gateCount; ++index )
        {
            Internal::writeRatio( outWriter, snapshot._listGateProgress[static_cast<size_t>( index )] );
        }
        const int32 palletCount = Internal::writeCount( outWriter, snapshot._listPalletState.size() );
        for ( int32 index = 0; index < palletCount; ++index )
        {
            outWriter.writeInt( static_cast<int32>( snapshot._listPalletState[static_cast<size_t>( index )] ), 0, static_cast<int32>( PalletState::Broken ) );
        }
        const int32 windowCount = Internal::writeCount( outWriter, snapshot._listWindowBlocked.size() );
        for ( int32 index = 0; index < windowCount; ++index )
        {
            outWriter.writeBool( snapshot._listWindowBlocked[static_cast<size_t>( index )] == SW_TRUE );
        }

        const int32 survivorCount = Internal::writeCount( outWriter, snapshot._listSurvivor.size() );
        for ( int32 index = 0; index < survivorCount; ++index )
        {
            const HorrorSurvivorSnapshot& survivor = snapshot._listSurvivor[static_cast<size_t>( index )];
            outWriter.writeInt( static_cast<int32>( survivor._state ), 0, static_cast<int32>( SurvivorState::Escaped ) );
            outWriter.writeInt( static_cast<int32>( survivor._activity ), 0, static_cast<int32>( SurvivorActivity::InLocker ) );
            outWriter.writeInt( survivor._hookStage, 0, 15 );
            Internal::writePosition( outWriter, survivor._position );
            Internal::writeTime( outWriter, survivor._hookTimer );
            Internal::writeRatio( outWriter, survivor._healProgress );
            Internal::writeTime( outWriter, survivor._bleedout );
            Internal::writeRatio( outWriter, survivor._wiggleProgress );
        }

        Internal::writePosition( outWriter, snapshot._killerPosition );
        outWriter.writeInt( snapshot._killerCarrying, -1, Internal::kMaxCount );
        Internal::writeTime( outWriter, snapshot._killerStun );
        Internal::writeTime( outWriter, snapshot._killerCooldown );
    }

    bool HorrorSnapshotCodec::read( BitReader& reader, HorrorSnapshot& outSnapshot )
    {
        using Internal = HorrorSnapshotInternal;
        outSnapshot    = HorrorSnapshot{};
        if ( reader.readUint32() != Internal::kMagic )
            return false;
        outSnapshot._tick              = reader.readUint32();
        outSnapshot._phase             = static_cast<MatchPhase>( reader.readInt( 0, static_cast<int32>( MatchPhase::Ended ) ) );
        outSnapshot._bGatesPowered     = reader.readBool() ? SW_TRUE : SW_FALSE;
        outSnapshot._bHatchOpen        = reader.readBool() ? SW_TRUE : SW_FALSE;
        outSnapshot._bCollapseStarted  = reader.readBool() ? SW_TRUE : SW_FALSE;
        outSnapshot._collapseRemaining = Internal::readTime( reader );

        const int32 generatorCount = reader.readInt( 0, Internal::kMaxCount );
        if ( reader.hasOverflowed() )
            return false;
        for ( int32 index = 0; index < generatorCount; ++index )
        {
            outSnapshot._listGeneratorProgress.push_back( Internal::readRatio( reader ) );
            outSnapshot._listGeneratorFlag.push_back( static_cast<uint8>( reader.readBits( 3 ) ) );
        }
        const int32 gateCount = reader.readInt( 0, Internal::kMaxCount );
        if ( reader.hasOverflowed() )
            return false;
        for ( int32 index = 0; index < gateCount; ++index )
        {
            outSnapshot._listGateProgress.push_back( Internal::readRatio( reader ) );
        }
        const int32 palletCount = reader.readInt( 0, Internal::kMaxCount );
        if ( reader.hasOverflowed() )
            return false;
        for ( int32 index = 0; index < palletCount; ++index )
        {
            outSnapshot._listPalletState.push_back( static_cast<PalletState>( reader.readInt( 0, static_cast<int32>( PalletState::Broken ) ) ) );
        }
        const int32 windowCount = reader.readInt( 0, Internal::kMaxCount );
        if ( reader.hasOverflowed() )
            return false;
        for ( int32 index = 0; index < windowCount; ++index )
        {
            outSnapshot._listWindowBlocked.push_back( reader.readBool() ? SW_TRUE : SW_FALSE );
        }

        const int32 survivorCount = reader.readInt( 0, Internal::kMaxCount );
        if ( reader.hasOverflowed() )
            return false;
        for ( int32 index = 0; index < survivorCount; ++index )
        {
            HorrorSurvivorSnapshot survivor;
            survivor._state          = static_cast<SurvivorState>( reader.readInt( 0, static_cast<int32>( SurvivorState::Escaped ) ) );
            survivor._activity       = static_cast<SurvivorActivity>( reader.readInt( 0, static_cast<int32>( SurvivorActivity::InLocker ) ) );
            survivor._hookStage      = reader.readInt( 0, 15 );
            survivor._position       = Internal::readPosition( reader );
            survivor._hookTimer      = Internal::readTime( reader );
            survivor._healProgress   = Internal::readRatio( reader );
            survivor._bleedout       = Internal::readTime( reader );
            survivor._wiggleProgress = Internal::readRatio( reader );
            if ( reader.hasOverflowed() )
                return false;
            outSnapshot._listSurvivor.push_back( survivor );
        }

        outSnapshot._killerPosition = Internal::readPosition( reader );
        outSnapshot._killerCarrying = reader.readInt( -1, Internal::kMaxCount );
        outSnapshot._killerStun     = Internal::readTime( reader );
        outSnapshot._killerCooldown = Internal::readTime( reader );
        return reader.hasOverflowed() == false;
    }
} // namespace sw
