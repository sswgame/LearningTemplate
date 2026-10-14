#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/AI/AIPerception.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorRules.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/HorrorMatch.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/HorrorSnapshot.h"

namespace sw
{
    void HorrorMatch::makeSnapshot( HorrorSnapshot& outSnapshot ) const
    {
        outSnapshot                    = HorrorSnapshot{};
        outSnapshot._tick              = _tick;
        outSnapshot._phase             = _match.getPhase();
        outSnapshot._killerPosition    = _killer._position;
        outSnapshot._killerStun        = _killer._stunRemaining.getRemaining();
        outSnapshot._killerCooldown    = _killer._attackCooldown.getRemaining();
        outSnapshot._killerCarrying    = _killer._carrying;
        outSnapshot._collapseRemaining = _collapseRemaining.getRemaining();
        outSnapshot._bGatesPowered     = _bGatesPowered;
        outSnapshot._bHatchOpen        = _bHatchOpen;
        outSnapshot._bCollapseStarted  = _bCollapseStarted;
        for ( const HorrorSurvivor& survivor : _listSurvivor )
        {
            HorrorSurvivorSnapshot entry;
            entry._position       = survivor._position;
            entry._hookTimer      = survivor._hookTimer;
            entry._healProgress   = survivor._healing.getProgress();
            entry._bleedout       = survivor._vitality.isDowned() ? survivor._vitality.getDownedHealth() : 0.0f;
            entry._wiggleProgress = survivor._wiggleProgress;
            entry._hookStage      = survivor._hookStage;
            entry._state          = survivor._state;
            entry._activity       = survivor._activity;
            outSnapshot._listSurvivor.push_back( entry );
        }
        for ( const HorrorGenerator& generator : _listGenerator )
        {
            uint8 flag = 0;
            flag |= generator._progress.isCompleted() ? 1u : 0u;
            flag |= generator._bKicked == SW_TRUE ? 2u : 0u;
            flag |= generator._bBlocked == SW_TRUE ? 4u : 0u;
            outSnapshot._listGeneratorProgress.push_back( generator._progress.getProgress() );
            outSnapshot._listGeneratorFlag.push_back( flag );
        }
        for ( const HorrorGate& gate : _listGate )
        {
            outSnapshot._listGateProgress.push_back( gate._progress.getProgress() );
        }
        outSnapshot._listPalletState = _listPalletState;
        for ( const HorrorWindow& window : _listWindow )
        {
            outSnapshot._listWindowBlocked.push_back( window._blockedRemaining > 0.0f ? SW_TRUE : SW_FALSE );
        }
    }

    void HorrorMatch::writeState( BitWriter& outWriter ) const
    {
        HorrorSnapshot snapshot;
        makeSnapshot( snapshot );
        HorrorSnapshotCodec::write( snapshot, outWriter );
    }

    void HorrorMatch::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeName( outArchive, _settings._killerID );
        outArchive << _bStarted;
        outArchive << _bEndReported;
        outArchive << _tick;
        StateArchiveUtil::writeStepTimer( outArchive, _timer );
        _match.writeState( outArchive );
        outArchive << _survivorTeam;
        outArchive << _killerTeam;
        outArchive << _completedGeneratorCount;
        outArchive << _bGatesPowered;
        outArchive << _bHatchPlaced;
        outArchive << _bHatchOpen;
        outArchive << _bHatchClosed;
        outArchive << _bCollapseStarted;
        StateArchiveUtil::writeCountdown( outArchive, _collapseRemaining );
        outArchive << _hatchPosition;

        // 무대: 위치를 먼저 싣고(읽는 쪽이 add... 로 다시 만든다) 그 위에 상태를 덮는다.
        outArchive << static_cast<uint32>( _listHook.size() );
        for ( const float3& position : _listHook )
        {
            outArchive << position;
        }
        outArchive << static_cast<uint32>( _listPallet.size() );
        for ( size_t index = 0; index < _listPallet.size(); ++index )
        {
            outArchive << _listPallet[index];
            outArchive << static_cast<uint8>( _listPalletState[index] );
        }
        outArchive << static_cast<uint32>( _listLocker.size() );
        for ( size_t index = 0; index < _listLocker.size(); ++index )
        {
            outArchive << _listLocker[index];
            outArchive << _listLockerOccupant[index];
        }
        outArchive << static_cast<uint32>( _listWindow.size() );
        for ( const HorrorWindow& window : _listWindow )
        {
            outArchive << window._position;
            outArchive << window._blockedRemaining;
            outArchive << window._chaseVaultCount;
        }
        outArchive << static_cast<uint32>( _listGenerator.size() );
        for ( const HorrorGenerator& generator : _listGenerator )
        {
            outArchive << generator._position;
            outArchive << generator._bKicked;
            outArchive << generator._bBlocked;
            generator._progress.writeState( outArchive );
        }
        outArchive << static_cast<uint32>( _listGate.size() );
        for ( const HorrorGate& gate : _listGate )
        {
            outArchive << gate._position;
            gate._progress.writeState( outArchive );
        }

        outArchive << static_cast<uint32>( _listSurvivor.size() );
        for ( const HorrorSurvivor& survivor : _listSurvivor )
        {
            outArchive << survivor._position;
            survivor._vitality.writeState( outArchive );
            survivor._healing.writeState( outArchive );
            survivor._score.writeState( outArchive );
            outArchive << survivor._lastMoveSpeed;
            StateArchiveUtil::writeCountdown( outArchive, survivor._hasteRemaining );
            outArchive << survivor._hookTimer;
            outArchive << survivor._struggleIdle;
            outArchive << survivor._wiggleProgress;
            StateArchiveUtil::writeCountdown( outArchive, survivor._vaultRemaining );
            outArchive << survivor._noiseRadius;
            StateArchiveUtil::writeCountdown( outArchive, survivor._noiseRemaining );
            outArchive << survivor._vaultExit;
            outArchive << survivor._hookStage;
            outArchive << survivor._activityTarget;
            outArchive << survivor._participant;
            outArchive << static_cast<uint8>( survivor._state );
            outArchive << static_cast<uint8>( survivor._activity );
            outArchive << survivor._bStruggling;
            outArchive << survivor._bWiggling;
            outArchive << survivor._bBledOut;
        }

        _killer._score.writeState( outArchive );
        outArchive << _killer._position;
        outArchive << _killer._forward;
        outArchive << _killer._busyExit;
        StateArchiveUtil::writeCountdown( outArchive, _killer._attackCooldown );
        StateArchiveUtil::writeCountdown( outArchive, _killer._stunRemaining );
        StateArchiveUtil::writeCountdown( outArchive, _killer._busyRemaining );
        StateArchiveUtil::writeCountdown( outArchive, _killer._abilityCooldown );
        outArchive << _killer._carrying;
        outArchive << _killer._breakingPallet;
        outArchive << _killer._participant;
        outArchive << _killer._bVaulting;
    }

    bool HorrorMatch::readState( Archive& archive )
    {
        if ( _pCatalog == nullptr || _killer._pDef == nullptr )
            return false;
        hashed_string killerID;
        if ( StateArchiveUtil::readName( archive, killerID ) == false || killerID != _settings._killerID )
            return false;
        // 같은 설정과 카탈로그로 새 판을 열고, 무대와 생존자를 add... 로 다시 만든 뒤(규칙에서 오는 설정) 상태를 덮는다. 끝까지 맞을 때만 바꾼다.
        HorrorMatch restored;
        if ( restored.initialize( _settings, _pCatalog ) == false )
            return false;
        uint8 bStarted = SW_FALSE;
        archive >> bStarted;
        archive >> restored._bEndReported;
        archive >> restored._tick;
        const bool bHeadValid = archive.isOk() && bStarted <= SW_TRUE && restored._bEndReported <= SW_TRUE;
        if ( bHeadValid == false || StateArchiveUtil::readStepTimer( archive, restored._timer ) == false )
            return false;
        restored._match.initialize( bStarted == SW_TRUE ? makeMatchSettings() : MatchSettings{} );
        if ( restored._match.readState( archive ) == false )
            return false;
        archive >> restored._survivorTeam;
        archive >> restored._killerTeam;
        archive >> restored._completedGeneratorCount;
        archive >> restored._bGatesPowered;
        archive >> restored._bHatchPlaced;
        archive >> restored._bHatchOpen;
        archive >> restored._bHatchClosed;
        archive >> restored._bCollapseStarted;
        const bool bCollapseRead = StateArchiveUtil::readCountdown( archive, restored._collapseRemaining );
        archive >> restored._hatchPosition;
        const uint8 flags =
            static_cast<uint8>( restored._bGatesPowered | restored._bHatchPlaced | restored._bHatchOpen | restored._bHatchClosed | restored._bCollapseStarted );
        const bool bInvalidRestoredFlags = bCollapseRead == false || archive.isError() || flags > SW_TRUE || restored._completedGeneratorCount < 0;
        if ( bInvalidRestoredFlags )
            return false;

        uint32 hookCount = 0;
        if ( StateArchiveUtil::readCount( archive, 12, hookCount ) == false )
            return false;
        for ( uint32 index = 0; index < hookCount; ++index )
        {
            float3 position;
            archive >> position;
            (void)restored.addHook( position );
        }
        uint32 palletCount = 0;
        if ( StateArchiveUtil::readCount( archive, 13, palletCount ) == false )
            return false;
        for ( uint32 index = 0; index < palletCount; ++index )
        {
            float3 position;
            uint8  state = 0;
            archive >> position;
            archive >> state;
            if ( archive.isError() || state > static_cast<uint8>( PalletState::Broken ) )
                return false;
            restored._listPalletState[static_cast<size_t>( restored.addPallet( position ) )] = static_cast<PalletState>( state );
        }
        uint32 lockerCount = 0;
        if ( StateArchiveUtil::readCount( archive, 16, lockerCount ) == false )
            return false;
        for ( uint32 index = 0; index < lockerCount; ++index )
        {
            float3 position;
            int32  occupant = -1;
            archive >> position;
            archive >> occupant;
            restored._listLockerOccupant[static_cast<size_t>( restored.addLocker( position ) )] = occupant;
        }
        uint32 windowCount = 0;
        if ( StateArchiveUtil::readCount( archive, 20, windowCount ) == false )
            return false;
        for ( uint32 index = 0; index < windowCount; ++index )
        {
            float3 position;
            archive >> position;
            HorrorWindow& window = restored._listWindow[static_cast<size_t>( restored.addWindow( position ) )];
            archive >> window._blockedRemaining;
            archive >> window._chaseVaultCount;
        }
        uint32 generatorCount = 0;
        if ( StateArchiveUtil::readCount( archive, 14, generatorCount ) == false )
            return false;
        for ( uint32 index = 0; index < generatorCount; ++index )
        {
            float3 position;
            uint8  bKicked  = SW_FALSE;
            uint8  bBlocked = SW_FALSE;
            archive >> position;
            archive >> bKicked;
            archive >> bBlocked;
            const bool bInvalidGeneratorFlags = archive.isError() || bKicked > SW_TRUE || bBlocked > SW_TRUE;
            if ( bInvalidGeneratorFlags )
                return false;
            const int32 generator = restored.addGenerator( position );
            restored.configureGenerator( generator, bKicked == SW_TRUE ); // 걷어차인 발전기는 퇴행 규칙을 쓴다
            HorrorGenerator& target = restored._listGenerator[static_cast<size_t>( generator )];
            target._bBlocked        = bBlocked;
            if ( target._progress.readState( archive ) == false )
                return false;
        }
        uint32 gateCount = 0;
        if ( StateArchiveUtil::readCount( archive, 12, gateCount ) == false )
            return false;
        for ( uint32 index = 0; index < gateCount; ++index )
        {
            float3 position;
            archive >> position;
            if ( archive.isError() )
                return false;
            const int32 gate = restored.addGate( position );
            if ( restored._listGate[static_cast<size_t>( gate )]._progress.readState( archive ) == false )
                return false;
        }

        uint32 survivorCount = 0;
        // 생존자마다 최소한 위치(12) + 실수, 타이머, 정수 필드(60) + 체력, 치료, 점수
        if ( StateArchiveUtil::readCount( archive, 72, survivorCount ) == false )
            return false;
        for ( uint32 index = 0; index < survivorCount; ++index )
        {
            float3 position;
            archive >> position;
            if ( archive.isError() )
                return false;
            const int32 survivor = restored.addSurvivor( position );
            if ( survivor < 0 || restored.readSurvivor( archive, restored._listSurvivor[static_cast<size_t>( survivor )] ) == false )
                return false;
        }
        for ( const int32 occupant : restored._listLockerOccupant )
        {
            if ( occupant < -1 || occupant >= static_cast<int32>( survivorCount ) )
                return false;
        }

        HorrorKiller& killer = restored._killer;
        if ( killer._score.readState( archive ) == false )
            return false;
        archive >> killer._position;
        archive >> killer._forward;
        archive >> killer._busyExit;
        const bool bKillerTimerRead = StateArchiveUtil::readCountdown( archive, killer._attackCooldown ) &&
                                      StateArchiveUtil::readCountdown( archive, killer._stunRemaining ) &&
                                      StateArchiveUtil::readCountdown( archive, killer._busyRemaining ) &&
                                      StateArchiveUtil::readCountdown( archive, killer._abilityCooldown );
        archive >> killer._carrying;
        archive >> killer._breakingPallet;
        archive >> killer._participant;
        archive >> killer._bVaulting;
        const bool bKillerValid = bKillerTimerRead && archive.isOk() && -1 <= killer._carrying && killer._carrying < static_cast<int32>( survivorCount ) &&
                                -1 <= killer._breakingPallet && killer._breakingPallet < static_cast<int32>( palletCount ) && killer._bVaulting <= SW_TRUE;
        if ( bKillerValid == false )
            return false;

        restored._bStarted = bStarted;
        restored._eventBuffer.clear();
        *this = std::move( restored );
        return true;
    }

    MatchSettings HorrorMatch::makeMatchSettings() const
    {
        MatchSettings settings;
        settings._bRespawn = SW_FALSE;
        return settings;
    }

    bool HorrorMatch::readSurvivor( Archive& archive, HorrorSurvivor& outSurvivor ) const
    {
        uint8      state                    = 0;
        uint8      activity                 = 0;
        const bool bSurvivorStateReadFailed = outSurvivor._vitality.readState( archive ) == false || outSurvivor._healing.readState( archive ) == false || outSurvivor._score.readState( archive ) == false;
        if ( bSurvivorStateReadFailed )
            return false;
        archive >> outSurvivor._lastMoveSpeed;
        const bool bHasteRead = StateArchiveUtil::readCountdown( archive, outSurvivor._hasteRemaining );
        archive >> outSurvivor._hookTimer;
        archive >> outSurvivor._struggleIdle;
        archive >> outSurvivor._wiggleProgress;
        const bool bVaultRead = StateArchiveUtil::readCountdown( archive, outSurvivor._vaultRemaining );
        archive >> outSurvivor._noiseRadius;
        const bool bNoiseRead = StateArchiveUtil::readCountdown( archive, outSurvivor._noiseRemaining );
        archive >> outSurvivor._vaultExit;
        archive >> outSurvivor._hookStage;
        archive >> outSurvivor._activityTarget;
        archive >> outSurvivor._participant;
        archive >> state;
        archive >> activity;
        archive >> outSurvivor._bStruggling;
        archive >> outSurvivor._bWiggling;
        archive >> outSurvivor._bBledOut;
        const uint8 flags  = static_cast<uint8>( outSurvivor._bStruggling | outSurvivor._bWiggling | outSurvivor._bBledOut );
        const bool  bValid = bHasteRead && bVaultRead && bNoiseRead && archive.isOk() && state <= static_cast<uint8>( SurvivorState::Escaped ) &&
                            activity <= static_cast<uint8>( SurvivorActivity::InLocker ) && 0 <= outSurvivor._hookStage && -1 <= outSurvivor._activityTarget &&
                            flags <= SW_TRUE;
        if ( bValid == false )
            return false;
        outSurvivor._state    = static_cast<SurvivorState>( state );
        outSurvivor._activity = static_cast<SurvivorActivity>( activity );
        return true;
    }
} // namespace sw
