#include "pch.h"

#include "GameFramework/Base/Gameplay/Match/MatchState.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Match/TeamAttitude.h"

namespace sw
{
    MatchState::MatchState()
        : _listTeam{}
        , _listParticipant{}
        , _eventBuffer{}
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
        _eventBuffer.clear();
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
            participant._respawnTimer.tick( deltaTime );
            if ( participant._respawnTimer.isActive() == false )
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

        const bool bEnemyKill = isValidParticipant( killer ) && TeamAttitudeUtil::isHostile( _listParticipant[static_cast<size_t>( killer )]._team, dead._team );
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
            if ( TeamAttitudeUtil::isHostile( helper._team, dead._team ) == false )
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
            dead._respawnTimer.start( _settings._respawnDelay );
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
        {
            remaining += other._bEliminated ? 0 : 1;
        }
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
        {
            bAnyCostLoss = bAnyCostLoss || ( team._bEliminated && team._bUnlimitedCost == SW_FALSE && team._costPool <= 0 );
        }
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
        {
            count += participant._team == team && participant._bEliminated == SW_FALSE ? 1 : 0;
        }
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
        _eventBuffer.push( event );
    }

    void MatchState::drainEvents( vector<MatchEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void MatchState::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listTeam.size() );
        for ( const MatchTeam& team : _listTeam )
        {
            StateArchiveUtil::writeName( outArchive, team._name );
            outArchive << team._score;
            outArchive << team._costPool;
            outArchive << team._placement;
            outArchive << team._bUnlimitedCost;
            outArchive << team._bEliminated;
        }
        outArchive << static_cast<uint32>( _listParticipant.size() );
        for ( const MatchParticipant& participant : _listParticipant )
        {
            StateArchiveUtil::writeName( outArchive, participant._role );
            outArchive << static_cast<uint32>( participant._listDamage.size() );
            for ( const MatchParticipant::DamageRecord& record : participant._listDamage )
            {
                outArchive << record._attacker;
                outArchive << record._time;
            }
            StateArchiveUtil::writeCountdown( outArchive, participant._respawnTimer );
            outArchive << participant._damageDealt;
            outArchive << participant._team;
            outArchive << participant._kills;
            outArchive << participant._deaths;
            outArchive << participant._assists;
            outArchive << participant._respawnCost;
            outArchive << participant._bAlive;
            outArchive << participant._bEliminated;
        }
        outArchive << _elapsed;
        outArchive << _phaseTime;
        outArchive << _winningTeam;
        outArchive << static_cast<uint8>( _phase );
    }

    bool MatchState::readState( Archive& archive )
    {
        uint32 teamCount = 0;
        // 팀마다 이름(4) + 정수 셋(12) + 표시 둘(2)
        if ( StateArchiveUtil::readCount( archive, 18, teamCount ) == false )
            return false;
        vector<MatchTeam> listTeam( teamCount );
        for ( MatchTeam& team : listTeam )
        {
            if ( StateArchiveUtil::readName( archive, team._name ) == false )
                return false;
            archive >> team._score;
            archive >> team._costPool;
            archive >> team._placement;
            archive >> team._bUnlimitedCost;
            archive >> team._bEliminated;
            if ( archive.isError() || team._bUnlimitedCost > SW_TRUE || team._bEliminated > SW_TRUE )
                return false;
        }
        uint32 participantCount = 0;
        // 참가자마다 이름(4) + 피해 수(4) + 타이머(4) + 피해량(4) + 정수 여섯(24) + 표시 둘(2)
        if ( StateArchiveUtil::readCount( archive, 42, participantCount ) == false )
            return false;
        vector<MatchParticipant> listParticipant( participantCount );
        for ( MatchParticipant& participant : listParticipant )
        {
            uint32 damageCount = 0;
            if ( StateArchiveUtil::readName( archive, participant._role ) == false || StateArchiveUtil::readCount( archive, 8, damageCount ) == false )
                return false;
            participant._listDamage.resize( damageCount );
            for ( MatchParticipant::DamageRecord& record : participant._listDamage )
            {
                archive >> record._attacker;
                archive >> record._time;
            }
            if ( StateArchiveUtil::readCountdown( archive, participant._respawnTimer ) == false )
                return false;
            archive >> participant._damageDealt;
            archive >> participant._team;
            archive >> participant._kills;
            archive >> participant._deaths;
            archive >> participant._assists;
            archive >> participant._respawnCost;
            archive >> participant._bAlive;
            archive >> participant._bEliminated;
            const bool bValid = archive.isOk() && -1 <= participant._team && participant._team < static_cast<int32>( teamCount ) && participant._bAlive <= SW_TRUE &&
                                participant._bEliminated <= SW_TRUE;
            if ( bValid == false )
                return false;
        }
        float32 elapsed     = 0.0f;
        float32 phaseTime   = 0.0f;
        int32   winningTeam = -1;
        uint8   phase       = 0;
        archive >> elapsed;
        archive >> phaseTime;
        archive >> winningTeam;
        archive >> phase;
        const bool bValid = archive.isOk() && phase <= static_cast<uint8>( MatchPhase::Ended ) && -1 <= winningTeam && winningTeam < static_cast<int32>( teamCount );
        if ( bValid == false )
            return false;
        _listTeam        = std::move( listTeam );
        _listParticipant = std::move( listParticipant );
        _elapsed         = elapsed;
        _phaseTime       = phaseTime;
        _winningTeam     = winningTeam;
        _phase           = static_cast<MatchPhase>( phase );
        _eventBuffer.clear();
        return true;
    }
} // namespace sw
