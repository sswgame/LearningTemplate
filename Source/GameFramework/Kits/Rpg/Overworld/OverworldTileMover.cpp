#include "pch.h"

#include "GameFramework/Kits/Rpg/Overworld/OverworldTileMover.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Control/Intent/ControlIntent.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Rpg/Overworld/TileMap.h"

namespace sw
{
    SW_LOG_CALLER( "OverworldTileMover" );

    SW_GF_API bool shouldEncounterOnStep( float32 encounterRate, uint32 stepCount )
    {
        // 0 이면 **안 난다** — 야생 조우를 끄려고 `setEncounterRate( 0 )` 을 부르는 자리다.
        if ( encounterRate <= 0.0f )
            return false;
        // 1 이상이면 매 걸음이다. 1/rate 를 uint32 로 자르면 그 구간이 통째로 0 이 돼
        // "주기 0" 이 된다. 그것을 다른 주기로 바꾸면 **높은 확률이 낮은 빈도**가 된다.
        if ( encounterRate >= 1.0f )
            return true;

        const uint32 period = static_cast<uint32>( 1.0f / encounterRate );
        return period <= 1u || ( stepCount % period ) == 0u;
    }

    OverworldTileMover::OverworldTileMover()
        : _pTileMap{ nullptr }
        , _pendingWarpMap{}
        , _loco{}
        , _tile{ 1, 1 }
        , _pendingWarpSpawn{ 1, 1 }
        , _settings{}
        , _encounterStepCounter{ 0 }
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
        // 걸음·상호작용이 끝나는 것은 **로코모션 하나가 판정한다.** 여기에 쿨다운을 따로 두거나
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

        // 걸음이 시작됐으면 그대로 둔다. `_loco.update` 가 `kStepDuration` 뒤에 끝낸다.
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
        outArchive << _encounterStepCounter;
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
        archive >> restored._encounterStepCounter;
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
        _loco.notifyStepStarted();
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
            ++_encounterStepCounter;
            if ( shouldEncounterOnStep( _settings._encounterRate, _encounterStepCounter ) )
            {
                _bEncounterPending = SW_TRUE;
                SW_LOG_TRACE( "Wild encounter at (%#,%#)", _tile._x, _tile._y );
            }
        }
        return true;
    }
} // namespace sw
