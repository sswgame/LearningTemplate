#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/Math/RayMath.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Match/TeamAttitude.h"
#include "GameFramework/Kits/Genre/Action/MechArena/MechArenaSnapshot.h"
#include "GameFramework/Kits/Genre/Action/MechArena/MechArenaWorld.h"

namespace sw
{
    namespace
    {
        struct MechArenaWorldStateInternal
        {

            static void writeInput( Archive& outArchive, const MechInput& input )
            {
                outArchive << input._moveX;
                outArchive << input._moveZ;
                outArchive << input._skillSlot;
                outArchive << input._bDash;
                outArchive << input._bJump;
                outArchive << input._bHover;
                outArchive << input._bFire;
                outArchive << input._bMelee;
                outArchive << input._bSpecial;
                outArchive << input._bTransform;
                outArchive << input._bLockOn;
            }

            [[nodiscard]] static bool readInput( Archive& archive, MechInput& outInput )
            {
                archive >> outInput._moveX;
                archive >> outInput._moveZ;
                archive >> outInput._skillSlot;
                archive >> outInput._bDash;
                archive >> outInput._bJump;
                archive >> outInput._bHover;
                archive >> outInput._bFire;
                archive >> outInput._bMelee;
                archive >> outInput._bSpecial;
                archive >> outInput._bTransform;
                archive >> outInput._bLockOn;
                const uint8 combined = static_cast<uint8>( outInput._bDash | outInput._bJump | outInput._bHover | outInput._bFire | outInput._bMelee |
                                                           outInput._bSpecial | outInput._bTransform | outInput._bLockOn );
                return archive.isOk() && combined <= SW_TRUE;
            }

            static void writePilot( Archive& outArchive, const MechPilot& pilot )
            {
                outArchive << static_cast<uint32>( pilot._listDeckMech.size() );
                for ( const MechDef* pMech : pilot._listDeckMech )
                {
                    StateArchiveUtil::writeName( outArchive, pMech->_id );
                }
                outArchive << static_cast<uint32>( pilot._listSkill.size() );
                for ( const MechSkillDef* pSkill : pilot._listSkill )
                {
                    StateArchiveUtil::writeName( outArchive, pSkill->_id );
                }
                outArchive << pilot._spawnPosition;
                outArchive << pilot._team;
                outArchive << pilot._deckIndex;
                outArchive << pilot._deathSlot;
                outArchive << pilot._mode;
                outArchive << pilot._comboStage;
                outArchive << pilot._meleeSlot;
                outArchive << pilot._kills;
                outArchive << pilot._deaths;
                outArchive << static_cast<uint8>( pilot._state );
                outArchive << pilot._bGrounded;
                outArchive << pilot._bMeleeHit;
                outArchive << pilot._bMeleeQueued;
                outArchive << pilot._bWasOverheated;
                outArchive << static_cast<uint32>( pilot._listParticipant.size() );
                for ( const int32 participant : pilot._listParticipant )
                {
                    outArchive << participant;
                }
                outArchive << pilot._position;
                outArchive << pilot._velocity;
                outArchive << pilot._forward;
                outArchive << pilot._dashDirection;
                StateArchiveUtil::writeCountdown( outArchive, pilot._dashRemaining );
                StateArchiveUtil::writeCountdown( outArchive, pilot._staggerRemaining );
                StateArchiveUtil::writeCountdown( outArchive, pilot._transformCooldown );
                for ( size_t index = 0; index < pilot._listSkill.size(); ++index )
                {
                    StateArchiveUtil::writeCountdown( outArchive, pilot._listSkillRemaining[index] );
                    StateArchiveUtil::writeCountdown( outArchive, pilot._listSkillCooldown[index] );
                    outArchive << pilot._listSkillSpent[index];
                }
                writeInput( outArchive, pilot._input );
                writeInput( outArchive, pilot._previousInput );
                pilot._vitality.writeState( outArchive );
                pilot._boost.writeState( outArchive );
                pilot._lockOn.writeState( outArchive );
                pilot._melee.writeState( outArchive );
                outArchive << static_cast<uint32>( pilot._listWeaponState.size() );
                for ( size_t index = 0; index < pilot._listWeaponState.size(); ++index )
                {
                    pilot._listWeaponState[index].writeState( outArchive );
                    StateArchiveUtil::writeCountdown( outArchive, pilot._listSpecialCooldown[index] );
                }
            }
        };
    } // namespace

    // --- 직렬화 -----------------------------------------------------------------------------------

    void MechArenaWorld::makeSnapshot( MechArenaSnapshot& outSnapshot ) const
    {
        outSnapshot                = MechArenaSnapshot{};
        outSnapshot._tick          = _tick;
        outSnapshot._phase         = _match.getPhase();
        outSnapshot._winningTeam   = _match.getWinningTeam();
        outSnapshot._remainingTime = _match.getRemaining();
        outSnapshot._arenaHalfSize = _settings._arenaHalfSize;
        outSnapshot._ceiling       = _settings._ceiling;
        for ( int32 team = 0; team < static_cast<int32>( _listTeamName.size() ); ++team )
        {
            outSnapshot._listTeamGauge.push_back( getTeamGauge( team ) );
        }
        for ( int32 index = 0; index < getPilotCount(); ++index )
        {
            const MechPilot&  pilot = _listPilot[static_cast<size_t>( index )];
            const MechDef*    pMech = pilot.getMech();
            MechPilotSnapshot entry;
            entry._position      = pilot._position;
            entry._health        = pilot._vitality.getHealth();
            entry._boostHeat     = pilot._boost.getRatio();
            entry._downRatio     = pMech->_downMax > 0.0f ? MathUtil::saturate( 1.0f - pilot._vitality.getPoise() / pMech->_downMax ) : 0.0f;
            entry._team          = pilot._team;
            entry._mechIndex     = _pMechCatalog != nullptr ? _pMechCatalog->findMechIndex( pMech->_id ) : -1;
            entry._mode          = pilot._mode;
            entry._lockTarget    = pilot._lockOn.hasTarget() ? static_cast<int32>( pilot._lockOn.getTarget() ) - 1 : -1;
            entry._comboStage    = pilot._comboStage;
            entry._state         = pilot._state;
            entry._bOverheated   = pilot._boost.isOverheated() ? SW_TRUE : SW_FALSE;
            entry._bInvulnerable = pilot._vitality.isInvulnerable() ? SW_TRUE : SW_FALSE;
            const int32 shot     = findSlot( pilot, MechWeaponKind::Shot );
            if ( shot >= 0 )
            {
                entry._magazineAmmo = pilot._listWeaponState[static_cast<size_t>( shot )].getMagazineAmmo();
                entry._bReloading   = pilot._listWeaponState[static_cast<size_t>( shot )].isReloading() ? SW_TRUE : SW_FALSE;
            }
            const int32 skillCount = MathUtil::min( countActiveSkills( pilot ), 32 );
            for ( int32 skill = 0; skill < skillCount; ++skill )
            {
                entry._activeSkillMask |= pilot._listSkillRemaining[static_cast<size_t>( skill )].isActive() ? ( 1u << static_cast<uint32>( skill ) ) : 0u;
            }
            outSnapshot._listPilot.push_back( entry );
        }
        for ( const MechProjectile& projectile : _listProjectile )
        {
            MechProjectileSnapshot entry;
            entry._position = projectile._position;
            entry._owner    = projectile._owner;
            outSnapshot._listProjectile.push_back( entry );
        }
    }

    void MechArenaWorld::writeState( BitWriter& outWriter ) const
    {
        MechArenaSnapshot snapshot;
        makeSnapshot( snapshot );
        MechArenaSnapshotCodec::write( snapshot, outWriter );
    }

    void MechArenaWorld::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listTeamName.size() );
        for ( const hashed_string& name : _listTeamName )
        {
            StateArchiveUtil::writeName( outArchive, name );
        }
        outArchive << _bStarted;
        outArchive << _bEndReported;
        outArchive << _tick;
        StateArchiveUtil::writeStepTimer( outArchive, _timer );
        _match.writeState( outArchive );
        outArchive << static_cast<uint32>( _listPilot.size() );
        for ( const MechPilot& pilot : _listPilot )
        {
            MechArenaWorldStateInternal::writePilot( outArchive, pilot );
        }
        outArchive << static_cast<uint32>( _listProjectile.size() );
        for ( const MechProjectile& projectile : _listProjectile )
        {
            outArchive << projectile._position;
            outArchive << projectile._direction;
            outArchive << projectile._speed;
            outArchive << projectile._homing;
            outArchive << projectile._rangeLeft;
            outArchive << projectile._damage;
            outArchive << projectile._downValue;
            outArchive << projectile._knockback;
            outArchive << projectile._staggerTime;
            outArchive << projectile._owner;
            outArchive << projectile._target;
            outArchive << static_cast<uint8>( projectile._kind );
        }
    }

    bool MechArenaWorld::readState( Archive& archive )
    {
        if ( _pMechCatalog == nullptr || _pMoveCatalog == nullptr )
            return false;
        // 사본에 읽고 끝까지 맞으면 바꾼다. 설정과 카탈로그는 사본이 그대로 가지고 있다.
        MechArenaWorld restored  = *this;
        uint32         teamCount = 0;
        if ( StateArchiveUtil::readCount( archive, 4, teamCount ) == false )
            return false;
        restored._listTeamName.assign( teamCount, hashed_string{} );
        for ( hashed_string& name : restored._listTeamName )
        {
            if ( StateArchiveUtil::readName( archive, name ) == false )
                return false;
        }
        archive >> restored._bStarted;
        archive >> restored._bEndReported;
        archive >> restored._tick;
        const bool bHeadValid = archive.isOk() && restored._bStarted <= SW_TRUE && restored._bEndReported <= SW_TRUE;
        if ( bHeadValid == false || StateArchiveUtil::readStepTimer( archive, restored._timer ) == false )
            return false;
        restored._match.initialize( restored._bStarted == SW_TRUE ? makeMatchSettings() : MatchSettings{} );
        if ( restored._match.readState( archive ) == false )
            return false;

        uint32 pilotCount = 0;
        // 조종사마다 적어도 덱 개수(4) + 기체 이름(4) + 스킬 개수(4) + 위치, 정수, 상태 슬롯(60) 바이트
        if ( StateArchiveUtil::readCount( archive, 72, pilotCount ) == false )
            return false;
        restored._listPilot.assign( pilotCount, MechPilot{} );
        for ( uint32 index = 0; index < pilotCount; ++index )
        {
            if ( restored.readPilot( archive, static_cast<int32>( index ), restored._listPilot[index] ) == false )
                return false;
        }

        uint32 projectileCount = 0;
        // 탄마다 위치(12) + 방향(12) + 실수 7개(28) + 쏜 쪽과 대상(8) + 종류(1) 바이트
        if ( StateArchiveUtil::readCount( archive, 61, projectileCount ) == false )
            return false;
        restored._listProjectile.assign( projectileCount, MechProjectile{} );
        for ( MechProjectile& projectile : restored._listProjectile )
        {
            uint8 kind = 0;
            archive >> projectile._position;
            archive >> projectile._direction;
            archive >> projectile._speed;
            archive >> projectile._homing;
            archive >> projectile._rangeLeft;
            archive >> projectile._damage;
            archive >> projectile._downValue;
            archive >> projectile._knockback;
            archive >> projectile._staggerTime;
            archive >> projectile._owner;
            archive >> projectile._target;
            archive >> kind;
            const bool bValid = archive.isOk() && -1 <= projectile._owner && projectile._owner < static_cast<int32>( pilotCount ) && -1 <= projectile._target &&
                                projectile._target < static_cast<int32>( pilotCount ) && kind <= static_cast<uint8>( MechWeaponKind::Special );
            if ( bValid == false )
                return false;
            projectile._kind = static_cast<MechWeaponKind>( kind );
        }
        restored._eventBuffer.clear();
        *this = std::move( restored );
        return true;
    }

    MatchSettings MechArenaWorld::makeMatchSettings() const
    {
        MatchSettings matchSettings;
        matchSettings._respawnDelay = _settings._respawnDelay;
        matchSettings._timeLimit    = _settings._timeLimit;
        matchSettings._scorePerKill = 1;
        return matchSettings;
    }

    bool MechArenaWorld::readPilot( Archive& archive, int32 pilotIndex, MechPilot& outPilot )
    {
        uint32 deckCount = 0;
        if ( StateArchiveUtil::readCount( archive, 4, deckCount ) == false || deckCount == 0 )
            return false;
        for ( uint32 index = 0; index < deckCount; ++index )
        {
            hashed_string mechID;
            if ( StateArchiveUtil::readName( archive, mechID ) == false )
                return false;
            const MechDef* pMech = _pMechCatalog->findMech( mechID );
            if ( pMech == nullptr || pMech->_listMode.empty() )
                return false;
            outPilot._listDeckMech.push_back( pMech );
        }
        uint32 skillCount = 0;
        if ( StateArchiveUtil::readCount( archive, 4, skillCount ) == false )
            return false;
        for ( uint32 index = 0; index < skillCount; ++index )
        {
            hashed_string skillID;
            if ( StateArchiveUtil::readName( archive, skillID ) == false )
                return false;
            const MechSkillDef* pSkill = _pMechCatalog->findSkill( skillID );
            if ( pSkill == nullptr )
                return false;
            outPilot._listSkill.push_back( pSkill );
        }
        outPilot._listSkillRemaining.resize( skillCount );
        outPilot._listSkillCooldown.resize( skillCount );
        outPilot._listSkillSpent.resize( skillCount, SW_FALSE );

        int32 mode           = 0;
        uint8 state          = 0;
        uint8 bWasOverheated = SW_FALSE;
        archive >> outPilot._spawnPosition;
        archive >> outPilot._team;
        archive >> outPilot._deckIndex;
        archive >> outPilot._deathSlot;
        archive >> mode;
        archive >> outPilot._comboStage;
        archive >> outPilot._meleeSlot;
        archive >> outPilot._kills;
        archive >> outPilot._deaths;
        archive >> state;
        archive >> outPilot._bGrounded;
        archive >> outPilot._bMeleeHit;
        archive >> outPilot._bMeleeQueued;
        archive >> bWasOverheated;
        const int32 deckSize   = static_cast<int32>( deckCount );
        const uint8 flags      = static_cast<uint8>( outPilot._bGrounded | outPilot._bMeleeHit | outPilot._bMeleeQueued | bWasOverheated );
        const bool  bHeadValid = archive.isOk() && 0 <= outPilot._team && outPilot._team < static_cast<int32>( _listTeamName.size() ) && 0 <= outPilot._deckIndex &&
                                outPilot._deckIndex < deckSize && -1 <= outPilot._deathSlot && outPilot._deathSlot < deckSize && -1 <= outPilot._comboStage &&
                                state <= static_cast<uint8>( MechPilotState::Retired ) && flags <= SW_TRUE;
        if ( bHeadValid == false )
            return false;
        outPilot._state = static_cast<MechPilotState>( state );

        // 무기, 체력, 부스트 설정은 지금 장착한 기체가 정한다. 격추된 동안은 격추된 슬롯의 기체를 쓴다(교체한 슬롯은 부활할 때 장착된다).
        const int32 deckIndex = outPilot._deckIndex;
        outPilot._deckIndex   = outPilot._deathSlot >= 0 ? outPilot._deathSlot : deckIndex;
        const MechDef* pMech  = outPilot.getMech();
        equipMech( outPilot, pilotIndex );
        outPilot._deckIndex      = deckIndex;
        outPilot._bWasOverheated = bWasOverheated;
        outPilot._mode           = mode;
        if ( mode < 0 || mode >= static_cast<int32>( pMech->_listMode.size() ) )
            return false;

        uint32 participantCount = 0;
        if ( StateArchiveUtil::readCount( archive, 4, participantCount ) == false || ( participantCount != 0 && participantCount != deckCount ) )
            return false;
        outPilot._listParticipant.assign( participantCount, -1 );
        for ( int32& participant : outPilot._listParticipant )
        {
            archive >> participant;
        }
        archive >> outPilot._position;
        archive >> outPilot._velocity;
        archive >> outPilot._forward;
        archive >> outPilot._dashDirection;
        const bool bTimerRead = StateArchiveUtil::readCountdown( archive, outPilot._dashRemaining ) &&
                                StateArchiveUtil::readCountdown( archive, outPilot._staggerRemaining ) &&
                                StateArchiveUtil::readCountdown( archive, outPilot._transformCooldown );
        if ( bTimerRead == false )
            return false;
        for ( uint32 index = 0; index < skillCount; ++index )
        {
            const bool bSkillRead = StateArchiveUtil::readCountdown( archive, outPilot._listSkillRemaining[index] ) &&
                                    StateArchiveUtil::readCountdown( archive, outPilot._listSkillCooldown[index] );
            archive >> outPilot._listSkillSpent[index];
            if ( bSkillRead == false || archive.isError() || outPilot._listSkillSpent[index] > SW_TRUE )
                return false;
        }
        const bool bBodyRead = MechArenaWorldStateInternal::readInput( archive, outPilot._input ) && MechArenaWorldStateInternal::readInput( archive, outPilot._previousInput ) &&
                               outPilot._vitality.readState( archive ) && outPilot._boost.readState( archive ) && outPilot._lockOn.readState( archive ) &&
                               outPilot._melee.readState( archive, *_pMoveCatalog );
        if ( bBodyRead == false )
            return false;

        uint32 weaponCount = 0;
        archive >> weaponCount;
        if ( archive.isError() || weaponCount != static_cast<uint32>( outPilot._listWeaponState.size() ) || outPilot._meleeSlot >= static_cast<int32>( weaponCount ) ||
             outPilot._meleeSlot < -1 )
            return false;
        for ( uint32 index = 0; index < weaponCount; ++index )
        {
            if ( outPilot._listWeaponState[index].readState( archive ) == false ||
                 StateArchiveUtil::readCountdown( archive, outPilot._listSpecialCooldown[index] ) == false )
                return false;
        }
        return true;
    }
} // namespace sw
