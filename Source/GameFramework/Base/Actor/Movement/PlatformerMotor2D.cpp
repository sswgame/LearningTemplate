#include "pch.h"

#include "GameFramework/Base/Actor/Movement/PlatformerMotor2D.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        struct PlatformerMotor2DInternal
        {
            static constexpr float32 kSkin = 1.0e-3f; ///< 벽에 붙일 때 남기는 틈
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // PlatformTileMap
    // ------------------------------------------------------------------------------
    void PlatformTileMap::initialize( int32 width, int32 height, float32 tileSize, const float2& origin )
    {
        _width    = MathUtil::max( 0, width );
        _height   = MathUtil::max( 0, height );
        _tileSize = MathUtil::max( 1.0e-3f, tileSize );
        _origin   = origin;
        _listTile.assign( static_cast<size_t>( _width * _height ), PlatformTile::Empty );
    }

    void PlatformTileMap::setTile( int32 x, int32 y, PlatformTile tile )
    {
        if ( x >= 0 && y >= 0 && x < _width && y < _height )
            _listTile[static_cast<size_t>( y * _width + x )] = tile;
    }

    void PlatformTileMap::fillTiles( int32 minX, int32 minY, int32 maxX, int32 maxY, PlatformTile tile )
    {
        for ( int32 y = minY; y <= maxY; ++y )
        {
            for ( int32 x = minX; x <= maxX; ++x )
            {
                setTile( x, y, tile );
            }
        }
    }

    void PlatformTileMap::loadFromText( string_view text, float32 tileSize, const float2& origin )
    {
        vector<string_view> listLine;
        size_t              start = 0;
        while ( start <= text.size() )
        {
            const size_t end  = text.find( '\n', start );
            string_view  line = text.substr( start, end == string_view::npos ? string_view::npos : end - start );
            if ( line.empty() == false && line.back() == '\r' )
                line.remove_suffix( 1 );
            if ( line.empty() == false )
                listLine.push_back( line );
            if ( end == string_view::npos )
                break;
            start = end + 1;
        }
        int32 width = 0;
        for ( const string_view line : listLine )
        {
            width = MathUtil::max( width, static_cast<int32>( line.size() ) );
        }
        initialize( width, static_cast<int32>( listLine.size() ), tileSize, origin );
        for ( size_t row = 0; row < listLine.size(); ++row )
        {
            const int32 y = _height - 1 - static_cast<int32>( row );
            for ( size_t column = 0; column < listLine[row].size(); ++column )
            {
                const utf8   symbol = listLine[row][column];
                PlatformTile tile   = PlatformTile::Empty;
                if ( symbol == '#' )
                    tile = PlatformTile::Solid;
                else if ( symbol == '-' )
                    tile = PlatformTile::OneWay;
                else if ( symbol == 'H' )
                    tile = PlatformTile::Ladder;
                else if ( symbol == '^' )
                    tile = PlatformTile::Hazard;
                setTile( static_cast<int32>( column ), y, tile );
            }
        }
    }

    PlatformTile PlatformTileMap::getTile( int32 x, int32 y ) const
    {
        if ( y < 0 )
            return PlatformTile::Empty; // 구덩이
        if ( x < 0 || x >= _width || y >= _height )
            return PlatformTile::Solid;
        return _listTile[static_cast<size_t>( y * _width + x )];
    }

    int32 PlatformTileMap::computeTileX( float32 worldX ) const { return static_cast<int32>( MathUtil::floor( ( worldX - _origin._x ) / _tileSize ) ); }

    int32 PlatformTileMap::computeTileY( float32 worldY ) const { return static_cast<int32>( MathUtil::floor( ( worldY - _origin._y ) / _tileSize ) ); }

    // ------------------------------------------------------------------------------
    // PlatformerMotor2D
    // ------------------------------------------------------------------------------
    PlatformerMotor2D::PlatformerMotor2D()
        : _settings{}
        , _position{}
        , _velocity{}
        , _coyote{}
        , _jumpBuffer{}
        , _wallLock{}
        , _dash{}
        , _dashCooldown{}
        , _dropThrough{}
        , _extraJumpsLeft{ 0 }
        , _airDashesLeft{ 0 }
        , _wallSide{ 0 }
        , _facing{ 1 }
        , _events{ 0 }
        , _bGrounded{ SW_FALSE }
        , _bClimbing{ SW_FALSE }
        , _bRising{ SW_FALSE }
    {
    }

    void PlatformerMotor2D::setSettings( const PlatformerSettings& settings )
    {
        _settings       = settings;
        _extraJumpsLeft = settings._extraJumpCount;
        _airDashesLeft  = settings._airDashCount;
    }

    void PlatformerMotor2D::setPosition( const float2& position )
    {
        _position = position;
        _velocity = float2{ 0.0f, 0.0f };
        _dash.clear();
    }

    void PlatformerMotor2D::addImpulse( const float2& impulse )
    {
        _velocity = _velocity + impulse;
        _dash.clear();
        _bRising = SW_FALSE;
    }

    float32 PlatformerMotor2D::getGravity() const
    {
        const float32 apex = MathUtil::max( 1.0e-3f, _settings._timeToApex );
        return 2.0f * _settings._jumpHeight / ( apex * apex );
    }

    float32 PlatformerMotor2D::getJumpSpeed() const { return getGravity() * MathUtil::max( 1.0e-3f, _settings._timeToApex ); }

    bool PlatformerMotor2D::isBlocked( const PlatformTileMap& map, const float2& center, bool bFromAbove, float32 previousBottom ) const
    {
        const float2 half = _settings._halfExtents;
        const int32  minX = map.computeTileX( center._x - half._x + PlatformerMotor2DInternal::kSkin );
        const int32  maxX = map.computeTileX( center._x + half._x - PlatformerMotor2DInternal::kSkin );
        const int32  minY = map.computeTileY( center._y - half._y + PlatformerMotor2DInternal::kSkin );
        const int32  maxY = map.computeTileY( center._y + half._y - PlatformerMotor2DInternal::kSkin );
        for ( int32 y = minY; y <= maxY; ++y )
        {
            for ( int32 x = minX; x <= maxX; ++x )
            {
                const PlatformTile tile = map.getTile( x, y );
                if ( tile == PlatformTile::Solid )
                    return true;
                // 한쪽 발판 — 내려오며, 지난 틱 발이 발판 윗면보다 위였고, 내려가기 중이 아닐 때만.
                if ( tile == PlatformTile::OneWay && bFromAbove && _dropThrough.isActive() == false && previousBottom >= map.getTileBottom( y + 1 ) - 1.0e-3f )
                    return true;
            }
        }
        return false;
    }

    bool PlatformerMotor2D::isTouching( const PlatformTileMap& map, PlatformTile tile ) const
    {
        const float2 half = _settings._halfExtents;
        for ( int32 y = map.computeTileY( _position._y - half._y ); y <= map.computeTileY( _position._y + half._y - PlatformerMotor2DInternal::kSkin ); ++y )
        {
            for ( int32 x = map.computeTileX( _position._x - half._x ); x <= map.computeTileX( _position._x + half._x - PlatformerMotor2DInternal::kSkin ); ++x )
            {
                if ( map.getTile( x, y ) == tile )
                    return true;
            }
        }
        return false;
    }

    void PlatformerMotor2D::moveAxis( const PlatformTileMap& map, float32 delta, bool bVertical )
    {
        if ( delta == 0.0f )
            return;
        // 칸 반보다 작은 걸음으로 나눠 얇은 벽을 건너뛰지 않게.
        const float32 maxStep   = map.getTileSize() * 0.45f;
        const int32   stepCount = MathUtil::max( 1, static_cast<int32>( MathUtil::abs( delta ) / maxStep ) + 1 );
        const float32 step      = delta / static_cast<float32>( stepCount );
        for ( int32 index = 0; index < stepCount; ++index )
        {
            float2        next           = _position;
            const float32 previousBottom = _position._y - _settings._halfExtents._y;
            if ( bVertical )
                next._y += step;
            else
                next._x += step;
            if ( isBlocked( map, next, bVertical && step < 0.0f, previousBottom ) == false )
            {
                _position = next;
                continue;
            }
            // 칸 가장자리에 붙인다.
            if ( bVertical )
            {
                if ( step < 0.0f )
                {
                    const int32 tileY = map.computeTileY( next._y - _settings._halfExtents._y );
                    _position._y      = map.getTileBottom( tileY + 1 ) + _settings._halfExtents._y;
                    _bGrounded        = SW_TRUE;
                }
                else
                {
                    const int32 tileY = map.computeTileY( next._y + _settings._halfExtents._y );
                    _position._y      = map.getTileBottom( tileY ) - _settings._halfExtents._y - PlatformerMotor2DInternal::kSkin;
                    _events |= PlatformerEvent::kHitCeiling;
                    _bRising = SW_FALSE;
                }
                _velocity._y = 0.0f;
            }
            else
            {
                if ( step > 0.0f )
                {
                    const int32 tileX = map.computeTileX( next._x + _settings._halfExtents._x );
                    _position._x      = map.getTileLeft( tileX ) - _settings._halfExtents._x - PlatformerMotor2DInternal::kSkin;
                }
                else
                {
                    const int32 tileX = map.computeTileX( next._x - _settings._halfExtents._x );
                    _position._x      = map.getTileLeft( tileX + 1 ) + _settings._halfExtents._x + PlatformerMotor2DInternal::kSkin;
                }
                if ( _dash.isActive() == false )
                    _velocity._x = 0.0f;
            }
            return;
        }
    }

    void PlatformerMotor2D::startJump( float32 speed, uint32 event )
    {
        _velocity._y = speed;
        _bRising     = SW_TRUE;
        _bGrounded   = SW_FALSE;
        _bClimbing   = SW_FALSE;
        _coyote.clear();
        _jumpBuffer.clear();
        _events |= event;
    }

    void PlatformerMotor2D::update( const PlatformTileMap& map, const PlatformerInput& input, float32 deltaTime )
    {
        _events = 0;
        if ( deltaTime <= 0.0f )
            return;
        const bool bWasGrounded = _bGrounded != SW_FALSE;
        if ( bWasGrounded )
            _coyote.start( _settings._coyoteTime );
        else
            _coyote.tick( deltaTime );
        if ( input._bJumpPressed )
            _jumpBuffer.start( _settings._jumpBufferTime );
        else
            _jumpBuffer.tick( deltaTime );
        _wallLock.tick( deltaTime );
        _dashCooldown.tick( deltaTime );
        _dropThrough.tick( deltaTime );
        if ( bWasGrounded )
        {
            _extraJumpsLeft = _settings._extraJumpCount;
            _airDashesLeft  = _settings._airDashCount;
        }
        const float32 moveX = MathUtil::clamp( input._move._x, -1.0f, 1.0f );
        const float32 moveY = MathUtil::clamp( input._move._y, -1.0f, 1.0f );
        if ( moveX > 0.1f )
            _facing = 1;
        else if ( moveX < -0.1f )
            _facing = -1;

        // 대시 — 입력 방향(없으면 바라보는 쪽)으로 곧게, 중력 없이.
        const bool bCanDash = _dashCooldown.isActive() == false && _dash.isActive() == false && ( bWasGrounded || _airDashesLeft > 0 );
        if ( input._bDashPressed && bCanDash )
        {
            float2 direction{ moveX, moveY };
            if ( direction.getLengthSquared() < 0.01f )
                direction = float2{ static_cast<float32>( _facing ), 0.0f };
            direction = direction * ( 1.0f / direction.getLength() );
            _velocity = direction * _settings._dashSpeed;
            _dash.start( _settings._dashDuration );
            _dashCooldown.start( _settings._dashDuration + _settings._dashCooldown );
            _bClimbing = SW_FALSE;
            _bRising   = SW_FALSE;
            if ( bWasGrounded == false )
                --_airDashesLeft;
            _events |= PlatformerEvent::kDashed;
        }

        // 사다리 — 위아래를 누르면 잡고, 점프로 놓는다.
        const bool bOnLadder = isTouching( map, PlatformTile::Ladder );
        if ( bOnLadder == false )
            _bClimbing = SW_FALSE;
        else if ( MathUtil::abs( moveY ) > 0.5f && _dash.isActive() == false )
            _bClimbing = SW_TRUE;

        // 발판 아래로 — 아래 + 점프.
        if ( bWasGrounded && moveY < -0.5f && input._bJumpPressed )
        {
            _dropThrough.start( _settings._dropThroughTime );
            _jumpBuffer.clear();
        }

        if ( _dash.isActive() )
        {
            if ( _dash.tick( deltaTime ) )
                _velocity = _velocity * 0.4f; // 대시 끝에 속도를 꺾는다
        }
        else if ( _bClimbing )
        {
            _velocity = float2{ moveX * _settings._climbSpeed * 0.5f, moveY * _settings._climbSpeed };
            if ( _jumpBuffer.isActive() )
                startJump( getJumpSpeed() * 0.8f, PlatformerEvent::kJumped );
        }
        else
        {
            // 좌우 — 벽 점프 직후에는 입력을 약하게.
            const float32 control      = _wallLock.isActive() ? 0.2f : 1.0f;
            const float32 acceleration = ( bWasGrounded ? _settings._groundAcceleration : _settings._airAcceleration ) * control;
            _velocity._x               = MathUtil::moveToward( _velocity._x, moveX * _settings._runSpeed, acceleration * deltaTime );

            // 벽 — 공중에서 벽 쪽을 누르고 내려오면 미끄러진다.
            _wallSide = 0;
            if ( bWasGrounded == false && _settings._bWallJump )
            {
                const bool bLeft  = isBlocked( map, float2{ _position._x - 0.05f, _position._y }, false, 0.0f );
                const bool bRight = isBlocked( map, float2{ _position._x + 0.05f, _position._y }, false, 0.0f );
                if ( bLeft && moveX < -0.1f )
                    _wallSide = -1;
                else if ( bRight && moveX > 0.1f )
                    _wallSide = 1;
            }

            // 점프 — 땅(코요테 포함) → 벽 → 공중 점프 순.
            if ( _jumpBuffer.isActive() && _dropThrough.isActive() == false )
            {
                if ( _coyote.isActive() )
                {
                    startJump( getJumpSpeed(), PlatformerEvent::kJumped );
                }
                else if ( _wallSide != 0 || ( _settings._bWallJump &&
                                              ( isBlocked( map, float2{ _position._x - 0.05f, _position._y }, false, 0.0f ) ||
                                                isBlocked( map, float2{ _position._x + 0.05f, _position._y }, false, 0.0f ) ) ) )
                {
                    const int32 side = _wallSide != 0 ? _wallSide : ( isBlocked( map, float2{ _position._x - 0.05f, _position._y }, false, 0.0f ) ? -1 : 1 );
                    startJump( _settings._wallJumpSpeedY, PlatformerEvent::kWallJumped );
                    _velocity._x = static_cast<float32>( -side ) * _settings._wallJumpSpeedX;
                    _facing      = -side;
                    _wallLock.start( _settings._wallJumpLockTime );
                    _airDashesLeft = _settings._airDashCount;
                    _wallSide      = 0;
                }
                else if ( _extraJumpsLeft > 0 )
                {
                    --_extraJumpsLeft;
                    startJump( getJumpSpeed(), PlatformerEvent::kAirJumped );
                }
            }

            // 짧은 점프 — 오르는 중에 떼면.
            if ( _bRising && input._bJumpHeld == SW_FALSE && _velocity._y > 0.0f )
            {
                _velocity._y *= _settings._jumpCutRatio;
                _bRising = SW_FALSE;
            }
            if ( _velocity._y <= 0.0f )
                _bRising = SW_FALSE;

            const float32 gravityScale = _velocity._y < 0.0f ? _settings._fallGravityScale : 1.0f;
            _velocity._y               = MathUtil::max( -_settings._maxFallSpeed, _velocity._y - getGravity() * gravityScale * deltaTime );
            if ( _wallSide != 0 )
                _velocity._y = MathUtil::max( _velocity._y, -_settings._wallSlideSpeed );
        }

        _bGrounded = SW_FALSE;
        moveAxis( map, _velocity._x * deltaTime, false );
        moveAxis( map, _velocity._y * deltaTime, true );
        // 서 있는가 — 발밑을 한 번 더 본다(위로 움직이지 않는 틱).
        if ( _bGrounded == SW_FALSE && _velocity._y <= 0.0f && _bClimbing == SW_FALSE &&
             isBlocked( map, float2{ _position._x, _position._y - 0.02f }, true, _position._y - _settings._halfExtents._y ) )
            _bGrounded = SW_TRUE;
        if ( _bGrounded && bWasGrounded == false )
            _events |= PlatformerEvent::kLanded;
        if ( isTouching( map, PlatformTile::Hazard ) )
            _events |= PlatformerEvent::kTouchedHazard;
    }

    void PlatformerMotor2D::writeState( Archive& outArchive ) const
    {
        outArchive << _position;
        outArchive << _velocity;
        StateArchiveUtil::writeCountdown( outArchive, _coyote );
        StateArchiveUtil::writeCountdown( outArchive, _jumpBuffer );
        StateArchiveUtil::writeCountdown( outArchive, _wallLock );
        StateArchiveUtil::writeCountdown( outArchive, _dash );
        StateArchiveUtil::writeCountdown( outArchive, _dashCooldown );
        StateArchiveUtil::writeCountdown( outArchive, _dropThrough );
        outArchive << _extraJumpsLeft;
        outArchive << _airDashesLeft;
        outArchive << _wallSide;
        outArchive << _facing;
        outArchive << _bGrounded;
        outArchive << _bClimbing;
        outArchive << _bRising;
    }

    bool PlatformerMotor2D::readState( Archive& archive )
    {
        PlatformerMotor2D restored = *this;
        archive >> restored._position;
        archive >> restored._velocity;
        const bool bTimerRead =
            StateArchiveUtil::readCountdown( archive, restored._coyote ) && StateArchiveUtil::readCountdown( archive, restored._jumpBuffer ) &&
            StateArchiveUtil::readCountdown( archive, restored._wallLock ) && StateArchiveUtil::readCountdown( archive, restored._dash ) &&
            StateArchiveUtil::readCountdown( archive, restored._dashCooldown ) && StateArchiveUtil::readCountdown( archive, restored._dropThrough );
        archive >> restored._extraJumpsLeft;
        archive >> restored._airDashesLeft;
        archive >> restored._wallSide;
        archive >> restored._facing;
        archive >> restored._bGrounded;
        archive >> restored._bClimbing;
        archive >> restored._bRising;
        const bool bValid = bTimerRead && archive.isOk() && restored._bGrounded <= SW_TRUE && restored._bClimbing <= SW_TRUE && restored._bRising <= SW_TRUE;
        if ( bValid == false )
            return false;
        restored._events = 0;
        *this            = restored;
        return true;
    }
} // namespace sw
