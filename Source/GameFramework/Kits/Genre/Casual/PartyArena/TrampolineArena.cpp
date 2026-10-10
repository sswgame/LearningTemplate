#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/PartyArena/TrampolineArena.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        struct TrampolineArenaInternal
        {
            static constexpr float32 kTiny        = 1.0e-5f;
            static constexpr float32 kNoPressTime = -1000.0f;
            static constexpr int32   kMinPlayers  = 2;
            static constexpr int32   kMaxPlayers  = 4;
            static constexpr uint32  kAxisSteps   = 255u;

            static float32 computeFlatLength( const float3& value ) { return MathUtil::sqrt( value._x * value._x + value._z * value._z ); }

            static uint32 encodeAxis( float32 value )
            {
                const float32 clamped = MathUtil::clamp( value, -1.0f, 1.0f );
                return static_cast<uint32>( MathUtil::round( ( clamped + 1.0f ) * 0.5f * static_cast<float32>( kAxisSteps ) ) );
            }

            static float32 decodeAxis( uint32 bits )
            {
                return static_cast<float32>( MathUtil::min( bits, kAxisSteps ) ) / static_cast<float32>( kAxisSteps ) * 2.0f - 1.0f;
            }

            static const hashed_string& getTeamName( int32 player )
            {
                static const hashed_string kArrName[kMaxPlayers] = { hashed_string( "Player1" ), hashed_string( "Player2" ), hashed_string( "Player3" ),
                                                                     hashed_string( "Player4" ) };
                return kArrName[static_cast<size_t>( MathUtil::clamp( player, 0, kMaxPlayers - 1 ) )];
            }

            static const hashed_string& getSuperBounceName()
            {
                static const hashed_string name( "SuperBounce" );
                return name;
            }
            static const hashed_string& getHeavyName()
            {
                static const hashed_string name( "Heavy" );
                return name;
            }
            static const hashed_string& getDurationName()
            {
                static const hashed_string name( "duration" );
                return name;
            }
            static const hashed_string& getKnockbackTakenName()
            {
                static const hashed_string name( "knockbackTaken" );
                return name;
            }
            static const hashed_string& getKnockbackDealtName()
            {
                static const hashed_string name( "knockbackDealt" );
                return name;
            }
            static const hashed_string& getShieldName()
            {
                static const hashed_string name( "Shield" );
                return name;
            }

            static void writePlayer( Archive& outArchive, const TrampolinePlayer& body )
            {
                outArchive << body._position;
                outArchive << body._velocity;
                outArchive << body._facing;
                outArchive << body._input._moveX;
                outArchive << body._input._moveZ;
                outArchive << body._input._bJumpPressed;
                outArchive << body._input._bAttackPressed;
                outArchive << body._input._bPoundPressed;
                outArchive << body._landTime;
                outArchive << body._jumpPressTime;
                StateArchiveUtil::writeCountdown( outArchive, body._contactTimer );
                StateArchiveUtil::writeCountdown( outArchive, body._attackTimer );
                StateArchiveUtil::writeCountdown( outArchive, body._attackCooldown );
                StateArchiveUtil::writeCountdown( outArchive, body._stunTimer );
                StateArchiveUtil::writeCountdown( outArchive, body._invulnerableTimer );
                outArchive << body._lastHitTime;
                StateArchiveUtil::writeCountdown( outArchive, body._heavyTimer );
                outArchive << body._knockbackTaken;
                outArchive << body._knockbackDealt;
                outArchive << body._combo;
                outArchive << body._lastHitter;
                outArchive << body._bPounding;
                outArchive << body._bSuperBounce;
                outArchive << body._bShield;
                outArchive << body._bHitThisDash;
                outArchive << static_cast<uint8>( body._state );
            }

            [[nodiscard]] static bool readPlayer( Archive& archive, int32 playerCount, TrampolinePlayer& outBody )
            {
                archive >> outBody._position;
                archive >> outBody._velocity;
                archive >> outBody._facing;
                archive >> outBody._input._moveX;
                archive >> outBody._input._moveZ;
                archive >> outBody._input._bJumpPressed;
                archive >> outBody._input._bAttackPressed;
                archive >> outBody._input._bPoundPressed;
                archive >> outBody._landTime;
                archive >> outBody._jumpPressTime;
                const bool bTimersRead = StateArchiveUtil::readCountdown( archive, outBody._contactTimer ) && StateArchiveUtil::readCountdown( archive, outBody._attackTimer ) &&
                                         StateArchiveUtil::readCountdown( archive, outBody._attackCooldown ) && StateArchiveUtil::readCountdown( archive, outBody._stunTimer ) &&
                                         StateArchiveUtil::readCountdown( archive, outBody._invulnerableTimer );
                archive >> outBody._lastHitTime;
                const bool bHeavyRead = StateArchiveUtil::readCountdown( archive, outBody._heavyTimer );
                uint8      state      = 0;
                archive >> outBody._knockbackTaken;
                archive >> outBody._knockbackDealt;
                archive >> outBody._combo;
                archive >> outBody._lastHitter;
                archive >> outBody._bPounding;
                archive >> outBody._bSuperBounce;
                archive >> outBody._bShield;
                archive >> outBody._bHitThisDash;
                archive >> state;
                const bool bInputValid  = outBody._input._bJumpPressed <= SW_TRUE && outBody._input._bAttackPressed <= SW_TRUE && outBody._input._bPoundPressed <= SW_TRUE;
                const bool bFlagsValid  = outBody._bPounding <= SW_TRUE && outBody._bSuperBounce <= SW_TRUE && outBody._bShield <= SW_TRUE && outBody._bHitThisDash <= SW_TRUE;
                const bool bHitterValid = -1 <= outBody._lastHitter && outBody._lastHitter < playerCount;
                const bool bStateValid  = state <= static_cast<uint8>( TrampolinePlayerState::Respawning );
                if ( bTimersRead == false || bHeavyRead == false || archive.isError() || bInputValid == false || bFlagsValid == false || bHitterValid == false ||
                     bStateValid == false || outBody._combo < 0 )
                    return false;
                outBody._state = static_cast<TrampolinePlayerState>( state );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    TrampolineArena::TrampolineArena()
        : _listPlayer{}
        , _eventBuffer{}
        , _listMatchEvent{}
        , _settings{}
        , _judge{}
        , _itemSpawner{}
        , _match{}
        , _timer{}
        , _time{ 0.0f }
    {
    }

    bool TrampolineArena::initialize( const TrampolineSettings& settings, int32 playerCount, float32 roundTime, int32 scoreLimit, uint32 seed )
    {
        using Internal = TrampolineArenaInternal;
        if ( playerCount < Internal::kMinPlayers || playerCount > Internal::kMaxPlayers )
            return false;
        _settings              = settings;
        _settings._step        = MathUtil::max( 1.0e-3f, _settings._step );
        _settings._arenaRadius = MathUtil::max( 1.0f, _settings._arenaRadius );
        _settings._gravity     = MathUtil::max( 0.1f, _settings._gravity );
        _settings._maxCombo    = MathUtil::max( 0, _settings._maxCombo );
        if ( _judge.getWindows().empty() )
        {
            vector<TimingWindow> listWindow( 2 );
            listWindow[0]._grade      = hashed_string( "Perfect" );
            listWindow[0]._earlyWidth = 0.05f;
            listWindow[0]._lateWidth  = 0.05f;
            listWindow[1]._grade      = hashed_string( "Good" );
            listWindow[1]._earlyWidth = 0.1f;
            listWindow[1]._lateWidth  = 0.1f;
            _judge.setWindows( listWindow );
        }

        MatchSettings match;
        match._timeLimit    = MathUtil::max( 0.0f, roundTime );
        match._scoreLimit   = MathUtil::max( 0, scoreLimit );
        match._respawnDelay = _settings._respawnDelay;
        match._assistWindow = _settings._creditWindow;
        match._scorePerKill = _settings._ringOutScore;
        _match.initialize( match );
        _listPlayer.clear();
        _listPlayer.resize( static_cast<size_t>( playerCount ) );
        for ( int32 player = 0; player < playerCount; ++player )
        {
            const int32 team = _match.addTeam( Internal::getTeamName( player ) );
            (void)_match.addParticipant( team, Internal::getTeamName( player ) );
        }

        PartyItemSpawnSettings item = _itemSpawner.getSettings();
        item._center                = float3{};
        item._spawnRadius           = _settings._arenaRadius * 0.75f;
        _itemSpawner.initialize( item, seed );
        _eventBuffer.clear();
        _timer = FixedStepTimer( _settings._step, 0.25f );
        _time  = 0.0f;
        return true;
    }

    void TrampolineArena::start()
    {
        _match.start();
        for ( int32 player = 0; player < getPlayerCount(); ++player )
        {
            respawn( player );
        }
        _eventBuffer.clear(); // 첫 출발은 부활로 알리지 않는다
    }

    void TrampolineArena::setInput( int32 player, const TrampolineInput& input )
    {
        if ( isValidPlayer( player ) == false )
            return;
        // 눌림은 다음 걸음까지 모은다(프레임이 걸음보다 잦아도 잃지 않게).
        TrampolineInput& stored = _listPlayer[static_cast<size_t>( player )]._input;
        stored._moveX           = input._moveX;
        stored._moveZ           = input._moveZ;
        stored._bJumpPressed |= input._bJumpPressed;
        stored._bAttackPressed |= input._bAttackPressed;
        stored._bPoundPressed |= input._bPoundPressed;
    }

    int32 TrampolineArena::update( float32 frameTime )
    {
        const int32 stepCount = _timer.consume( frameTime );
        for ( int32 index = 0; index < stepCount; ++index )
        {
            step();
        }
        return stepCount;
    }

    void TrampolineArena::step()
    {
        if ( isRoundOver() )
            return;
        const float32 deltaTime = _settings._step;
        _time += deltaTime;
        for ( int32 player = 0; player < getPlayerCount(); ++player )
        {
            updatePlayer( player, deltaTime );
        }
        updateAttacks();
        _itemSpawner.update( deltaTime );
        pickUpItems();
        _itemSpawner.discardEvents();

        _match.update( deltaTime );
        _listMatchEvent.clear();
        _match.drainEvents( _listMatchEvent );
        for ( const MatchEvent& event : _listMatchEvent )
        {
            if ( event._kind == MatchEvent::Kind::Respawned && isValidPlayer( event._participant ) )
                respawn( event._participant );
        }
        for ( TrampolinePlayer& body : _listPlayer )
        {
            body._input._bJumpPressed   = SW_FALSE;
            body._input._bAttackPressed = SW_FALSE;
            body._input._bPoundPressed  = SW_FALSE;
        }
    }

    void TrampolineArena::writeInput( BitWriter& writer, const TrampolineInput& input )
    {
        using Internal = TrampolineArenaInternal;
        writer.writeBool( input._bJumpPressed == SW_TRUE );
        writer.writeBool( input._bAttackPressed == SW_TRUE );
        writer.writeBool( input._bPoundPressed == SW_TRUE );
        writer.writeBits( Internal::encodeAxis( input._moveX ), 8 );
        writer.writeBits( Internal::encodeAxis( input._moveZ ), 8 );
    }

    TrampolineInput TrampolineArena::readInput( BitReader& reader )
    {
        using Internal = TrampolineArenaInternal;
        TrampolineInput input;
        input._bJumpPressed   = reader.readBits( 1 ) != 0u ? SW_TRUE : SW_FALSE;
        input._bAttackPressed = reader.readBits( 1 ) != 0u ? SW_TRUE : SW_FALSE;
        input._bPoundPressed  = reader.readBits( 1 ) != 0u ? SW_TRUE : SW_FALSE;
        input._moveX          = Internal::decodeAxis( reader.readBits( 8 ) );
        input._moveZ          = Internal::decodeAxis( reader.readBits( 8 ) );
        return input;
    }

    float32 TrampolineArena::computeBounceHeight( int32 combo ) const
    {
        const int32 clamped = MathUtil::clamp( combo, 0, _settings._maxCombo );
        return _settings._baseBounceHeight + static_cast<float32>( clamped ) * _settings._comboHeightStep;
    }

    void TrampolineArena::computeRoundScores( vector<int32>& outListScore ) const
    {
        outListScore.clear();
        for ( int32 player = 0; player < getPlayerCount(); ++player )
        {
            const MatchTeam* pTeam = _match.findTeam( player );
            outListScore.push_back( pTeam != nullptr ? pTeam->_score : 0 );
        }
    }

    const TrampolinePlayer* TrampolineArena::findPlayer( int32 player ) const
    {
        return isValidPlayer( player ) ? &_listPlayer[static_cast<size_t>( player )] : nullptr;
    }

    void TrampolineArena::drainEvents( vector<TrampolineEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void TrampolineArena::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listPlayer.size() );
        for ( const TrampolinePlayer& body : _listPlayer )
        {
            TrampolineArenaInternal::writePlayer( outArchive, body );
        }
        _itemSpawner.writeState( outArchive );
        _match.writeState( outArchive );
        StateArchiveUtil::writeStepTimer( outArchive, _timer );
        outArchive << _time;
    }

    bool TrampolineArena::readState( Archive& archive )
    {
        uint32 playerCount = 0;
        archive >> playerCount;
        if ( archive.isError() || playerCount != _listPlayer.size() )
            return false;
        // 사본에 읽고 끝까지 맞으면 바꾼다 — 설정 · 판정 창 · 아이템 정의는 사본이 그대로 든다.
        TrampolineArena arena = *this;
        for ( TrampolinePlayer& body : arena._listPlayer )
        {
            if ( TrampolineArenaInternal::readPlayer( archive, static_cast<int32>( playerCount ), body ) == false )
                return false;
        }
        const bool bPartsRead = arena._itemSpawner.readState( archive ) && arena._match.readState( archive ) && StateArchiveUtil::readStepTimer( archive, arena._timer );
        archive >> arena._time;
        if ( bPartsRead == false || archive.isError() )
            return false;
        arena._eventBuffer.clear();
        arena._listMatchEvent.clear();
        *this = std::move( arena );
        return true;
    }

    // --- 걸음 -------------------------------------------------------------------------------------

    float3 TrampolineArena::makeSpawnPosition( int32 player ) const
    {
        const float32 angle  = MathUtil::kPi * 2.0f * static_cast<float32>( player ) / static_cast<float32>( MathUtil::max( 1, getPlayerCount() ) );
        const float32 radius = _settings._arenaRadius * 0.5f;
        return float3{ MathUtil::cos( angle ) * radius, _settings._spawnHeight, MathUtil::sin( angle ) * radius };
    }

    void TrampolineArena::updatePlayer( int32 player, float32 deltaTime )
    {
        using Internal         = TrampolineArenaInternal;
        TrampolinePlayer& body = _listPlayer[static_cast<size_t>( player )];
        body._attackCooldown.tick( deltaTime );
        body._stunTimer.tick( deltaTime );
        body._invulnerableTimer.tick( deltaTime );
        if ( body._heavyTimer.isActive() )
        {
            if ( body._heavyTimer.tick( deltaTime ) )
            {
                body._knockbackTaken = 1.0f;
                body._knockbackDealt = 1.0f;
            }
        }

        switch ( body._state )
        {
            case TrampolinePlayerState::Respawning:
            {
                return;
            }
            case TrampolinePlayerState::Falling:
            {
                body._velocity._y -= _settings._gravity * deltaTime;
                body._position += body._velocity * deltaTime;
                if ( body._position._y < _settings._fallDepth )
                    ringOut( player );
                return;
            }
            case TrampolinePlayerState::Contact:
            {
                // 닿기 전 창 안에서 미리 누른 것이 있으면 그것을, 없으면 닿은 뒤 처음 누른 것을 쓴다(눌러 대도 늦어지지 않게).
                const bool bNoPressYet = body._jumpPressTime < body._landTime - _judge.getEarliestWidth();
                if ( body._input._bJumpPressed == SW_TRUE && bNoPressYet )
                    body._jumpPressTime = _time;
                body._contactTimer.tick( deltaTime );
                if ( body._contactTimer.isActive() )
                    return;
                // 눌림이 끝났다 — 닿은 순간을 목표로 점프 입력을 판정한다.
                const TimingResult result   = _judge.judge( body._landTime, body._jumpPressTime );
                const bool         bSuccess = result.isHit() && result._pWindow->_bBreaksCombo == SW_FALSE;
                launch( player, bSuccess, bSuccess ? result._pWindow->_grade : hashed_string{} );
                return;
            }
            case TrampolinePlayerState::Air:
            {
                break;
            }
        }

        const TrampolineInput& input = body._input;
        if ( input._bJumpPressed == SW_TRUE )
            body._jumpPressTime = _time;
        const bool bCanControl = body._stunTimer.isActive() == false && body._bPounding == SW_FALSE;
        if ( bCanControl && input._bPoundPressed == SW_TRUE && body._position._y >= _settings._poundMinHeight )
        {
            body._bPounding = SW_TRUE;
            body._attackTimer.clear();
            body._velocity = float3{ 0.0f, -_settings._poundSpeed, 0.0f };
        }
        else if ( bCanControl && input._bAttackPressed == SW_TRUE && body._attackCooldown.isActive() == false )
        {
            float3        direction{ input._moveX, 0.0f, input._moveZ };
            const float32 length = Internal::computeFlatLength( direction );
            direction            = length > Internal::kTiny ? direction / length : body._facing;
            body._facing         = direction;
            body._attackTimer.start( _settings._attackDuration );
            body._attackCooldown.start( _settings._attackCooldown );
            body._bHitThisDash = SW_FALSE;
            body._velocity._x  = direction._x * _settings._attackDashSpeed;
            body._velocity._z  = direction._z * _settings._attackDashSpeed;
            body._velocity._y  = MathUtil::max( body._velocity._y, 0.0f );
            pushEvent( TrampolineEvent::Kind::Attacked, player, -1, 0 );
        }

        if ( body._attackTimer.isActive() )
        {
            if ( body._attackTimer.tick( deltaTime ) )
            {
                // 돌진이 끝나면 공중 이동 속도로 돌아온다.
                const float32 speed = Internal::computeFlatLength( body._velocity );
                if ( speed > _settings._airMoveSpeed )
                {
                    body._velocity._x *= _settings._airMoveSpeed / speed;
                    body._velocity._z *= _settings._airMoveSpeed / speed;
                }
            }
        }
        else if ( bCanControl )
        {
            updateAirControl( body, deltaTime );
        }
        else if ( body._bPounding == SW_FALSE )
        {
            // 밀려나는 중 — 수평 속도가 서서히 준다.
            const float32 keep = MathUtil::max( 0.0f, 1.0f - _settings._knockbackDrag * deltaTime );
            body._velocity._x *= keep;
            body._velocity._z *= keep;
        }

        if ( body._bPounding == SW_FALSE && body._attackTimer.isActive() == false )
            body._velocity._y -= _settings._gravity * deltaTime;
        body._position += body._velocity * deltaTime;

        const bool bOverArena = Internal::computeFlatLength( body._position ) <= _settings._arenaRadius;
        if ( body._position._y <= 0.0f && body._velocity._y <= 0.0f )
        {
            if ( bOverArena )
                land( player );
            else
                body._state = TrampolinePlayerState::Falling;
        }
    }

    void TrampolineArena::updateAirControl( TrampolinePlayer& body, float32 deltaTime )
    {
        using Internal = TrampolineArenaInternal;
        float3        target{ body._input._moveX, 0.0f, body._input._moveZ };
        const float32 length = Internal::computeFlatLength( target );
        if ( length > Internal::kTiny )
            body._facing = target / length;
        if ( length > 1.0f )
            target = target / length;
        target                 = target * _settings._airMoveSpeed;
        const float32 maxDelta = _settings._airAcceleration * deltaTime;
        const float32 deltaX   = target._x - body._velocity._x;
        const float32 deltaZ   = target._z - body._velocity._z;
        const float32 distance = MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ );
        const float32 scale    = distance > maxDelta ? maxDelta / distance : 1.0f;
        body._velocity._x += deltaX * scale;
        body._velocity._z += deltaZ * scale;
    }

    void TrampolineArena::land( int32 player )
    {
        using Internal         = TrampolineArenaInternal;
        TrampolinePlayer& body = _listPlayer[static_cast<size_t>( player )];
        body._position._y      = 0.0f;
        body._velocity         = float3{};
        body._state            = TrampolinePlayerState::Contact;
        body._landTime         = _time;
        body._contactTimer.start( _settings._contactTime );
        pushEvent( TrampolineEvent::Kind::Landed, player, -1, 0 );
        if ( body._bPounding == SW_FALSE )
            return;

        // 내려찍기 — 둘레의 낮은 상대를 밀어내고 콤보를 끊는다.
        body._bPounding = SW_FALSE;
        int32 pushed    = 0;
        for ( int32 other = 0; other < getPlayerCount(); ++other )
        {
            TrampolinePlayer& target   = _listPlayer[static_cast<size_t>( other )];
            const bool        bOnBoard = target._state == TrampolinePlayerState::Air || target._state == TrampolinePlayerState::Contact;
            if ( other == player || bOnBoard == false || target._invulnerableTimer.isActive() || target._position._y >= _settings._poundHitHeight )
                continue;
            float3        away     = float3{ target._position._x - body._position._x, 0.0f, target._position._z - body._position._z };
            const float32 distance = Internal::computeFlatLength( away );
            if ( distance > _settings._poundRadius )
                continue;
            away = distance > Internal::kTiny ? away / distance : float3{ 1.0f, 0.0f, 0.0f };
            knockBack( other, player, away, _settings._poundKnockback * body._knockbackDealt );
            ++pushed;
        }
        pushEvent( TrampolineEvent::Kind::GroundPound, player, -1, pushed );
        if ( body._combo > 0 )
            pushEvent( TrampolineEvent::Kind::ComboBroken, player, -1, body._combo );
        body._combo         = 0;
        body._jumpPressTime = Internal::kNoPressTime;
    }

    void TrampolineArena::launch( int32 player, bool bSuccess, const hashed_string& grade )
    {
        TrampolinePlayer& body = _listPlayer[static_cast<size_t>( player )];
        if ( bSuccess )
        {
            body._combo = MathUtil::min( body._combo + 1, _settings._maxCombo );
        }
        else
        {
            if ( body._combo > 0 )
                pushEvent( TrampolineEvent::Kind::ComboBroken, player, -1, body._combo );
            body._combo = 0;
        }
        float32 height = computeBounceHeight( body._combo );
        if ( body._bSuperBounce == SW_TRUE )
        {
            height *= 2.0f;
            body._bSuperBounce = SW_FALSE;
        }
        body._velocity._y   = MathUtil::sqrt( 2.0f * _settings._gravity * height );
        body._state         = TrampolinePlayerState::Air;
        body._jumpPressTime = TrampolineArenaInternal::kNoPressTime;
        TrampolineEvent event;
        event._kind   = TrampolineEvent::Kind::Bounced;
        event._player = player;
        event._value  = body._combo;
        event._grade  = grade;
        _eventBuffer.push( event );
    }

    void TrampolineArena::updateAttacks()
    {
        using Internal      = TrampolineArenaInternal;
        const float32 reach = _settings._bodyRadius * 2.0f + _settings._attackRadius;
        for ( int32 attacker = 0; attacker < getPlayerCount(); ++attacker )
        {
            TrampolinePlayer& body = _listPlayer[static_cast<size_t>( attacker )];
            if ( body._attackTimer.isActive() == false || body._bHitThisDash == SW_TRUE || body._state != TrampolinePlayerState::Air )
                continue;
            for ( int32 victim = 0; victim < getPlayerCount(); ++victim )
            {
                TrampolinePlayer& target   = _listPlayer[static_cast<size_t>( victim )];
                const bool        bOnBoard = target._state == TrampolinePlayerState::Air || target._state == TrampolinePlayerState::Contact;
                if ( victim == attacker || bOnBoard == false || target._invulnerableTimer.isActive() )
                    continue;
                if ( float3::getDistance( body._position, target._position ) > reach )
                    continue;
                body._bHitThisDash = SW_TRUE;
                if ( target._bShield == SW_TRUE )
                {
                    target._bShield = SW_FALSE;
                    pushEvent( TrampolineEvent::Kind::ShieldBlocked, victim, attacker, 0 );
                    break;
                }
                float3        direction = float3{ target._position._x - body._position._x, 0.0f, target._position._z - body._position._z };
                const float32 length    = Internal::computeFlatLength( direction );
                direction               = length > Internal::kTiny ? direction / length : body._facing;
                knockBack( victim, attacker, direction, _settings._attackKnockback * body._knockbackDealt );
                pushEvent( TrampolineEvent::Kind::Hit, victim, attacker, 0 );
                break;
            }
        }
    }

    void TrampolineArena::knockBack( int32 victim, int32 attacker, const float3& direction, float32 strength )
    {
        TrampolinePlayer& target = _listPlayer[static_cast<size_t>( victim )];
        const float32     power  = strength * target._knockbackTaken;
        target._velocity._x      = direction._x * power;
        target._velocity._z      = direction._z * power;
        target._velocity._y      = MathUtil::max( target._velocity._y, power * 0.3f );
        target._stunTimer.start( _settings._stunTime );
        target._attackTimer.clear();
        target._bPounding   = SW_FALSE;
        target._lastHitter  = attacker;
        target._lastHitTime = _time;
        if ( target._state == TrampolinePlayerState::Contact )
            target._state = TrampolinePlayerState::Air; // 면에서 떠밀려 난다
        _match.reportDamage( attacker, victim, 1.0f );
    }

    void TrampolineArena::ringOut( int32 player )
    {
        TrampolinePlayer& body    = _listPlayer[static_cast<size_t>( player )];
        const bool        bCredit = body._lastHitter >= 0 && _time - body._lastHitTime <= _settings._creditWindow;
        const int32       credit  = bCredit ? body._lastHitter : -1;
        _match.reportKill( player, credit );
        if ( credit < 0 && _settings._selfOutPenalty > 0 )
            _match.addScore( player, -_settings._selfOutPenalty );
        body._state    = TrampolinePlayerState::Respawning;
        body._velocity = float3{};
        body._combo    = 0;
        pushEvent( TrampolineEvent::Kind::RingOut, player, credit, 0 );
    }

    void TrampolineArena::respawn( int32 player )
    {
        TrampolinePlayer&     body  = _listPlayer[static_cast<size_t>( player )];
        const TrampolineInput input = body._input;
        body                        = TrampolinePlayer{};
        body._input                 = input;
        body._position              = makeSpawnPosition( player );
        body._invulnerableTimer.start( _settings._spawnInvulnerable );
        body._state = TrampolinePlayerState::Air;
        pushEvent( TrampolineEvent::Kind::Respawned, player, -1, 0 );
    }

    void TrampolineArena::pickUpItems()
    {
        for ( int32 player = 0; player < getPlayerCount(); ++player )
        {
            const TrampolinePlayer& body     = _listPlayer[static_cast<size_t>( player )];
            const bool              bOnBoard = body._state == TrampolinePlayerState::Air || body._state == TrampolinePlayerState::Contact;
            PartyItemInstance       item;
            if ( bOnBoard == false || _itemSpawner.tryPickUp( body._position, _settings._pickupRadius + _settings._bodyRadius, player, item ) == false )
                continue;
            const PartyItemDef* pDef = _itemSpawner.findItem( item._itemID );
            if ( pDef != nullptr )
                applyItem( player, *pDef );
            TrampolineEvent event;
            event._kind   = TrampolineEvent::Kind::ItemPicked;
            event._player = player;
            event._itemID = item._itemID;
            _eventBuffer.push( event );
        }
    }

    void TrampolineArena::applyItem( int32 player, const PartyItemDef& def )
    {
        TrampolinePlayer& body = _listPlayer[static_cast<size_t>( player )];
        if ( def._effect == TrampolineArenaInternal::getSuperBounceName() )
        {
            body._bSuperBounce = SW_TRUE;
        }
        else if ( def._effect == TrampolineArenaInternal::getHeavyName() )
        {
            body._heavyTimer.start( def._stats.getValue( TrampolineArenaInternal::getDurationName(), 8.0f ) );
            body._knockbackTaken = def._stats.getValue( TrampolineArenaInternal::getKnockbackTakenName(), 0.5f );
            body._knockbackDealt = def._stats.getValue( TrampolineArenaInternal::getKnockbackDealtName(), 1.5f );
        }
        else if ( def._effect == TrampolineArenaInternal::getShieldName() )
        {
            body._bShield = SW_TRUE;
        }
    }

    void TrampolineArena::pushEvent( TrampolineEvent::Kind kind, int32 player, int32 other, int32 value )
    {
        TrampolineEvent event;
        event._kind   = kind;
        event._player = player;
        event._other  = other;
        event._value  = value;
        _eventBuffer.push( event );
    }
} // namespace sw
