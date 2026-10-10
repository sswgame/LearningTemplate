#include "pch.h"

#include "GameFramework/Base/Gameplay/Interaction/InteractionProgress.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Input/TimingJudge.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    float32 InteractionConfig::computeParticipantScale( int32 participantCount ) const
    {
        if ( participantCount <= 0 )
            return 0.0f;
        if ( _listParticipantScale.empty() )
            return 1.0f + static_cast<float32>( participantCount - 1 ) * _extraParticipantScale;
        const size_t index = MathUtil::min( static_cast<size_t>( participantCount ), _listParticipantScale.size() ) - 1;
        return _listParticipantScale[index];
    }

    InteractionProgress::InteractionProgress()
        : _config{}
        , _listParticipant{}
        , _eventBuffer{}
        , _pJudge{ nullptr }
        , _random{}
        , _seed{ GameRandom::kDefaultSeed }
        , _skillCheckActor{ 0 }
        , _progress{ 0.0f }
        , _time{ 0.0f }
        , _skillCheckCountdown{}
        , _skillCheckTarget{ 0.0f }
        , _bCompleted{ SW_FALSE }
        , _bSkillCheckPending{ SW_FALSE }
        , _bRegressing{ SW_FALSE }
    {
    }

    void InteractionProgress::initialize( const InteractionConfig& config, const TimingJudge* pJudge, uint32 seed )
    {
        _config                  = config;
        _config._duration        = MathUtil::max( 1.0e-3f, _config._duration );
        _config._maxParticipants = MathUtil::max( 1, _config._maxParticipants );
        _pJudge                  = pJudge;
        _seed                    = seed;
        reset();
    }

    void InteractionProgress::reset()
    {
        _listParticipant.clear();
        _eventBuffer.clear();
        _random.setSeed( _seed );
        _skillCheckActor    = 0;
        _progress           = 0.0f;
        _time               = 0.0f;
        _skillCheckTarget   = 0.0f;
        _bCompleted         = SW_FALSE;
        _bSkillCheckPending = SW_FALSE;
        _bRegressing        = SW_FALSE;
        _skillCheckCountdown.clear();
        scheduleSkillCheck();
    }

    void InteractionProgress::scheduleSkillCheck()
    {
        _skillCheckCountdown.start( _config._skillCheckInterval > 0.0f ? _config._skillCheckInterval * _random.nextRange( 0.5f, 1.5f ) : 0.0f );
    }

    void InteractionProgress::pushEvent( InteractionEvent::Kind kind, uint32 actorID, float32 value )
    {
        InteractionEvent event;
        event._kind    = kind;
        event._actorID = actorID;
        event._value   = value;
        _eventBuffer.push( event );
    }

    bool InteractionProgress::hasParticipant( uint32 actorID ) const
    {
        for ( const uint32 participant : _listParticipant )
        {
            if ( participant == actorID )
                return true;
        }
        return false;
    }

    bool InteractionProgress::join( uint32 actorID )
    {
        const bool bFull = static_cast<int32>( _listParticipant.size() ) >= _config._maxParticipants;
        if ( _bCompleted == SW_TRUE || bFull || hasParticipant( actorID ) )
            return false;
        _listParticipant.push_back( actorID );
        _bRegressing = SW_FALSE;
        pushEvent( InteractionEvent::Kind::Joined, actorID, _progress );
        return true;
    }

    bool InteractionProgress::leave( uint32 actorID )
    {
        for ( size_t index = 0; index < _listParticipant.size(); ++index )
        {
            if ( _listParticipant[index] != actorID )
                continue;
            _listParticipant.erase( _listParticipant.begin() + static_cast<ptrdiff_t>( index ) );
            if ( _bSkillCheckPending == SW_TRUE && _skillCheckActor == actorID )
                _bSkillCheckPending = SW_FALSE; // 떠난 사람의 체크는 없던 일
            pushEvent( InteractionEvent::Kind::Left, actorID, _progress );
            if ( _listParticipant.empty() && _bCompleted == SW_FALSE )
            {
                if ( _config._bResetOnInterrupt == SW_TRUE )
                    _progress = 0.0f;
                pushEvent( InteractionEvent::Kind::Interrupted, actorID, _progress );
            }
            return true;
        }
        return false;
    }

    void InteractionProgress::addProgress( float32 delta )
    {
        if ( _bCompleted == SW_TRUE )
            return;
        _progress = MathUtil::clamp( _progress + delta, 0.0f, 1.0f );
        if ( _progress < 1.0f )
            return;
        _bCompleted         = SW_TRUE;
        _bSkillCheckPending = SW_FALSE;
        pushEvent( InteractionEvent::Kind::Completed, _listParticipant.empty() ? 0u : _listParticipant.front(), 1.0f );
    }

    void InteractionProgress::setProgress( float32 progress )
    {
        _progress = MathUtil::clamp( progress, 0.0f, 1.0f );
        addProgress( 0.0f );
    }

    void InteractionProgress::update( float32 deltaTime )
    {
        if ( _bCompleted == SW_TRUE || deltaTime <= 0.0f )
            return;
        _time += deltaTime;

        if ( _listParticipant.empty() )
        {
            const bool bCanRegress = _config._regressionRate > 0.0f && _progress > 0.0f;
            if ( bCanRegress && _bRegressing == SW_FALSE )
            {
                _bRegressing = SW_TRUE;
                pushEvent( InteractionEvent::Kind::RegressionStarted, 0, _progress );
            }
            if ( bCanRegress )
                _progress = MathUtil::max( 0.0f, _progress - _config._regressionRate * deltaTime );
            return;
        }

        // 응답 없이 창이 닫힌 체크는 실패.
        if ( _bSkillCheckPending == SW_TRUE && _pJudge != nullptr && _pJudge->hasExpired( _skillCheckTarget, _time ) )
            resolveSkillCheck( false, hashed_string{} );

        const int32 participantCount = static_cast<int32>( _listParticipant.size() );
        addProgress( _config.computeParticipantScale( participantCount ) / _config._duration * deltaTime );
        if ( _bCompleted == SW_TRUE )
            return;

        const bool bSkillChecks = _pJudge != nullptr && _config._skillCheckInterval > 0.0f;
        if ( bSkillChecks && _bSkillCheckPending == SW_FALSE )
        {
            _skillCheckCountdown.tick( deltaTime );
            if ( _skillCheckCountdown.isActive() == false )
            {
                const int32 pick    = _random.nextInt( 0, participantCount - 1 );
                _skillCheckActor    = _listParticipant[static_cast<size_t>( pick )];
                _skillCheckTarget   = _time + MathUtil::max( 0.0f, _config._skillCheckLeadTime );
                _bSkillCheckPending = SW_TRUE;
                pushEvent( InteractionEvent::Kind::SkillCheckStarted, _skillCheckActor, _skillCheckTarget );
            }
        }
    }

    bool InteractionProgress::respondSkillCheck( uint32 actorID, float32 pressTime )
    {
        if ( _bSkillCheckPending == SW_FALSE || _skillCheckActor != actorID || _pJudge == nullptr )
            return false;
        const TimingResult result = _pJudge->judge( _skillCheckTarget, pressTime );
        resolveSkillCheck( result.isHit(), result.isHit() ? result._pWindow->_grade : hashed_string{} );
        return true;
    }

    void InteractionProgress::resolveSkillCheck( bool bSuccess, const hashed_string& grade )
    {
        const uint32 actorID = _skillCheckActor;
        _bSkillCheckPending  = SW_FALSE;
        scheduleSkillCheck();
        if ( bSuccess )
        {
            const float32    bonus = _config._gradeBonus.getValue( grade, _config._skillCheckBonus );
            InteractionEvent event;
            event._kind    = InteractionEvent::Kind::SkillCheckSucceeded;
            event._actorID = actorID;
            event._grade   = grade;
            event._value   = bonus;
            _eventBuffer.push( event );
            addProgress( bonus );
            return;
        }
        InteractionEvent event;
        event._kind    = InteractionEvent::Kind::SkillCheckFailed;
        event._actorID = actorID;
        event._value   = -_config._skillCheckPenalty;
        event._bNoise  = _config._bSkillCheckFailNoise;
        _eventBuffer.push( event );
        addProgress( -_config._skillCheckPenalty );
    }

    void InteractionProgress::drainEvents( vector<InteractionEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void InteractionProgress::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listParticipant.size() );
        for ( const uint32 participant : _listParticipant )
        {
            outArchive << participant;
        }
        StateArchiveUtil::writeRandom( outArchive, _random );
        outArchive << _seed;
        outArchive << _skillCheckActor;
        outArchive << _progress;
        outArchive << _time;
        StateArchiveUtil::writeCountdown( outArchive, _skillCheckCountdown );
        outArchive << _skillCheckTarget;
        outArchive << _bCompleted;
        outArchive << _bSkillCheckPending;
        outArchive << _bRegressing;
    }

    bool InteractionProgress::readState( Archive& archive )
    {
        InteractionProgress restored         = *this;
        uint32              participantCount = 0;
        if ( StateArchiveUtil::readCount( archive, 4, participantCount ) == false || participantCount > static_cast<uint32>( _config._maxParticipants ) )
            return false;
        restored._listParticipant.assign( participantCount, 0u );
        for ( uint32& participant : restored._listParticipant )
        {
            archive >> participant;
        }
        if ( StateArchiveUtil::readRandom( archive, restored._random ) == false )
            return false;
        archive >> restored._seed;
        archive >> restored._skillCheckActor;
        archive >> restored._progress;
        archive >> restored._time;
        const bool bTimerRead = StateArchiveUtil::readCountdown( archive, restored._skillCheckCountdown );
        archive >> restored._skillCheckTarget;
        archive >> restored._bCompleted;
        archive >> restored._bSkillCheckPending;
        archive >> restored._bRegressing;
        const bool bValid = bTimerRead && archive.isOk() && 0.0f <= restored._progress && restored._progress <= 1.0f && restored._bCompleted <= SW_TRUE &&
                            restored._bSkillCheckPending <= SW_TRUE && restored._bRegressing <= SW_TRUE;
        if ( bValid == false )
            return false;
        restored._eventBuffer.clear();
        *this = std::move( restored );
        return true;
    }
} // namespace sw
