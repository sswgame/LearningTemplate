#include "pch.h"

#include "GameFramework/Kits/Genre/Action/MechArena/MechArenaSnapshot.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

namespace sw
{
    namespace
    {
        struct MechArenaSnapshotInternal
        {
            static constexpr uint32  kMagic         = FourCcUtil::make( "MECH" );
            static constexpr int32   kMaxCount      = 255;
            static constexpr int32   kMaxAmmo       = 4095;
            static constexpr float32 kPositionStep  = 0.01f;
            static constexpr float32 kRatioStep     = 1.0f / 1023.0f;
            static constexpr float32 kMaxHealth     = 100000.0f;
            static constexpr float32 kHealthStep    = 0.1f;
            static constexpr float32 kMaxTime       = 100000.0f;
            static constexpr float32 kTimeStep      = 0.01f;
            static constexpr int32   kStateMaxValue = static_cast<int32>( MechPilotState::Retired );
            static constexpr int32   kPhaseMaxValue = static_cast<int32>( MatchPhase::Ended );

            static void writePosition( BitWriter& outWriter, const float3& position, float32 halfSize, float32 ceiling )
            {
                outWriter.writeQuantizedFloat( position._x, -halfSize, halfSize, kPositionStep );
                outWriter.writeQuantizedFloat( position._y, 0.0f, ceiling, kPositionStep );
                outWriter.writeQuantizedFloat( position._z, -halfSize, halfSize, kPositionStep );
            }

            static float3 readPosition( BitReader& reader, float32 halfSize, float32 ceiling )
            {
                float3 position;
                position._x = reader.readQuantizedFloat( -halfSize, halfSize, kPositionStep );
                position._y = reader.readQuantizedFloat( 0.0f, ceiling, kPositionStep );
                position._z = reader.readQuantizedFloat( -halfSize, halfSize, kPositionStep );
                return position;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void MechArenaSnapshotCodec::write( const MechArenaSnapshot& snapshot, BitWriter& outWriter )
    {
        using Internal         = MechArenaSnapshotInternal;
        const float32 halfSize = MathUtil::max( 1.0f, snapshot._arenaHalfSize );
        const float32 ceiling  = MathUtil::max( 1.0f, snapshot._ceiling );
        outWriter.writeUint32( Internal::kMagic );
        outWriter.writeUint32( snapshot._tick );
        outWriter.writeFloat( halfSize );
        outWriter.writeFloat( ceiling );
        outWriter.writeInt( static_cast<int32>( snapshot._phase ), 0, Internal::kPhaseMaxValue );
        outWriter.writeInt( snapshot._winningTeam, -1, Internal::kMaxCount );
        outWriter.writeQuantizedFloat( snapshot._remainingTime, 0.0f, Internal::kMaxTime, Internal::kTimeStep );

        const int32 teamCount = MathUtil::min( static_cast<int32>( snapshot._listTeamGauge.size() ), Internal::kMaxCount );
        outWriter.writeInt( teamCount, 0, Internal::kMaxCount );
        for ( int32 index = 0; index < teamCount; ++index )
        {
            outWriter.writeVarInt( snapshot._listTeamGauge[static_cast<size_t>( index )] );
        }

        const int32 pilotCount = MathUtil::min( static_cast<int32>( snapshot._listPilot.size() ), Internal::kMaxCount );
        outWriter.writeInt( pilotCount, 0, Internal::kMaxCount );
        for ( int32 index = 0; index < pilotCount; ++index )
        {
            const MechPilotSnapshot& pilot = snapshot._listPilot[static_cast<size_t>( index )];
            outWriter.writeInt( static_cast<int32>( pilot._state ), 0, Internal::kStateMaxValue );
            outWriter.writeInt( pilot._team, 0, Internal::kMaxCount );
            outWriter.writeInt( pilot._mechIndex, -1, Internal::kMaxCount );
            outWriter.writeInt( pilot._mode, 0, Internal::kMaxCount );
            Internal::writePosition( outWriter, pilot._position, halfSize, ceiling );
            outWriter.writeQuantizedFloat( pilot._health, 0.0f, Internal::kMaxHealth, Internal::kHealthStep );
            outWriter.writeQuantizedFloat( pilot._boostHeat, 0.0f, 1.0f, Internal::kRatioStep );
            outWriter.writeQuantizedFloat( pilot._downRatio, 0.0f, 1.0f, Internal::kRatioStep );
            outWriter.writeInt( pilot._magazineAmmo, 0, Internal::kMaxAmmo );
            outWriter.writeInt( pilot._lockTarget, -1, Internal::kMaxCount );
            outWriter.writeInt( pilot._comboStage, -1, Internal::kMaxCount );
            outWriter.writeVarUint( pilot._activeSkillMask );
            outWriter.writeBool( pilot._bOverheated == SW_TRUE );
            outWriter.writeBool( pilot._bInvulnerable == SW_TRUE );
            outWriter.writeBool( pilot._bReloading == SW_TRUE );
        }

        const int32 projectileCount = MathUtil::min( static_cast<int32>( snapshot._listProjectile.size() ), Internal::kMaxCount );
        outWriter.writeInt( projectileCount, 0, Internal::kMaxCount );
        for ( int32 index = 0; index < projectileCount; ++index )
        {
            const MechProjectileSnapshot& projectile = snapshot._listProjectile[static_cast<size_t>( index )];
            outWriter.writeInt( projectile._owner, -1, Internal::kMaxCount );
            Internal::writePosition( outWriter, projectile._position, halfSize, ceiling );
        }
    }

    bool MechArenaSnapshotCodec::read( BitReader& reader, MechArenaSnapshot& outSnapshot )
    {
        using Internal = MechArenaSnapshotInternal;
        outSnapshot    = MechArenaSnapshot{};
        if ( reader.readUint32() != Internal::kMagic )
            return false;
        outSnapshot._tick          = reader.readUint32();
        outSnapshot._arenaHalfSize = reader.readFloat();
        outSnapshot._ceiling       = reader.readFloat();
        if ( reader.hasOverflowed() || ( outSnapshot._arenaHalfSize >= 1.0f ) == false || ( outSnapshot._ceiling >= 1.0f ) == false )
            return false;
        const float32 halfSize     = outSnapshot._arenaHalfSize;
        const float32 ceiling      = outSnapshot._ceiling;
        outSnapshot._phase         = static_cast<MatchPhase>( reader.readInt( 0, Internal::kPhaseMaxValue ) );
        outSnapshot._winningTeam   = reader.readInt( -1, Internal::kMaxCount );
        outSnapshot._remainingTime = reader.readQuantizedFloat( 0.0f, Internal::kMaxTime, Internal::kTimeStep );

        const int32 teamCount = reader.readInt( 0, Internal::kMaxCount );
        if ( reader.hasOverflowed() )
            return false;
        outSnapshot._listTeamGauge.resize( static_cast<size_t>( teamCount ) );
        for ( int32& gauge : outSnapshot._listTeamGauge )
        {
            gauge = static_cast<int32>( reader.readVarInt() );
        }

        const int32 pilotCount = reader.readInt( 0, Internal::kMaxCount );
        if ( reader.hasOverflowed() )
            return false;
        outSnapshot._listPilot.resize( static_cast<size_t>( pilotCount ) );
        for ( MechPilotSnapshot& pilot : outSnapshot._listPilot )
        {
            pilot._state           = static_cast<MechPilotState>( reader.readInt( 0, Internal::kStateMaxValue ) );
            pilot._team            = reader.readInt( 0, Internal::kMaxCount );
            pilot._mechIndex       = reader.readInt( -1, Internal::kMaxCount );
            pilot._mode            = reader.readInt( 0, Internal::kMaxCount );
            pilot._position        = Internal::readPosition( reader, halfSize, ceiling );
            pilot._health          = reader.readQuantizedFloat( 0.0f, Internal::kMaxHealth, Internal::kHealthStep );
            pilot._boostHeat       = reader.readQuantizedFloat( 0.0f, 1.0f, Internal::kRatioStep );
            pilot._downRatio       = reader.readQuantizedFloat( 0.0f, 1.0f, Internal::kRatioStep );
            pilot._magazineAmmo    = reader.readInt( 0, Internal::kMaxAmmo );
            pilot._lockTarget      = reader.readInt( -1, Internal::kMaxCount );
            pilot._comboStage      = reader.readInt( -1, Internal::kMaxCount );
            pilot._activeSkillMask = static_cast<uint32>( reader.readVarUint() );
            pilot._bOverheated     = reader.readBool() ? SW_TRUE : SW_FALSE;
            pilot._bInvulnerable   = reader.readBool() ? SW_TRUE : SW_FALSE;
            pilot._bReloading      = reader.readBool() ? SW_TRUE : SW_FALSE;
            if ( reader.hasOverflowed() )
                return false;
        }

        const int32 projectileCount = reader.readInt( 0, Internal::kMaxCount );
        if ( reader.hasOverflowed() )
            return false;
        outSnapshot._listProjectile.resize( static_cast<size_t>( projectileCount ) );
        for ( MechProjectileSnapshot& projectile : outSnapshot._listProjectile )
        {
            projectile._owner    = reader.readInt( -1, Internal::kMaxCount );
            projectile._position = Internal::readPosition( reader, halfSize, ceiling );
        }
        return reader.hasOverflowed() == false;
    }
} // namespace sw
