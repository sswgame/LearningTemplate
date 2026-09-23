#include "pch.h"

#include "GameFramework/Kits/Overworld/PlayerController.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/InputManager.h"

#include "GameFramework/Kits/Overworld/TileMap.h"

namespace sw
{
    SW_LOG_CALLER( "PlayerController" );

    SW_GF_API bool shouldEncounterOnStep( float32 encounterRate, uint32 stepCount )
    {
        // 0 이면 **안 난다.** 예전에는 `rate > 0.01f` 가 거짓일 때 주기를 3 으로 놓아서,
        // 야생 조우를 끄려고 `setEncounterRate( 0 )` 을 부르면 오히려 **세 걸음마다** 났다.
        if ( encounterRate <= 0.0f )
            return false;
        // 1 이상이면 매 걸음이다. 1/rate 를 uint32 로 자르면 그 구간이 통째로 0 이 돼
        // "주기 0" 이 되고, 예전 코드는 그것을 다시 3 으로 바꿔 **높은 확률이 낮은 빈도**가 됐다.
        if ( encounterRate >= 1.0f )
            return true;

        const uint32 period = static_cast<uint32>( 1.0f / encounterRate );
        return period <= 1u || ( stepCount % period ) == 0u;
    }

    PlayerController::PlayerController()
        : _pTileMap{ nullptr }
        , _pActionMap{ nullptr }
        , _pendingWarpMap{}
        , _loco{}
        , _tile{ 1, 1 }
        , _pendingWarpSpawn{ 1, 1 }
        , _encounterStepCounter{ 0 }
        , _encounterRate{ 0.33f }
        , _bMoved{ SW_FALSE }
        , _bWarpPending{ SW_FALSE }
        , _bEncounterPending{ SW_FALSE }
        , _bInteractPending{ SW_FALSE }
        , _bInputEnabled{ SW_TRUE }
        , _reserved{ 0 }
    {
    }

    void PlayerController::setPosition( int32 x, int32 y )
    {
        _tile._x = x;
        _tile._y = y;
    }

    void PlayerController::update( float32 deltaTime, InputManager& input )
    {
        // 걸음·상호작용이 끝나는 것은 **로코모션 하나가 판정한다.** 예전에는 여기에 같은
        // 길이의 `_stepCooldown` 이 따로 있었고, 걸음을 시작하자마자 `notifyStepFinished()`
        // 로 취소해 버려서 실제 잠금은 그 쿨다운이 하고 `Walk` 는 죽은 상태였다.
        _loco.update( deltaTime );

        if ( _bInputEnabled == SW_FALSE )
            return;
        if ( _loco.canAcceptMoveInput() == false )
            return;

        ActionMap* pActionMap = _pActionMap != nullptr ? _pActionMap : &input.getActionMap();
        if ( pActionMap->getInputManager() != &input )
            pActionMap->setInputManager( &input );

        if ( pActionMap->wasActionTriggered( "Interact" ) )
        {
            _loco.beginInteract();
            _bInteractPending = SW_TRUE;
            return;
        }

        int32        deltaX{ 0 };
        int32        deltaY{ 0 };
        const float2 moveVec = pActionMap->getVector2D( "Move" );
        if ( moveVec._y > 0.5f )
            deltaY = -1;
        else if ( moveVec._y < -0.5f )
            deltaY = 1;
        else if ( moveVec._x < -0.5f )
            deltaX = -1;
        else if ( moveVec._x > 0.5f )
            deltaX = 1;

        if ( deltaX == 0 && deltaY == 0 )
            return;

        // 걸음이 시작됐으면 그대로 둔다. `_loco.update` 가 `kStepDuration` 뒤에 끝낸다.
        if ( tryStep( deltaX, deltaY ) == false )
            _loco.setFacingFromDelta( deltaX, deltaY );
    }

    bool PlayerController::consumeMovedFlag()
    {
        const bool v = _bMoved != SW_FALSE;
        _bMoved      = SW_FALSE;
        return v;
    }

    bool PlayerController::consumeWarpRequest( string& outMapPath, int32& outSpawnX, int32& outSpawnY )
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

    bool PlayerController::consumeEncounterRequest()
    {
        const bool v       = _bEncounterPending != SW_FALSE;
        _bEncounterPending = SW_FALSE;
        return v;
    }

    bool PlayerController::consumeInteractRequest()
    {
        const bool v      = _bInteractPending != SW_FALSE;
        _bInteractPending = SW_FALSE;
        return v;
    }

    void PlayerController::getFacingTile( int32& outX, int32& outY ) const
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
            default:
                break;
        }
    }

    bool PlayerController::tryStep( int32 deltaX, int32 deltaY )
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
            if ( shouldEncounterOnStep( _encounterRate, _encounterStepCounter ) )
            {
                _bEncounterPending = SW_TRUE;
                SW_LOG_TRACE( "Wild encounter at (%#,%#)", _tile._x, _tile._y );
            }
        }
        return true;
    }
} // namespace sw
