#include "pch.h"

#include "GameFramework/Kits/Feature/World/Overworld/OverworldTileMover.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Feature/World/Overworld/TileMap.h"

namespace sw
{
    SW_LOG_CALLER( "OverworldTileMover" );

    StepEncounterRule OverworldTileMover::makeEncounterRule( const OverworldTileMoverSettings& settings )
    {
        StepEncounterRule rule;
        rule._mode       = StepEncounterMode::Period;
        rule._rate       = settings._encounterRate;
        rule._graceSteps = 0;
        return rule;
    }

    OverworldTileMover::OverworldTileMover()
        : _pTileMap{ nullptr }
        , _pendingWarpMap{}
        , _loco{}
        , _tile{ 1, 1 }
        , _pendingWarpSpawn{ 1, 1 }
        , _settings{}
        , _encounterSteps{}
        , _bMoved{ SW_FALSE }
        , _bWarpPending{ SW_FALSE }
        , _bEncounterPending{ SW_FALSE }
        , _bInteractPending{ SW_FALSE }
        , _bInputEnabled{ SW_TRUE }
        , _reserved{ 0 }
    {
    }

    void OverworldTileMover::setPosition( int32 x, int32 y )
    {
        _tile._x = x;
        _tile._y = y;
    }

    void OverworldTileMover::update( float32 deltaTime, const ControlIntent& intent, int32 interactButton )
    {
        // 걸음과 상호작용이 끝났는지는 **로코모션 한 곳에서만 판정한다.** 여기에 쿨다운을 따로 두거나
        // 걸음을 시작하자마자 `notifyStepFinished()` 로 끝내면 `Walk` 상태가 관측되지 않는다.
        _loco.update( deltaTime );

        if ( _bInputEnabled == SW_FALSE )
            return;
        if ( _loco.canAcceptMoveInput() == false )
            return;

        if ( intent.wasTriggered( interactButton ) )
        {
            _loco.beginInteract();
            _bInteractPending = SW_TRUE;
            return;
        }

        int32         deltaX{ 0 };
        int32         deltaY{ 0 };
        const float2  moveVec  = intent._move;
        const float32 deadZone = _settings._moveDeadZone;
        if ( moveVec._y > deadZone )
            deltaY = -1;
        else if ( moveVec._y < -deadZone )
            deltaY = 1;
        else if ( moveVec._x < -deadZone )
            deltaX = -1;
        else if ( moveVec._x > deadZone )
            deltaX = 1;

        if ( deltaX == 0 && deltaY == 0 )
            return;

        // 걸음이 시작됐으면 그대로 둔다. `_loco.update` 가 `_settings._stepSeconds` 뒤에 끝낸다.
        if ( tryStep( deltaX, deltaY ) == false )
            _loco.setFacingFromDelta( deltaX, deltaY );
    }

    bool OverworldTileMover::consumeMovedFlag()
    {
        const bool v = _bMoved != SW_FALSE;
        _bMoved      = SW_FALSE;
        return v;
    }

    bool OverworldTileMover::consumeWarpRequest( string& outMapPath, int32& outSpawnX, int32& outSpawnY )
    {
        if ( _bWarpPending == SW_FALSE )
            return false;
        outMapPath = _pendingWarpMap;
        outSpawnX  = _pendingWarpSpawn._x;
        outSpawnY  = _pendingWarpSpawn._y;
        _pendingWarpMap.clear();
        _bWarpPending = SW_FALSE;
        return true;
    }

    bool OverworldTileMover::consumeEncounterRequest()
    {
        const bool v       = _bEncounterPending != SW_FALSE;
        _bEncounterPending = SW_FALSE;
        return v;
    }

    bool OverworldTileMover::consumeInteractRequest()
    {
        const bool v      = _bInteractPending != SW_FALSE;
        _bInteractPending = SW_FALSE;
        return v;
    }

    void OverworldTileMover::getFacingTile( int32& outX, int32& outY ) const
    {
        outX = _tile._x;
        outY = _tile._y;
        switch ( _loco.getFacing() )
        {
            case FacingDir::Up:
            {
                --outY;
                break;
            }
            case FacingDir::Down:
            {
                ++outY;
                break;
            }
            case FacingDir::Left:
            {
                --outX;
                break;
            }
            case FacingDir::Right:
            {
                ++outX;
                break;
            }
        }
    }

    void OverworldTileMover::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeInt2( outArchive, _tile );
        _loco.writeState( outArchive );
        outArchive << string_view( _pendingWarpMap );
        StateArchiveUtil::writeInt2( outArchive, _pendingWarpSpawn );
        // 조우 셈은 조우 칸 걸음 수만 싣는다(주기 판정은 그것만 본다, 유예 0). 바이트는 예전 `uint32` 칸 그대로다.
        outArchive << static_cast<uint32>( _encounterSteps.getTotalSteps() );
        outArchive << static_cast<uint8>( _bMoved );
        outArchive << static_cast<uint8>( _bWarpPending );
        outArchive << static_cast<uint8>( _bEncounterPending );
        outArchive << static_cast<uint8>( _bInteractPending );
        outArchive << static_cast<uint8>( _bInputEnabled );
    }

    bool OverworldTileMover::readState( Archive& archive )
    {
        OverworldTileMover restored = *this;
        StateArchiveUtil::readInt2( archive, restored._tile );
        if ( restored._loco.readState( archive ) == false )
            return false;
        archive >> restored._pendingWarpMap;
        StateArchiveUtil::readInt2( archive, restored._pendingWarpSpawn );
        uint32 encounterSteps = 0;
        archive >> encounterSteps;
        if ( encounterSteps > static_cast<uint32>( StepEncounterCounter::kNoEncounterYet ) ||
             restored._encounterSteps.restore( StepEncounterCounter::kNoEncounterYet, static_cast<int32>( encounterSteps ) ) == false )
            return false;
        uint8 arrFlag[5]{ SW_FALSE, SW_FALSE, SW_FALSE, SW_FALSE, SW_FALSE };
        for ( uint8& flag : arrFlag )
        {
            archive >> flag;
            if ( flag > SW_TRUE )
                return false;
        }
        if ( archive.isError() )
            return false;
        restored._bMoved            = arrFlag[0];
        restored._bWarpPending      = arrFlag[1];
        restored._bEncounterPending = arrFlag[2];
        restored._bInteractPending  = arrFlag[3];
        restored._bInputEnabled     = arrFlag[4];
        *this                       = std::move( restored );
        return true;
    }

    bool OverworldTileMover::tryStep( int32 deltaX, int32 deltaY )
    {
        const int32 nextX = _tile._x + deltaX;
        const int32 nextY = _tile._y + deltaY;
        if ( _pTileMap == nullptr || _pTileMap->isWalkable( nextX, nextY ) == false )
            return false;

        _loco.setFacingFromDelta( deltaX, deltaY );
        _loco.notifyStepStarted( _settings._stepSeconds );
        _tile._x = nextX;
        _tile._y = nextY;
        _bMoved  = SW_TRUE;

        const TileWarp* pWarp = _pTileMap->findWarp( _tile._x, _tile._y );
        if ( pWarp != nullptr )
        {
            _pendingWarpMap      = pWarp->_targetMap;
            _pendingWarpSpawn._x = pWarp->_targetTileX;
            _pendingWarpSpawn._y = pWarp->_targetTileY;
            _bWarpPending        = SW_TRUE;
            SW_LOG_TRACE( "Warp trigger → %# @ (%#,%#)", _pendingWarpMap, _pendingWarpSpawn._x, _pendingWarpSpawn._y );
        }
        else if ( _pTileMap->isEncounterTile( _tile._x, _tile._y ) )
        {
            _encounterSteps.advance();
            if ( _encounterSteps.roll( makeEncounterRule( _settings ), nullptr ) )
            {
                _encounterSteps.markEncounter();
                _bEncounterPending = SW_TRUE;
                SW_LOG_TRACE( "Wild encounter at (%#,%#)", _tile._x, _tile._y );
            }
        }
        return true;
    }
} // namespace sw
