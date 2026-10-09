#include "pch.h"

#include "GameFramework/Kits/Action/ActionPlatformer/ActionPlatformerBody.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        struct ActionPlatformerBodyInternal
        {
            static constexpr float32 kSkin       = 1.0e-3f; ///< 칸 가장자리에 붙일 때 남기는 틈
            static constexpr float32 kProbeExtra = 0.2f;    ///< 드릴 진입 — 몸 가장자리에서 이만큼 더 앞을 본다

            /** @brief 몸 상자가 벽 칸에 걸치는가입니다(한쪽 발판 · 사다리는 보지 않는다). */
            static bool isBoxBlocked( const PlatformTileMap& map, const float2& center, const float2& half )
            {
                const int32 minX = map.computeTileX( center._x - half._x + kSkin );
                const int32 maxX = map.computeTileX( center._x + half._x - kSkin );
                const int32 minY = map.computeTileY( center._y - half._y + kSkin );
                const int32 maxY = map.computeTileY( center._y + half._y - kSkin );
                for ( int32 y = minY; y <= maxY; ++y )
                {
                    for ( int32 x = minX; x <= maxX; ++x )
                    {
                        if ( map.getTile( x, y ) == PlatformTile::Solid )
                            return true;
                    }
                }
                return false;
            }

            /** @brief 길이가 있으면 단위 벡터, 없으면 @p fallback 입니다. */
            static float2 normalizeOr( const float2& value, const float2& fallback )
            {
                const float32 lengthSquared = value.getLengthSquared();
                if ( lengthSquared < 1.0e-4f )
                    return fallback;
                return value * ( 1.0f / MathUtil::sqrt( lengthSquared ) );
            }

            /** @brief `PlatformTileMap::loadFromText` 와 같은 줄 나누기(빈 줄 · 끝 CR 무시)입니다. */
            static void splitLines( string_view text, vector<string_view>& outListLine )
            {
                outListLine.clear();
                size_t start = 0;
                while ( start <= text.size() )
                {
                    const size_t end  = text.find( '\n', start );
                    string_view  line = text.substr( start, end == string_view::npos ? string_view::npos : end - start );
                    if ( line.empty() == false && line.back() == '\r' )
                        line.remove_suffix( 1 );
                    if ( line.empty() == false )
                        outListLine.push_back( line );
                    if ( end == string_view::npos )
                        break;
                    start = end + 1;
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // ActionTerrainGrid
    // ------------------------------------------------------------------------------
    ActionTerrainGrid::ActionTerrainGrid()
        : _listDirt{}
        , _listGrapplePoint{}
        , _origin{}
        , _tileSize{ 1.0f }
        , _topology{}
    {
    }

    void ActionTerrainGrid::loadFromText( string_view text, float32 tileSize, const float2& origin, PlatformTileMap& outMap )
    {
        outMap.loadFromText( text, tileSize, origin );
        _origin   = origin;
        _tileSize = outMap.getTileSize();
        _topology = GridTopology{ outMap.getWidth(), outMap.getHeight() };
        _listDirt.assign( static_cast<size_t>( _topology.getCellCount() ), SW_FALSE );
        _listGrapplePoint.clear();

        vector<string_view> listLine;
        ActionPlatformerBodyInternal::splitLines( text, listLine );
        for ( size_t row = 0; row < listLine.size(); ++row )
        {
            const int32 y = _topology._height - 1 - static_cast<int32>( row );
            for ( size_t column = 0; column < listLine[row].size(); ++column )
            {
                const int32 x = static_cast<int32>( column );
                if ( listLine[row][column] == 'D' )
                {
                    _listDirt[static_cast<size_t>( _topology.toIndex( x, y ) )] = SW_TRUE;
                    outMap.setTile( x, y, PlatformTile::Solid );
                }
                else if ( listLine[row][column] == 'O' )
                {
                    const float32 half = _tileSize * 0.5f;
                    _listGrapplePoint.push_back( float2{ outMap.getTileLeft( x ) + half, outMap.getTileBottom( y ) + half } );
                }
            }
        }
    }

    bool ActionTerrainGrid::isDirt( int32 x, int32 y ) const
    {
        if ( _topology.isInside( x, y ) == false )
            return false;
        return _listDirt[static_cast<size_t>( _topology.toIndex( x, y ) )] == SW_TRUE;
    }

    bool ActionTerrainGrid::isDirtAt( const float2& worldPosition ) const { return isDirt( computeTileX( worldPosition._x ), computeTileY( worldPosition._y ) ); }

    int32 ActionTerrainGrid::findGrapplePoint( const float2& position, float32 minDistance, float32 maxDistance ) const
    {
        int32   bestIndex    = -1;
        float32 bestDistance = 0.0f;
        for ( size_t index = 0; index < _listGrapplePoint.size(); ++index )
        {
            const float32 distance = float2::getDistance( position, _listGrapplePoint[index] );
            if ( distance < minDistance || distance > maxDistance )
                continue;
            if ( bestIndex < 0 || distance < bestDistance )
            {
                bestIndex    = static_cast<int32>( index );
                bestDistance = distance;
            }
        }
        return bestIndex;
    }

    int32 ActionTerrainGrid::computeTileX( float32 worldX ) const { return static_cast<int32>( MathUtil::floor( ( worldX - _origin._x ) / _tileSize ) ); }

    int32 ActionTerrainGrid::computeTileY( float32 worldY ) const { return static_cast<int32>( MathUtil::floor( ( worldY - _origin._y ) / _tileSize ) ); }

    // ------------------------------------------------------------------------------
    // ActionPlatformerBody
    // ------------------------------------------------------------------------------
    ActionPlatformerBody::ActionPlatformerBody()
        : _motor{}
        , _motorSettings{}
        , _settings{}
        , _position{}
        , _velocity{}
        , _anchor{}
        , _ropeLength{ 0.0f }
        , _drillSearchTimer{}
        , _events{ 0 }
        , _mode{ ActionMoveMode::Normal }
        , _bInsideDirt{ SW_FALSE }
    {
    }

    void ActionPlatformerBody::initialize( const PlatformerSettings& motorSettings, const ActionBodySettings& settings )
    {
        _motorSettings = motorSettings;
        _settings      = settings;
        _motor.setSettings( motorSettings );
        _mode   = ActionMoveMode::Normal;
        _events = 0;
    }

    void ActionPlatformerBody::setPosition( const float2& position )
    {
        if ( _mode == ActionMoveMode::Glide )
            setGlide( false );
        _mode     = ActionMoveMode::Normal;
        _position = position;
        _velocity = float2{ 0.0f, 0.0f };
        _motor.setPosition( position );
    }

    float2 ActionPlatformerBody::getPosition() const
    {
        return _mode == ActionMoveMode::Grapple || _mode == ActionMoveMode::Drill ? _position : _motor.getPosition();
    }

    float2 ActionPlatformerBody::getVelocity() const
    {
        return _mode == ActionMoveMode::Grapple || _mode == ActionMoveMode::Drill ? _velocity : _motor.getVelocity();
    }

    void ActionPlatformerBody::update( const PlatformTileMap& map, const ActionTerrainGrid& terrain, const ActionBodyInput& input, float32 deltaTime )
    {
        _events = 0;
        if ( deltaTime <= 0.0f )
            return;
        if ( _mode == ActionMoveMode::Grapple )
        {
            updateGrapple( map, input, deltaTime );
            return;
        }
        if ( _mode == ActionMoveMode::Drill )
        {
            updateDrill( map, terrain, input, deltaTime );
            return;
        }

        // 보통 · 활공 — 갈고리 → 드릴 → 활공 순으로 본다.
        if ( input._bGrapplePressed == SW_TRUE && tryAttachGrapple( terrain ) )
        {
            updateGrapple( map, input, deltaTime );
            return;
        }
        if ( input._bDrillHeld == SW_TRUE && tryEnterDrill( terrain, input ) )
        {
            updateDrill( map, terrain, input, deltaTime );
            return;
        }
        const bool bWantGlide = input._bGlideHeld == SW_TRUE && _motor.isGrounded() == false;
        if ( bWantGlide != ( _mode == ActionMoveMode::Glide ) )
            setGlide( bWantGlide );
        _motor.update( map, input._motor, deltaTime );
        if ( _mode == ActionMoveMode::Glide && _motor.isGrounded() )
            setGlide( false );
    }

    PlatformerSettings ActionPlatformerBody::makeMotorSettings( bool bGlide ) const
    {
        PlatformerSettings settings = _motorSettings;
        if ( bGlide )
        {
            settings._maxFallSpeed     = MathUtil::min( settings._maxFallSpeed, _settings._glideFallSpeed );
            settings._fallGravityScale = settings._fallGravityScale * _settings._glideGravityScale;
        }
        return settings;
    }

    void ActionPlatformerBody::setGlide( bool bGlide )
    {
        // 기반 몸의 설정을 바꿔 낙하 상한 · 중력을 줄인다. 모드가 바뀔 때만 둔다(setSettings 는 공중 점프 · 대시 횟수를 다시 채운다).
        const PlatformerSettings settings = makeMotorSettings( bGlide );
        if ( bGlide )
        {
            _mode = ActionMoveMode::Glide;
            _events |= ActionBodyEvent::kGlideStarted;
        }
        else
        {
            if ( _mode == ActionMoveMode::Glide )
                _events |= ActionBodyEvent::kGlideEnded;
            _mode = ActionMoveMode::Normal;
        }
        const float2 velocity = _motor.getVelocity();
        _motor.setSettings( settings );
        _motor.setVelocity( velocity );
    }

    bool ActionPlatformerBody::tryAttachGrapple( const ActionTerrainGrid& terrain )
    {
        const float2 position = _motor.getPosition();
        const int32  index    = terrain.findGrapplePoint( position, _settings._grappleMinLength, _settings._grappleRange );
        if ( index < 0 )
            return false;
        if ( _mode == ActionMoveMode::Glide )
            setGlide( false );
        _anchor     = terrain.getGrapplePoints()[static_cast<size_t>( index )];
        _ropeLength = float2::getDistance( position, _anchor );
        _position   = position;
        _velocity   = _motor.getVelocity();
        _mode       = ActionMoveMode::Grapple;
        _events |= ActionBodyEvent::kGrappleAttached;
        return true;
    }

    bool ActionPlatformerBody::tryEnterDrill( const ActionTerrainGrid& terrain, const ActionBodyInput& input )
    {
        const float2  position = _motor.getPosition();
        const float2  facing{ static_cast<float32>( _motor.getFacing() ), 0.0f };
        const float2  direction = ActionPlatformerBodyInternal::normalizeOr( input._motor._move, ActionPlatformerBodyInternal::normalizeOr( _motor.getVelocity(), facing ) );
        const float2  half      = _motorSettings._halfExtents;
        const float32 reach     = MathUtil::max( half._x, half._y ) + ActionPlatformerBodyInternal::kProbeExtra;
        if ( terrain.isDirtAt( position + direction * reach ) == false )
            return false;
        if ( _mode == ActionMoveMode::Glide )
            setGlide( false );
        _position = position;
        _velocity = direction * _settings._drillSpeed;
        _drillSearchTimer.start( _settings._drillEntryTime );
        _bInsideDirt = SW_FALSE;
        _mode        = ActionMoveMode::Drill;
        _events |= ActionBodyEvent::kDrillEntered;
        return true;
    }

    void ActionPlatformerBody::updateGrapple( const PlatformTileMap& map, const ActionBodyInput& input, float32 deltaTime )
    {
        if ( input._bGrappleHeld == SW_FALSE )
        {
            // 놓기 — 진자의 속도를 그대로 들고 기반 몸으로.
            returnToMotor( _velocity );
            _events |= ActionBodyEvent::kGrappleReleased;
            return;
        }
        float2 velocity = _velocity;
        velocity._y -= _motor.getGravity() * deltaTime;
        const float2  rope   = _position - _anchor;
        const float32 length = rope.getLength();
        if ( length > 1.0e-4f )
        {
            // 좌우 입력은 줄에 수직인 접선으로 민다(오른쪽 입력 = +X 쪽 접선).
            const float2 radial = rope * ( 1.0f / length );
            float2       tangent{ -radial._y, radial._x };
            if ( tangent._x < 0.0f )
                tangent = -tangent;
            velocity += tangent * ( MathUtil::clamp( input._motor._move._x, -1.0f, 1.0f ) * _settings._grappleSwingAcceleration * deltaTime );
        }
        const float32 speed = velocity.getLength();
        if ( speed > _settings._grappleMaxSpeed )
            velocity = velocity * ( _settings._grappleMaxSpeed / speed );

        // 줄은 늘어나지 않는다 — 넘친 위치를 줄 길이로 되돌리고 바깥으로 가는 속도를 뺀다(접선 속도만 남는다).
        float2        next      = _position + velocity * deltaTime;
        const float2  toNext    = next - _anchor;
        const float32 nextRange = toNext.getLength();
        if ( nextRange > _ropeLength && nextRange > 1.0e-4f )
        {
            const float2 normal   = toNext * ( 1.0f / nextRange );
            next                  = _anchor + normal * _ropeLength;
            const float32 outward = velocity.dot( normal );
            if ( outward > 0.0f )
                velocity -= normal * outward;
        }
        if ( ActionPlatformerBodyInternal::isBoxBlocked( map, next, _motorSettings._halfExtents ) )
        {
            velocity = float2{ 0.0f, 0.0f }; // 벽에 부딪혔다 — 그 자리에 매달린다
            next     = _position;
        }
        _position = next;
        _velocity = velocity;
    }

    void ActionPlatformerBody::updateDrill( const PlatformTileMap& map, const ActionTerrainGrid& terrain, const ActionBodyInput& input, float32 deltaTime )
    {
        // 방향은 입력(없으면 지금 진행 방향) — 속도는 늘 일정하다.
        const float2 heading   = ActionPlatformerBodyInternal::normalizeOr( _velocity, float2{ static_cast<float32>( _motor.getFacing() ), 0.0f } );
        const float2 direction = ActionPlatformerBodyInternal::normalizeOr( input._motor._move, heading );
        _velocity              = direction * _settings._drillSpeed;
        const float2 previous  = _position;
        const float2 next      = _position + _velocity * deltaTime;
        const int32  nextX     = map.computeTileX( next._x );
        const int32  nextY     = map.computeTileY( next._y );
        const bool   bDirt     = terrain.isDirtAt( next );
        if ( bDirt == false && map.getTile( nextX, nextY ) == PlatformTile::Solid )
            return; // 바위 — 파지 못한다(방향을 바꿀 때까지 멈춘다)
        _position = next;
        if ( bDirt )
        {
            _bInsideDirt = SW_TRUE;
            return;
        }
        if ( _bInsideDirt == SW_FALSE )
        {
            _drillSearchTimer.tick( deltaTime );
            if ( _drillSearchTimer.isActive() == false )
            {
                returnToMotor( _velocity * 0.5f );
                _events |= ActionBodyEvent::kDrillAborted;
            }
            return;
        }

        // 튀어나왔다 — 몸 상자가 흙에 걸치지 않게 빠져나온 쪽 칸 가장자리 밖으로 옮긴다.
        const float2 half      = _motorSettings._halfExtents;
        const int32  previousX = map.computeTileX( previous._x );
        const int32  previousY = map.computeTileY( previous._y );
        if ( nextY > previousY )
            _position._y = map.getTileBottom( previousY + 1 ) + half._y + ActionPlatformerBodyInternal::kSkin;
        else if ( nextY < previousY )
            _position._y = map.getTileBottom( previousY ) - half._y - ActionPlatformerBodyInternal::kSkin;
        if ( nextX > previousX )
            _position._x = map.getTileLeft( previousX + 1 ) + half._x + ActionPlatformerBodyInternal::kSkin;
        else if ( nextX < previousX )
            _position._x = map.getTileLeft( previousX ) - half._x - ActionPlatformerBodyInternal::kSkin;

        float2 exitVelocity = direction * _settings._drillExitSpeed;
        _events |= ActionBodyEvent::kDrillExited;
        if ( input._motor._bJumpHeld == SW_TRUE || input._motor._bJumpPressed == SW_TRUE )
        {
            exitVelocity._y = MathUtil::max( exitVelocity._y, _settings._drillJumpSpeed );
            _events |= ActionBodyEvent::kDrillJumped;
        }
        returnToMotor( exitVelocity );
    }

    void ActionPlatformerBody::returnToMotor( const float2& velocity )
    {
        _motor.setPosition( _position );
        _motor.setVelocity( velocity );
        _velocity    = velocity;
        _mode        = ActionMoveMode::Normal;
        _bInsideDirt = SW_FALSE;
    }

    void ActionPlatformerBody::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint8>( _mode );
        _motor.writeState( outArchive );
        outArchive << _position;
        outArchive << _velocity;
        outArchive << _anchor;
        outArchive << _ropeLength;
        StateArchiveUtil::writeCountdown( outArchive, _drillSearchTimer );
        outArchive << _bInsideDirt;
    }

    bool ActionPlatformerBody::readState( Archive& archive )
    {
        uint8 mode = 0;
        archive >> mode;
        if ( archive.isError() || mode > static_cast<uint8>( ActionMoveMode::Drill ) )
            return false;
        // 사본에 읽고 끝까지 맞으면 바꾼다. 기반 몸의 설정은 모드가 정한다(활공이면 활공 설정) — 설정을 먼저 걸고 몸의 값을 읽는다.
        ActionPlatformerBody restored = *this;
        restored._mode                = static_cast<ActionMoveMode>( mode );
        restored._motor.setSettings( makeMotorSettings( restored._mode == ActionMoveMode::Glide ) );
        if ( restored._motor.readState( archive ) == false )
            return false;
        archive >> restored._position;
        archive >> restored._velocity;
        archive >> restored._anchor;
        archive >> restored._ropeLength;
        const bool bTimerRead = StateArchiveUtil::readCountdown( archive, restored._drillSearchTimer );
        archive >> restored._bInsideDirt;
        const bool bValid = bTimerRead && archive.isOk() && 0.0f <= restored._ropeLength && restored._bInsideDirt <= SW_TRUE;
        if ( bValid == false )
            return false;
        restored._events = 0;
        *this            = std::move( restored );
        return true;
    }
} // namespace sw
