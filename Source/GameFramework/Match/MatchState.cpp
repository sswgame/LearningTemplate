#include "pch.h"

#include "GameFramework/Match/MatchState.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    MatchState::MatchState()
        : _listTeam{}
        , _listParticipant{}
        , _listEvent{}
        , _settings{}
        , _elapsed{ 0.0f }
        , _phaseTime{ 0.0f }
        , _winningTeam{ -1 }
        , _phase{ MatchPhase::Waiting }
    {
    }

    void MatchState::initialize( const MatchSettings& settings )
    {
        _settings = settings;
        _listTeam.clear();
        _listParticipant.clear();
        _listEvent.clear();
        _elapsed     = 0.0f;
        _phaseTime   = 0.0f;
        _winningTeam = -1;
        _phase       = MatchPhase::Waiting;
    }

    int32 MatchState::addTeam( const hashed_string& name, int32 costPool )
    {
        MatchTeam team;
        team._name           = name;
        team._costPool       = MathUtil::max( 0, costPool );
        team._bUnlimitedCost = costPool <= 0 ? SW_TRUE : SW_FALSE;
        _listTeam.push_back( team );
        return static_cast<int32>( _listTeam.size() ) - 1;
    }

    int32 MatchState::addParticipant( int32 team, const hashed_string& role, int32 respawnCost )
    {
        if ( isValidTeam( team ) == false )
            return -1;
        MatchParticipant participant;
        participant._team        = team;
        participant._role        = role;
        participant._respawnCost = MathUtil::max( 0, respawnCost );
        _listParticipant.push_back( participant );
        return static_cast<int32>( _listParticipant.size() ) - 1;
    }

    void MatchState::setPhase( MatchPhase phase )
    {
        if ( _phase == phase )
            return;
        _phase     = phase;
        _phaseTime = 0.0f;
        pushEvent( MatchEvent::Kind::PhaseChanged, -1, -1, -1, static_cast<int32>( phase ) );
    }

    void MatchState::start()
    {
        if ( _phase != MatchPhase::Waiting )
            return;
        setPhase( _settings._warmupTime > 0.0f ? MatchPhase::Warmup : MatchPhase::InProgress );
    }

    float32 MatchState::getRemaining() const
    {
        if ( _settings._timeLimit <= 0.0f )
            return 0.0f;
        return MathUtil::max( 0.0f, _settings._timeLimit - ( _phase == MatchPhase::InProgress ? _phaseTime : 0.0f ) );
    }

    void MatchState::update( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f || _phase == MatchPhase::Waiting || _phase == MatchPhase::Ended )
            return;
        _elapsed += deltaTime;
        _phaseTime += deltaTime;
        if ( _phase == MatchPhase::Warmup )
        {
            if ( _phaseTime >= _settings._warmupTime )
                setPhase( MatchPhase::InProgress );
            return;
        }
        for ( size_t index = 0; index < _listParticipant.size(); ++index )
        {
            MatchParticipant& participant = _listParticipant[index];
            if ( participant._bAlive || participant._bEliminated )
                continue;
            participant._respawnTimer -= deltaTime;
            if ( participant._respawnTimer <= 0.0f )
            {
                participant._bAlive = SW_TRUE;
                pushEvent( MatchEvent::Kind::Respawned, static_cast<int32>( index ), -1, participant._team, 0 );
            }
        }
        if ( _settings._timeLimit > 0.0f && _phaseTime >= _settings._timeLimit )
            finishByScore();
    }

    void MatchState::reportDamage( int32 attacker, int32 victim, float32 amount )
    {
        if ( isValidParticipant( victim ) == false || amount <= 0.0f )
            return;
        if ( isValidParticipant( attacker ) )
        {
            _listParticipant[static_cast<size_t>( attacker )]._damageDealt += amount;
            vector<MatchParticipant::DamageRecord>& listDamage = _listParticipant[static_cast<size_t>( victim )]._listDamage;
            for ( MatchParticipant::DamageRecord& record : listDamage )
            {
                if ( record._attacker == attacker )
                {
                    record._time = _elapsed;
                    return;
                }
            }
            listDamage.push_back( MatchParticipant::DamageRecord{ attacker, _elapsed } );
        }
    }

    void MatchState::reportKill( int32 victim, int32 killer )
    {
        if ( _phase != MatchPhase::InProgress || isValidParticipant( victim ) == false )
            return;
        MatchParticipant& dead = _listParticipant[static_cast<size_t>( victim )];
        if ( dead._bAlive == SW_FALSE )
            return;
        dead._bAlive = SW_FALSE;
        ++dead._deaths;
        pushEvent( MatchEvent::Kind::Killed, victim, killer, dead._team, 0 );

        const bool bEnemyKill = isValidParticipant( killer ) && _listParticipant[static_cast<size_t>( killer )]._team != dead._team;
        if ( bEnemyKill )
        {
            MatchParticipant& slayer = _listParticipant[static_cast<size_t>( killer )];
            ++slayer._kills;
            if ( _settings._scorePerKill != 0 )
                addScore( slayer._team, _settings._scorePerKill );
        }
        for ( const MatchParticipant::DamageRecord& record : dead._listDamage )
        {
            if ( record._attacker == killer || _elapsed - record._time > _settings._assistWindow )
                continue;
            MatchParticipant& helper = _listParticipant[static_cast<size_t>( record._attacker )];
            if ( helper._team == dead._team )
                continue;
            ++helper._assists;
            pushEvent( MatchEvent::Kind::Assisted, record._attacker, victim, helper._team, 0 );
        }
        dead._listDamage.clear();

        MatchTeam& team = _listTeam[static_cast<size_t>( dead._team )];
        if ( team._bUnlimitedCost == SW_FALSE )
            team._costPool -= dead._respawnCost;
        if ( _settings._bRespawn == SW_FALSE )
        {
            eliminate( victim );
        }
        else
        {
            dead._respawnTimer = _settings._respawnDelay;
            if ( team._bUnlimitedCost == SW_FALSE && team._costPool <= 0 )
                eliminateTeam( dead._team ); // 전력 게이지가 바닥났다
        }
        resolveEndConditions();
    }

    void MatchState::eliminate( int32 participant )
    {
        if ( isValidParticipant( participant ) == false )
            return;
        MatchParticipant& target = _listParticipant[static_cast<size_t>( participant )];
        if ( target._bEliminated )
            return;
        target._bEliminated = SW_TRUE;
        target._bAlive      = SW_FALSE;
        pushEvent( MatchEvent::Kind::ParticipantEliminated, participant, -1, target._team, 0 );
        if ( countStanding( target._team ) == 0 )
            eliminateTeam( target._team );
        resolveEndConditions();
    }

    void MatchState::eliminateTeam( int32 team )
    {
        MatchTeam& target = _listTeam[static_cast<size_t>( team )];
        if ( target._bEliminated )
            return;
        target._bEliminated = SW_TRUE;
        for ( size_t index = 0; index < _listParticipant.size(); ++index )
        {
            MatchParticipant& participant = _listParticipant[index];
            if ( participant._team == team && participant._bEliminated == SW_FALSE )
            {
                participant._bEliminated = SW_TRUE;
                participant._bAlive      = SW_FALSE;
            }
        }
        int32 remaining = 0;
        for ( const MatchTeam& other : _listTeam )
            remaining += other._bEliminated ? 0 : 1;
        target._placement = remaining + 1;
        pushEvent( MatchEvent::Kind::TeamEliminated, -1, -1, team, target._placement );
    }

    void MatchState::addScore( int32 team, int32 amount )
    {
        if ( isValidTeam( team ) == false || amount == 0 || _phase == MatchPhase::Ended )
            return;
        MatchTeam& target = _listTeam[static_cast<size_t>( team )];
        target._score += amount;
        pushEvent( MatchEvent::Kind::ScoreChanged, -1, -1, team, target._score );
        if ( _settings._scoreLimit > 0 && target._score >= _settings._scoreLimit )
            endMatch( team );
    }

    void MatchState::resolveEndConditions()
    {
        if ( _phase == MatchPhase::Ended )
            return;
        int32 standingTeam  = -1;
        int32 standingCount = 0;
        for ( size_t index = 0; index < _listTeam.size(); ++index )
        {
            if ( _listTeam[index]._bEliminated == SW_FALSE )
            {
                standingTeam = static_cast<int32>( index );
                ++standingCount;
            }
        }
        // 게이지가 바닥난 팀이 생기면 늘 끝난다(기체 대전). 섬멸 규칙이면 하나 남을 때.
        bool bAnyCostLoss = false;
        for ( const MatchTeam& team : _listTeam )
            bAnyCostLoss = bAnyCostLoss || ( team._bEliminated && team._bUnlimitedCost == SW_FALSE && team._costPool <= 0 );
        if ( standingCount == 0 )
            endMatch( -1 );
        else if ( standingCount == 1 && ( _settings._bLastTeamStandingWins || bAnyCostLoss ) && _listTeam.size() > 1 )
            endMatch( standingTeam );
    }

    void MatchState::finishByScore()
    {
        int32 bestTeam  = -1;
        int32 bestScore = 0;
        bool  bTie      = false;
        for ( size_t index = 0; index < _listTeam.size(); ++index )
        {
            const MatchTeam& team = _listTeam[index];
            if ( team._bEliminated )
                continue;
            // 코스트제는 남은 게이지가 점수다.
            const int32 score = team._bUnlimitedCost ? team._score : team._costPool;
            if ( bestTeam < 0 || score > bestScore )
            {
                bestTeam  = static_cast<int32>( index );
                bestScore = score;
                bTie      = false;
            }
            else if ( score == bestScore )
            {
                bTie = true;
            }
        }
        endMatch( bTie ? -1 : bestTeam );
    }

    void MatchState::endMatch( int32 winningTeam )
    {
        if ( _phase == MatchPhase::Ended )
            return;
        _winningTeam = isValidTeam( winningTeam ) ? winningTeam : -1;
        if ( _winningTeam >= 0 )
            _listTeam[static_cast<size_t>( _winningTeam )]._placement = 1;
        setPhase( MatchPhase::Ended );
        pushEvent( MatchEvent::Kind::MatchEnded, -1, -1, _winningTeam, 0 );
    }

    const MatchTeam* MatchState::findTeam( int32 team ) const { return isValidTeam( team ) ? &_listTeam[static_cast<size_t>( team )] : nullptr; }

    const MatchParticipant* MatchState::findParticipant( int32 participant ) const
    {
        return isValidParticipant( participant ) ? &_listParticipant[static_cast<size_t>( participant )] : nullptr;
    }

    int32 MatchState::countStanding( int32 team ) const
    {
        int32 count = 0;
        for ( const MatchParticipant& participant : _listParticipant )
            count += participant._team == team && participant._bEliminated == SW_FALSE ? 1 : 0;
        return count;
    }

    void MatchState::pushEvent( MatchEvent::Kind kind, int32 participant, int32 other, int32 team, int32 value )
    {
        MatchEvent event;
        event._kind        = kind;
        event._participant = participant;
        event._other       = other;
        event._team        = team;
        event._value       = value;
        _listEvent.push_back( event );
    }

    void MatchState::drainEvents( vector<MatchEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }
} // namespace sw
