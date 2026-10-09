#include "pch.h"

#include "GameFramework/Kits/Casual/Rhythm/RhythmPlaySession.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Input/TimingJudge.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        struct RhythmPlaySessionInternal
        {
            struct DefaultRank
            {
                const utf8* _pRank;
                float32     _minAccuracy;
            };

            static constexpr DefaultRank kArrDefaultRank[] = {
                {"S", 95.0f},
                {"A", 90.0f},
                {"B", 80.0f},
                {"C", 70.0f},
                {"D",  0.0f},
            };

            /** @brief 다음에 처리할 지난 일의 종류입니다. */
            enum class DueKind : uint8
            {
                None = 0,
                Miss,
                AutoPress,
                TailComplete
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    RhythmPlaySession::RhythmPlaySession()
        : _settings{}
        , _listLane{}
        , _listGradeCount{}
        , _eventBuffer{}
        , _listInputRecord{}
        , _pChart{ nullptr }
        , _pJudge{ nullptr }
        , _score{ 0 }
        , _accuracyWeightSum{ 0.0 }
        , _life{ 0.0f }
        , _now{ 0.0f }
        , _combo{ 0 }
        , _maxCombo{ 0 }
        , _missCount{ 0 }
        , _judgedCount{ 0 }
        , _state{ RhythmPlayState::Playing }
    {
    }

    void RhythmPlaySession::initialize( const RhythmChart* pChart, const TimingJudge* pJudge, const RhythmPlaySettings& settings )
    {
        SW_ASSERT( pChart != nullptr && pJudge != nullptr );
        _pChart            = pChart;
        _pJudge            = pJudge;
        _settings          = settings;
        _settings._maxLife = MathUtil::max( 1.0f, _settings._maxLife );
        _life              = MathUtil::clamp( _settings._initialLife, 0.0f, _settings._maxLife );
        _score             = 0;
        _accuracyWeightSum = 0.0;
        _now               = -1.0e30f;
        _combo             = 0;
        _maxCombo          = 0;
        _missCount         = 0;
        _judgedCount       = 0;
        _state             = RhythmPlayState::Playing;
        _eventBuffer.clear();
        _listInputRecord.clear();
        _listGradeCount.clear();
        _listGradeCount.resize( _pJudge->getWindows().size(), 0 );

        _listLane.clear();
        _listLane.resize( static_cast<size_t>( _pChart->getLaneCount() ) );
        const vector<RhythmNote>& listNote = _pChart->getNotes();
        for ( size_t noteIndex = 0; noteIndex < listNote.size(); ++noteIndex )
        {
            _listLane[static_cast<size_t>( listNote[noteIndex]._lane )]._listNoteIndex.push_back( static_cast<int32>( noteIndex ) );
        }
        finishIfDone();
    }

    void RhythmPlaySession::update( float32 now )
    {
        if ( _pChart == nullptr || _state != RhythmPlayState::Playing )
            return;
        _now                               = MathUtil::max( _now, now );
        const vector<RhythmNote>& listNote = _pChart->getNotes();
        while ( _state == RhythmPlayState::Playing )
        {
            // 레인마다 다음 일 하나 — 가장 이른 것부터 처리해 콤보 순서가 시각 순이 되게 한다.
            RhythmPlaySessionInternal::DueKind dueKind = RhythmPlaySessionInternal::DueKind::None;
            int32                              dueLane = -1;
            float32                            dueTime = 0.0f;
            for ( size_t laneIndex = 0; laneIndex < _listLane.size(); ++laneIndex )
            {
                const LaneQueue&                   lane     = _listLane[laneIndex];
                RhythmPlaySessionInternal::DueKind kind     = RhythmPlaySessionInternal::DueKind::None;
                float32                            laneTime = 0.0f;
                if ( lane._holdingNote >= 0 )
                {
                    laneTime = computeTargetEndTime( listNote[static_cast<size_t>( lane._holdingNote )] );
                    if ( laneTime <= _now )
                        kind = RhythmPlaySessionInternal::DueKind::TailComplete;
                }
                else if ( lane._cursor < lane._listNoteIndex.size() )
                {
                    laneTime = computeTargetTime( listNote[static_cast<size_t>( lane._listNoteIndex[lane._cursor] )] );
                    if ( _settings._bAutoPlay == SW_TRUE )
                    {
                        if ( laneTime <= _now )
                            kind = RhythmPlaySessionInternal::DueKind::AutoPress;
                    }
                    else if ( _pJudge->hasExpired( laneTime, _now ) )
                    {
                        kind = RhythmPlaySessionInternal::DueKind::Miss;
                    }
                }
                const bool bEarlier = kind != RhythmPlaySessionInternal::DueKind::None && ( dueLane < 0 || laneTime < dueTime );
                if ( bEarlier )
                {
                    dueKind = kind;
                    dueLane = static_cast<int32>( laneIndex );
                    dueTime = laneTime;
                }
            }
            if ( dueLane < 0 )
                break;

            LaneQueue& lane = _listLane[static_cast<size_t>( dueLane )];
            switch ( dueKind )
            {
                case RhythmPlaySessionInternal::DueKind::None:
                {
                    break;
                }
                case RhythmPlaySessionInternal::DueKind::Miss:
                {
                    const int32 noteIndex = lane._listNoteIndex[lane._cursor];
                    ++lane._cursor;
                    applyJudgment( noteIndex, nullptr, 0.0f, false );
                    if ( listNote[static_cast<size_t>( noteIndex )]._bLong == SW_TRUE && _state == RhythmPlayState::Playing )
                        applyJudgment( noteIndex, nullptr, 0.0f, true ); // 머리를 놓친 롱노트는 끝도 놓친다
                    break;
                }
                case RhythmPlaySessionInternal::DueKind::AutoPress:
                {
                    pressInternal( dueLane, dueTime );
                    break;
                }
                case RhythmPlaySessionInternal::DueKind::TailComplete:
                {
                    const int32 noteIndex = lane._holdingNote;
                    lane._holdingNote     = -1;
                    const bool bNoWindow  = _pJudge->getWindows().empty();
                    applyJudgment( noteIndex, bNoWindow ? nullptr : &_pJudge->getWindows().front(), 0.0f, true );
                    break;
                }
            }
        }
        finishIfDone();
    }

    void RhythmPlaySession::press( int32 lane, float32 time )
    {
        const bool bValidLane = 0 <= lane && lane < static_cast<int32>( _listLane.size() );
        if ( _state != RhythmPlayState::Playing || bValidLane == false || _settings._bAutoPlay == SW_TRUE )
            return;
        RhythmInputRecord record;
        record._time   = time;
        record._lane   = lane;
        record._bPress = SW_TRUE;
        _listInputRecord.push_back( record );
        update( time );
        if ( _state != RhythmPlayState::Playing )
            return;
        pressInternal( lane, time );
        finishIfDone();
    }

    void RhythmPlaySession::release( int32 lane, float32 time )
    {
        const bool bValidLane = 0 <= lane && lane < static_cast<int32>( _listLane.size() );
        if ( _state != RhythmPlayState::Playing || bValidLane == false || _settings._bAutoPlay == SW_TRUE )
            return;
        RhythmInputRecord record;
        record._time   = time;
        record._lane   = lane;
        record._bPress = SW_FALSE;
        _listInputRecord.push_back( record );
        update( time );
        if ( _state != RhythmPlayState::Playing )
            return;
        LaneQueue& queue = _listLane[static_cast<size_t>( lane )];
        if ( queue._holdingNote < 0 )
            return;
        const int32 noteIndex = queue._holdingNote;
        queue._holdingNote    = -1;
        // 끝보다 먼저 뗐다 — 끝 시각에 판정하고, 창 밖으로 이르면 Miss.
        const RhythmNote&  note   = _pChart->getNotes()[static_cast<size_t>( noteIndex )];
        const TimingResult result = _pJudge->judge( computeTargetEndTime( note ), time );
        applyJudgment( noteIndex, result._pWindow, result.isHit() ? result._offset : 0.0f, true );
        finishIfDone();
    }

    void RhythmPlaySession::drainEvents( vector<RhythmEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void RhythmPlaySession::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _pChart != nullptr ? _pChart->getNotes().size() : 0 );
        outArchive << static_cast<uint32>( _listLane.size() );
        for ( const LaneQueue& lane : _listLane )
        {
            outArchive << static_cast<uint32>( lane._cursor );
            outArchive << lane._holdingNote;
        }
        outArchive << static_cast<uint32>( _listGradeCount.size() );
        for ( const int32 gradeCount : _listGradeCount )
        {
            outArchive << gradeCount;
        }
        outArchive << static_cast<uint32>( _listInputRecord.size() );
        for ( const RhythmInputRecord& record : _listInputRecord )
        {
            outArchive << record._time;
            outArchive << record._lane;
            outArchive << record._bPress;
        }
        outArchive << _score;
        outArchive << _accuracyWeightSum;
        outArchive << _life;
        outArchive << _now;
        outArchive << _combo;
        outArchive << _maxCombo;
        outArchive << _missCount;
        outArchive << _judgedCount;
        outArchive << static_cast<uint8>( _state );
    }

    bool RhythmPlaySession::readState( Archive& archive )
    {
        if ( _pChart == nullptr )
            return false;
        const vector<RhythmNote>& listNote  = _pChart->getNotes();
        uint32                    noteCount = 0;
        uint32                    laneCount = 0;
        archive >> noteCount;
        archive >> laneCount;
        // 같은 채보(노트 수 · 레인 수)로 `initialize` 한 판이어야 한다 — 레인 대기열은 채보에서 다시 만든 것을 쓴다.
        if ( archive.isError() || noteCount != listNote.size() || laneCount != _listLane.size() )
            return false;
        vector<LaneQueue> listLane = _listLane;
        for ( size_t laneIndex = 0; laneIndex < listLane.size(); ++laneIndex )
        {
            LaneQueue& lane   = listLane[laneIndex];
            uint32     cursor = 0;
            archive >> cursor;
            archive >> lane._holdingNote;
            const bool bHoldingValid = lane._holdingNote < 0 ||
                                       ( static_cast<size_t>( lane._holdingNote ) < listNote.size() && listNote[static_cast<size_t>( lane._holdingNote )]._lane == static_cast<int32>( laneIndex ) );
            if ( archive.isError() || cursor > lane._listNoteIndex.size() || bHoldingValid == false )
                return false;
            lane._cursor = cursor;
        }

        uint32 gradeCount = 0;
        archive >> gradeCount;
        if ( archive.isError() || gradeCount != _listGradeCount.size() )
            return false;
        vector<int32> listGradeCount( gradeCount, 0 );
        for ( int32& count : listGradeCount )
        {
            archive >> count;
        }

        uint32 recordCount = 0;
        // 입력마다 시각(4) + 레인(4) + 누름(1)
        if ( StateArchiveUtil::readCount( archive, 9, recordCount ) == false )
            return false;
        vector<RhythmInputRecord> listInputRecord( recordCount, RhythmInputRecord{} );
        for ( RhythmInputRecord& record : listInputRecord )
        {
            archive >> record._time;
            archive >> record._lane;
            archive >> record._bPress;
            if ( archive.isError() || record._bPress > SW_TRUE )
                return false;
        }

        int64   score             = 0;
        float64 accuracyWeightSum = 0.0;
        float32 life              = 0.0f;
        float32 now               = 0.0f;
        int32   combo             = 0;
        int32   maxCombo          = 0;
        int32   missCount         = 0;
        int32   judgedCount       = 0;
        uint8   state             = 0;
        archive >> score;
        archive >> accuracyWeightSum;
        archive >> life;
        archive >> now;
        archive >> combo;
        archive >> maxCombo;
        archive >> missCount;
        archive >> judgedCount;
        archive >> state;
        if ( archive.isError() || state > static_cast<uint8>( RhythmPlayState::Failed ) || combo < 0 || combo > maxCombo )
            return false;
        _listLane          = std::move( listLane );
        _listGradeCount    = std::move( listGradeCount );
        _listInputRecord   = std::move( listInputRecord );
        _score             = score;
        _accuracyWeightSum = accuracyWeightSum;
        _life              = life;
        _now               = now;
        _combo             = combo;
        _maxCombo          = maxCombo;
        _missCount         = missCount;
        _judgedCount       = judgedCount;
        _state             = static_cast<RhythmPlayState>( state );
        _eventBuffer.clear();
        return true;
    }

    float32 RhythmPlaySession::computeAccuracy() const
    {
        if ( _judgedCount == 0 )
            return 100.0f;
        return static_cast<float32>( _accuracyWeightSum / static_cast<float64>( _judgedCount ) * 100.0 );
    }

    hashed_string RhythmPlaySession::computeRank() const
    {
        if ( _state == RhythmPlayState::Failed )
            return _settings._failedRank;
        const float32 accuracy = computeAccuracy();
        hashed_string rank     = _settings._failedRank;
        float32       bestMin  = -1.0f;
        if ( _settings._listRankThreshold.empty() )
        {
            for ( const RhythmPlaySessionInternal::DefaultRank& entry : RhythmPlaySessionInternal::kArrDefaultRank )
            {
                if ( entry._minAccuracy <= accuracy && entry._minAccuracy > bestMin )
                {
                    bestMin = entry._minAccuracy;
                    rank    = hashed_string( entry._pRank );
                }
            }
            return rank;
        }
        for ( const RhythmRankThreshold& threshold : _settings._listRankThreshold )
        {
            if ( threshold._minAccuracy <= accuracy && threshold._minAccuracy > bestMin )
            {
                bestMin = threshold._minAccuracy;
                rank    = threshold._rank;
            }
        }
        return rank;
    }

    float32 RhythmPlaySession::computeNoteY( float32 noteBeat, float32 now, float32 hiSpeed ) const
    {
        if ( _pChart == nullptr )
            return 0.0f;
        return _pChart->computeNoteY( noteBeat, now - _settings._globalOffset, hiSpeed, _settings._scrollMode );
    }

    int32 RhythmPlaySession::findGradeCount( const hashed_string& grade ) const
    {
        if ( _pJudge == nullptr )
            return 0;
        const vector<TimingWindow>& listWindow = _pJudge->getWindows();
        for ( size_t windowIndex = 0; windowIndex < listWindow.size() && windowIndex < _listGradeCount.size(); ++windowIndex )
        {
            if ( listWindow[windowIndex]._grade == grade )
                return _listGradeCount[windowIndex];
        }
        return 0;
    }

    bool RhythmPlaySession::isHolding( int32 lane ) const
    {
        const bool bValidLane = 0 <= lane && lane < static_cast<int32>( _listLane.size() );
        return bValidLane && _listLane[static_cast<size_t>( lane )]._holdingNote >= 0;
    }

    void RhythmPlaySession::pressInternal( int32 lane, float32 time )
    {
        LaneQueue&  queue = _listLane[static_cast<size_t>( lane )];
        RhythmEvent emptyPress;
        emptyPress._kind  = RhythmEvent::Kind::EmptyPress;
        emptyPress._lane  = lane;
        emptyPress._combo = _combo;
        if ( queue._holdingNote >= 0 || queue._cursor >= queue._listNoteIndex.size() )
        {
            _eventBuffer.push( emptyPress );
            return;
        }
        const int32        noteIndex = queue._listNoteIndex[queue._cursor];
        const RhythmNote&  note      = _pChart->getNotes()[static_cast<size_t>( noteIndex )];
        const TimingResult result    = _pJudge->judge( computeTargetTime( note ), time );
        if ( result.isHit() == false )
        {
            // 가장 넓은 창의 이른 폭(`getEarliestWidth`)보다 이르다 — 이 노트에 쓰지 않는다. 늦은 쪽은 update 가 이미 Miss 로 넘겼다.
            emptyPress._offset = result._offset;
            _eventBuffer.push( emptyPress );
            return;
        }
        ++queue._cursor;
        applyJudgment( noteIndex, result._pWindow, result._offset, false );
        if ( note._bLong == SW_TRUE )
            queue._holdingNote = noteIndex;
    }

    void RhythmPlaySession::applyJudgment( int32 noteIndex, const TimingWindow* pWindow, float32 offset, bool bTail )
    {
        const RhythmNote& note = _pChart->getNotes()[static_cast<size_t>( noteIndex )];
        ++_judgedCount;

        RhythmEvent event;
        event._kind      = RhythmEvent::Kind::Judged;
        event._noteIndex = noteIndex;
        event._lane      = note._lane;
        event._offset    = offset;
        event._bTail     = bTail ? SW_TRUE : SW_FALSE;
        if ( pWindow == nullptr )
        {
            ++_missCount;
            _combo = 0;
            _life += _settings._missLifeDelta;
        }
        else
        {
            const vector<TimingWindow>& listWindow  = _pJudge->getWindows();
            const size_t                windowIndex = static_cast<size_t>( pWindow - listWindow.data() );
            if ( windowIndex < _listGradeCount.size() )
                ++_listGradeCount[windowIndex];
            event._grade = pWindow->_grade;
            _score += pWindow->_score;
            if ( pWindow->_bBreaksCombo == SW_TRUE )
            {
                _combo = 0;
            }
            else
            {
                ++_combo;
                _maxCombo                 = MathUtil::max( _maxCombo, _combo );
                const float64 bonusFactor = static_cast<float64>( MathUtil::min( _combo, _settings._comboBonusCap ) ) * static_cast<float64>( _settings._comboBonusPerCombo );
                _score += static_cast<int64>( static_cast<float64>( pWindow->_score ) * bonusFactor + 0.5 ); // 반올림 — 0.01 같은 float32 는 조금 모자라다
            }

            const RhythmGradeRule* pRule = findGradeRule( pWindow->_grade );
            if ( pRule != nullptr )
                _life += pRule->_lifeDelta;
            float32 weight = 1.0f;
            if ( pRule != nullptr && pRule->_accuracyWeight >= 0.0f )
                weight = pRule->_accuracyWeight;
            else if ( listWindow.front()._score > 0 )
                weight = static_cast<float32>( pWindow->_score ) / static_cast<float32>( listWindow.front()._score );
            _accuracyWeightSum += static_cast<float64>( MathUtil::clamp( weight, 0.0f, 1.0f ) );
        }
        event._combo = _combo;
        _life        = MathUtil::clamp( _life, 0.0f, _settings._maxLife );
        _eventBuffer.push( event );

        if ( _life <= 0.0f )
        {
            _state = RhythmPlayState::Failed;
            RhythmEvent failed;
            failed._kind  = RhythmEvent::Kind::Failed;
            failed._combo = _combo;
            _eventBuffer.push( failed );
        }
    }

    const RhythmGradeRule* RhythmPlaySession::findGradeRule( const hashed_string& grade ) const
    {
        for ( const RhythmGradeRule& rule : _settings._listGradeRule )
        {
            if ( rule._grade == grade )
                return &rule;
        }
        return nullptr;
    }

    void RhythmPlaySession::finishIfDone()
    {
        if ( _pChart == nullptr || _state != RhythmPlayState::Playing || _judgedCount < _pChart->getJudgmentCount() )
            return;
        _state = RhythmPlayState::Cleared;
        RhythmEvent cleared;
        cleared._kind  = RhythmEvent::Kind::Cleared;
        cleared._combo = _combo;
        _eventBuffer.push( cleared );
    }
} // namespace sw
