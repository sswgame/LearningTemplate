#include "pch.h"

#include "GameFramework/Runtime/Character/Movement/PlatformerMotor2D.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    namespace
    {
        struct PlatformerMotor2DInternal
        {
            static constexpr float32 kSkin           = 1.0e-3f;     ///< 벽에 붙일 때 남기는 틈
            static constexpr float32 kSlopeSin       = 0.70710678f; ///< 45° 경사의 사인
            static constexpr float32 kSlopeStepSlack = 0.02f;       ///< 경사 끝에서 넘는 높이(몸 반폭)에 더하는 여유(칸 비율) — 빗변에 붙인 발 높이의 부동소수 오차
            static constexpr float32 kSlopeSnapTile  = 0.5f;        ///< 경사 빗변에 붙이는 범위(칸 비율) — 한 걸음(칸의 0.45 이하)보다 넓다

            /** @brief 경사 칸 안 가로 위치 @p localX(0..1)의 빗변 높이(0..1)입니다. 오르막은 오른쪽이 높다. */
            static float32 computeSlopeHeight( PlatformTile tile, float32 localX )
            {
                const float32 clampedX = MathUtil::clamp( localX, 0.0f, 1.0f );
                return tile == PlatformTile::SlopeUp ? clampedX : 1.0f - clampedX;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // PlatformTileMap
    // ------------------------------------------------------------------------------
    bool PlatformTileMap::initialize( int32 width, int32 height, float32 tileSize, const float2& origin )
    {
        // 타일 크기가 0 이하이거나 크기가 음수이면 맞추지 않고 거절한다(예전에는 `max( 1.0e-3f, tileSize )` 로 조용히 맞췄다).
        const bool bInvalid = tileSize <= 0.0f || width < 0 || height < 0;
        if ( bInvalid )
        {
            SW_LOG_ERROR_ID( "PlatformerMotor2D.PlatformTileMapRefusedXTiles", "PlatformTileMap: refused %# x %# tiles of size %# - the size must be positive and the counts must not be negative", width, height, tileSize );
            return false;
        }
        _topology = GridTopology( width, height );
        _tileSize = tileSize;
        _origin   = origin;
        _listTile.assign( static_cast<size_t>( _topology.getCellCount() ), PlatformTile::Empty );
        return true;
    }

    void PlatformTileMap::setTile( int32 x, int32 y, PlatformTile tile )
    {
        if ( _topology.isInside( x, y ) )
            _listTile[static_cast<size_t>( _topology.toIndex( x, y ) )] = tile;
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

    bool PlatformTileMap::loadFromXMLText( string_view text, float32 tileSize, const float2& origin )
    {
        vector<string_view> listLine;
        StringUtil::forEachLine( text, [&]( string_view line )
        {
            if ( line.empty() == false )
                listLine.push_back( line );
        } );
        int32 width = 0;
        for ( const string_view line : listLine )
        {
            width = MathUtil::max( width, static_cast<int32>( line.size() ) );
        }
        if ( initialize( width, static_cast<int32>( listLine.size() ), tileSize, origin ) == false )
            return false;
        for ( size_t row = 0; row < listLine.size(); ++row )
        {
            const int32 y = _topology._height - 1 - static_cast<int32>( row );
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
                else if ( symbol == '/' )
                    tile = PlatformTile::SlopeUp;
                else if ( symbol == '\\' )
                    tile = PlatformTile::SlopeDown;
                setTile( static_cast<int32>( column ), y, tile );
            }
        }
        return true;
    }

    PlatformTile PlatformTileMap::getTile( int32 x, int32 y ) const
    {
        if ( y < 0 )
            return PlatformTile::Empty; // 구덩이
        const bool bOutsideWorld = x < 0 || x >= _topology._width || y >= _topology._height;
        if ( bOutsideWorld )
            return PlatformTile::Solid;
        return _listTile[static_cast<size_t>( _topology.toIndex( x, y ) )];
    }

    int32 PlatformTileMap::computeTileX( float32 worldX ) const
    {
        return static_cast<int32>( MathUtil::floor( ( worldX - _origin._x ) / _tileSize ) );
    }

    int32 PlatformTileMap::computeTileY( float32 worldY ) const
    {
        return static_cast<int32>( MathUtil::floor( ( worldY - _origin._y ) / _tileSize ) );
    }

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

    void PlatformerMotor2D::changeTuning( const PlatformerSettings& settings )
    {
        _settings       = settings;
        _extraJumpsLeft = MathUtil::min( _extraJumpsLeft, settings._extraJumpCount );
        _airDashesLeft  = MathUtil::min( _airDashesLeft, settings._airDashCount );
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

    float32 PlatformerMotor2D::getJumpSpeed() const
    {
        return getGravity() * MathUtil::max( 1.0e-3f, _settings._timeToApex );
    }

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
                const bool bLandOnOneWay = tile == PlatformTile::OneWay && bFromAbove && _dropThrough.isActive() == false && previousBottom >= map.getTileBottom( y + 1 ) - 1.0e-3f;
                if ( bLandOnOneWay )
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

    bool PlatformerMotor2D::findSlopeSurface( const PlatformTileMap& map, float32 snapUp, float32 snapDown, float32& outSurfaceY, int32& outRise ) const
    {
        // 상자 몸은 모서리로 빗변에 닿는다 — 왼쪽 끝 · 가운데 · 오른쪽 끝에서 빗변을 찾아 가장 높은 것에 붙인다. 가운데 한 점만 보면 경사 꼭대기에서 앞 모서리가
        // 끝 바닥 칸에 걸리고, 경사 바닥 · 칸 경계에서 몸이 빗변 위에 떠 공중 판정이 났다.
        const float32 bottom       = _position._y - _settings._halfExtents._y;
        const float32 edgeOffset   = _settings._halfExtents._x - PlatformerMotor2DInternal::kSkin;
        const float32 arrSampleX[] = { _position._x - edgeOffset, _position._x, _position._x + edgeOffset };
        bool          bFound       = false;
        for ( const float32 sampleX : arrSampleX )
        {
            const int32   tileX  = map.computeTileX( sampleX );
            const float32 localX = ( sampleX - map.getTileLeft( tileX ) ) / map.getTileSize();
            // 위 칸부터 내려가며 본다 — 빗변이 칸 경계를 넘는 자리(오르막 꼭대기 · 내리막 시작)에서 높은 쪽을 먼저 잡는다.
            for ( int32 tileY = map.computeTileY( bottom + snapUp ); tileY >= map.computeTileY( bottom - snapDown ); --tileY )
            {
                const PlatformTile tile = map.getTile( tileX, tileY );
                if ( tile != PlatformTile::SlopeUp && tile != PlatformTile::SlopeDown )
                    continue;
                const float32 surfaceY = map.getTileBottom( tileY ) + PlatformerMotor2DInternal::computeSlopeHeight( tile, localX ) * map.getTileSize();
                if ( surfaceY > bottom + snapUp || surfaceY < bottom - snapDown )
                    continue;
                if ( bFound == false || surfaceY > outSurfaceY )
                {
                    outSurfaceY = surfaceY;
                    outRise     = tile == PlatformTile::SlopeUp ? 1 : -1;
                    bFound      = true;
                }
                break;
            }
        }
        return bFound;
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
            // 경사 위의 앞 모서리가 경사 끝 바닥 칸에 걸렸다 — 발은 몸 가운데의 빗변에 붙어 앞 모서리가 몸 반폭만큼 낮다. 그 칸 윗면까지(반폭 이내) 올려 넘는다.
            // 경사 위에서만 — 평지에서 같은 들어 올림을 허락하면 벽을 오른다.
            float32 slopeSurfaceY = 0.0f;
            int32   slopeRise     = 0;
            if ( bVertical == false && findSlopeSurface( map, PlatformerMotor2DInternal::kSlopeSnapTile * map.getTileSize(), PlatformerMotor2DInternal::kSkin, slopeSurfaceY, slopeRise ) )
            {
                const float32 leadX     = step > 0.0f ? next._x + _settings._halfExtents._x - PlatformerMotor2DInternal::kSkin : next._x - _settings._halfExtents._x + PlatformerMotor2DInternal::kSkin;
                const float32 bottom    = next._y - _settings._halfExtents._y;
                const int32   blockY    = map.computeTileY( bottom + PlatformerMotor2DInternal::kSkin );
                const float32 lift      = map.getTileBottom( blockY + 1 ) - bottom;
                const bool    bLeadWall = map.getTile( map.computeTileX( leadX ), blockY ) == PlatformTile::Solid;
                float2        lifted    = next;
                lifted._y += lift + PlatformerMotor2DInternal::kSkin;
                const bool bStepOver = bLeadWall && lift > 0.0f && lift <= _settings._halfExtents._x + PlatformerMotor2DInternal::kSlopeStepSlack * map.getTileSize() && isBlocked( map, lifted, false, 0.0f ) == false;
                if ( bStepOver )
                {
                    _position = lifted;
                    continue;
                }
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

    void PlatformerMotor2D::tryStartDash( const PlatformerInput& input, float32 moveX, float32 moveY, bool bWasGrounded )
    {
        const bool bCanDash = _dashCooldown.isActive() == false && _dash.isActive() == false && ( bWasGrounded || _airDashesLeft > 0 );
        if ( input._bDashPressed == SW_FALSE || bCanDash == false )
            return;
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

    void PlatformerMotor2D::tryJump( bool bWallLeft, bool bWallRight )
    {
        const bool bTouchingWall = _wallSide != 0 || bWallLeft || bWallRight;
        if ( _coyote.isActive() )
        {
            startJump( getJumpSpeed(), PlatformerEvent::kJumped );
        }
        else if ( bTouchingWall )
        {
            const int32 side = _wallSide != 0 ? _wallSide : ( bWallLeft ? -1 : 1 );
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

    void PlatformerMotor2D::updateRunAndJump( const PlatformTileMap& map, const PlatformerInput& input, float32 moveX, bool bWasGrounded, float32 deltaTime )
    {
        // 좌우 이동이다. 벽 점프 직후에는 입력을 약하게 받는다.
        const float32 control      = _wallLock.isActive() ? 0.2f : 1.0f;
        const float32 acceleration = ( bWasGrounded ? _settings._groundAcceleration : _settings._airAcceleration ) * control;
        _velocity._x               = MathUtil::moveToward( _velocity._x, moveX * _settings._runSpeed, acceleration * deltaTime );

        // 벽이다. 공중에서 벽 쪽을 누르고 내려오면 미끄러진다. 양옆 검사는 벽 점프도 같이 쓴다.
        bool bWallLeft  = false;
        bool bWallRight = false;
        if ( _settings._bWallJump )
        {
            bWallLeft  = isBlocked( map, float2{ _position._x - 0.05f, _position._y }, false, 0.0f );
            bWallRight = isBlocked( map, float2{ _position._x + 0.05f, _position._y }, false, 0.0f );
        }
        _wallSide = 0;
        if ( bWasGrounded == false )
        {
            if ( bWallLeft && moveX < -0.1f )
                _wallSide = -1;
            else if ( bWallRight && moveX > 0.1f )
                _wallSide = 1;
        }

        if ( _jumpBuffer.isActive() && _dropThrough.isActive() == false )
            tryJump( bWallLeft, bWallRight );

        // 오르는 중에 점프를 떼면 짧은 점프가 된다.
        const bool bCutJump = _bRising && input._bJumpHeld == SW_FALSE && _velocity._y > 0.0f;
        if ( bCutJump )
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

    void PlatformerMotor2D::update( const PlatformTileMap& map, const PlatformerInput& input, float32 deltaTime )
    {
        _events = 0;
        if ( deltaTime <= 0.0f )
            return;
        const bool bWasGrounded = _bGrounded != SW_FALSE;
        if ( bWasGrounded )
            _coyote.start( _settings._coyoteTime );
        else
            _coyote.update( deltaTime );
        if ( input._bJumpPressed )
            _jumpBuffer.start( _settings._jumpBufferTime );
        else
            _jumpBuffer.update( deltaTime );
        _wallLock.update( deltaTime );
        _dashCooldown.update( deltaTime );
        _dropThrough.update( deltaTime );
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

        tryStartDash( input, moveX, moveY, bWasGrounded );

        // 지난 틱 경사 위에 서 있었는가 — 경사 관성과, 경사 끝에서 평지로 내려설 때의 발 붙이기가 본다.
        float32    startSurfaceY = 0.0f;
        int32      startRise     = 0;
        const bool bWasOnSlope   = bWasGrounded && findSlopeSurface( map, PlatformerMotor2DInternal::kSkin, PlatformerMotor2DInternal::kSkin, startSurfaceY, startRise );

        // 사다리는 위아래를 누르면 잡고, 점프로 놓는다.
        const bool bOnLadder = isTouching( map, PlatformTile::Ladder );
        if ( bOnLadder == false )
            _bClimbing = SW_FALSE;
        else if ( MathUtil::abs( moveY ) > 0.5f && _dash.isActive() == false )
            _bClimbing = SW_TRUE;

        // 아래와 점프를 함께 누르면 발판 아래로 내려간다.
        const bool bDropThrough = bWasGrounded && moveY < -0.5f && input._bJumpPressed;
        if ( bDropThrough )
        {
            _dropThrough.start( _settings._dropThroughTime );
            _jumpBuffer.clear();
        }

        if ( _dash.isActive() )
        {
            if ( _dash.update( deltaTime ) )
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
            updateRunAndJump( map, input, moveX, bWasGrounded, deltaTime );
            // 경사 땅의 관성 — 탈것 · 썰매와 같은 식이다(가로 속도를 진행 방향 속도로, 오르는 쪽이 +x 면 사인이 +).
            float32 slopeSurfaceY = 0.0f;
            int32   slopeRise     = 0;
            if ( bWasOnSlope && _velocity._y <= 0.0f && findSlopeSurface( map, PlatformerMotor2DInternal::kSkin, PlatformerMotor2DInternal::kSkin, slopeSurfaceY, slopeRise ) )
                _velocity._x += _settings._slopeInertia.computeAlongAcceleration( _velocity._x, static_cast<float32>( slopeRise ) * PlatformerMotor2DInternal::kSlopeSin, 0.0f, deltaTime ) * deltaTime;
        }

        _bGrounded = SW_FALSE;
        moveAxis( map, _velocity._x * deltaTime, false );
        moveAxis( map, _velocity._y * deltaTime, true );
        // 경사 빗변에 발을 붙인다 — 파고든 만큼 올리고(오르막 · 떨어져 닿음), 서 있던 몸은 빗변을 따라 내린다(내리막에서 뜨지 않게). 오르는 중 · 사다리는 붙이지 않는다.
        if ( _velocity._y <= 0.0f && _bClimbing == SW_FALSE )
        {
            const float32 snapRange     = map.getTileSize() * PlatformerMotor2DInternal::kSlopeSnapTile;
            float32       slopeSurfaceY = 0.0f;
            int32         slopeRise     = 0;
            // 빗변 자리가 막혔으면(몸 한쪽이 아직 경사 끝 바닥 칸 위다) 붙이지 않는다 — 내리면 몸이 바닥 칸에 파묻혀 다음 걸음의 벽 밀어내기가 되돌린다.
            const bool bOnSlope = findSlopeSurface( map, snapRange, bWasGrounded ? snapRange : 0.0f, slopeSurfaceY, slopeRise ) &&
                                  isBlocked( map, float2{ _position._x, slopeSurfaceY + _settings._halfExtents._y + PlatformerMotor2DInternal::kSkin }, false, 0.0f ) == false;
            if ( bOnSlope )
            {
                _position._y = slopeSurfaceY + _settings._halfExtents._y;
                _velocity._y = 0.0f;
                _bGrounded   = SW_TRUE;
            }
        }
        // 경사 끝에서 평지로 내려서는 걸음 — 지난 틱 경사 위였으면 평지 바닥도 경사와 같은 범위로 붙인다(빗변을 따라 내려온 몸이 평지 위에 떠 한 틱 공중이 되지 않게).
        // 평지에서 낭떠러지를 달려 나가는 몸은 붙이지 않는다(그대로 떨어진다).
        if ( _bGrounded == SW_FALSE && bWasOnSlope && _velocity._y <= 0.0f && _bClimbing == SW_FALSE )
        {
            const float2 beforeSnap = _position;
            moveAxis( map, -map.getTileSize() * PlatformerMotor2DInternal::kSlopeSnapTile, true );
            if ( _bGrounded == SW_FALSE )
                _position = beforeSnap; // 범위 안에 바닥이 없다 — 붙이지 않는다
        }
        // 위로 움직이지 않는 틱에는 발밑을 한 번 더 보고 서 있는지 정한다.
        const bool bSnapToGround = _bGrounded == SW_FALSE && _velocity._y <= 0.0f && _bClimbing == SW_FALSE &&
                                   isBlocked( map, float2{ _position._x, _position._y - 0.02f }, true, _position._y - _settings._halfExtents._y );
        if ( bSnapToGround )
            _bGrounded = SW_TRUE;
        if ( _bGrounded && bWasGrounded == false )
            _events |= PlatformerEvent::kLanded;
        if ( isTouching( map, PlatformTile::Hazard ) )
            _events |= PlatformerEvent::kTouchedHazard;
    }

    void PlatformerMotor2D::serializeState( StateIO& io )
    {
        io.value( _position );
        io.value( _velocity );
        io.value( _coyote );
        io.value( _jumpBuffer );
        io.value( _wallLock );
        io.value( _dash );
        io.value( _dashCooldown );
        io.value( _dropThrough );
        io.value( _extraJumpsLeft );
        io.value( _airDashesLeft );
        io.value( _wallSide );
        io.value( _facing );
        io.flag( _bGrounded );
        io.flag( _bClimbing );
        io.flag( _bRising );
        if ( io.isReading() )
        {
            _events = 0;
        }
    }

} // namespace sw
