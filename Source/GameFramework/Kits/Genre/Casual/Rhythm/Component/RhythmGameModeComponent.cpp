#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/Rhythm/Component/RhythmGameModeComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    namespace
    {
        struct RhythmGameModeComponentInternal
        {
            static constexpr FormatDef kStateFormat{ "RHGM" };

            static constexpr const utf8* kWinResult  = "win";
            static constexpr const utf8* kLoseResult = "lose";

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventJudged     = "RhythmJudged";
            static constexpr const utf8* kEventMissed     = "RhythmMissed";
            static constexpr const utf8* kEventEmptyPress = "RhythmEmptyPress";
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( rhythmState, "Rhythm.State", "Song state (0 playing, 1 cleared, 2 failed)", RhythmGameModeComponent, &RhythmGameModeComponent::getState );
    SW_AUTOMATION_GETTER_WATCH( rhythmMusicTime, "Rhythm.MusicTime", "Music time the song is judged at (s)", RhythmGameModeComponent, &RhythmGameModeComponent::getMusicTime );
    SW_AUTOMATION_GETTER_WATCH( rhythmJudged, "Rhythm.Judged", "Judgments so far (long notes count twice)", RhythmGameModeComponent, &RhythmGameModeComponent::getJudged );
    SW_AUTOMATION_GETTER_WATCH( rhythmMissed, "Rhythm.Missed", "Missed judgments", RhythmGameModeComponent, &RhythmGameModeComponent::getMissed );
    SW_AUTOMATION_GETTER_WATCH( rhythmMaxCombo, "Rhythm.MaxCombo", "Longest combo", RhythmGameModeComponent, &RhythmGameModeComponent::getMaxCombo );
    SW_AUTOMATION_GETTER_WATCH( rhythmScore, "Rhythm.Score", "Score", RhythmGameModeComponent, &RhythmGameModeComponent::getScore );

    RhythmGameModeComponent::RhythmGameModeComponent()
        : _chart{}
        , _clock{ RhythmClock::Music }
        , _listWindow{}
        , _listRank{}
        , _missLifeDelta{ -5.0f }
        , _maxLife{ 100.0f }
        , _resultHoldSeconds{ 3.0f }
        , _clearReward{ 0 }
        , _pChart{}
        , _judge{}
        , _session{}
        , _listEvent{}
        , _resultHold{}
        , _musicTime{ 0.0f }
    {
    }

    RhythmGameModeComponent::~RhythmGameModeComponent() = default;

    bool RhythmGameModeComponent::openSession( RhythmPlaySession& outSession ) const
    {
        RhythmPlaySettings settings;
        settings._missLifeDelta     = _missLifeDelta;
        settings._maxLife           = _maxLife;
        settings._initialLife       = _maxLife;
        settings._bAutoPlay         = isAutoPlayOn() ? SW_TRUE : SW_FALSE;
        settings._listRankThreshold = _listRank;
        for ( const RhythmJudgeWindow& window : _listWindow )
        {
            RhythmGradeRule rule;
            rule._grade     = window._grade;
            rule._lifeDelta = window._lifeDelta;
            settings._listGradeRule.push_back( rule );
        }
        return outSession.initialize( *_pChart, _judge, settings ); // 거절하면 판이 까닭을 남긴다
    }

    bool RhythmGameModeComponent::startGame()
    {
        if ( acquireRequired( _chart, _pChart ) == false )
            return false;
        vector<TimingWindow> listWindow;
        for ( const RhythmJudgeWindow& window : _listWindow )
        {
            TimingWindow timing;
            timing._grade        = window._grade;
            timing._earlyWidth   = window._width;
            timing._lateWidth    = window._width;
            timing._score        = window._score;
            timing._bBreaksCombo = window._bBreaksCombo;
            listWindow.push_back( timing );
        }
        _judge.setWindows( listWindow );
        RhythmPlaySession session;
        if ( openSession( session ) == false )
            return false;
        _session = std::move( session );
        _resultHold.clear();
        return true;
    }

    void RhythmGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "Rhythm.Started", "[Rhythm] '%#' by '%#', %# lanes, %# judgments - song time and lanes arrive as commands", _pChart->getTitle().c_str(), _pChart->getArtist().c_str(),
                        _pChart->getLaneCount(), _pChart->getJudgmentCount() );
    }

    bool RhythmGameModeComponent::isAcceptingInput() const
    {
        return isStarted() && _session.isFinished() == false && isAutoPlayOn() == false;
    }

    bool RhythmGameModeComponent::submitMusicTime( float32 seconds )
    {
        if ( isStarted() == false || _clock != RhythmClock::Music || _session.isFinished() )
            return false;
        _musicTime = MathUtil::max( _musicTime, seconds );
        return true;
    }

    bool RhythmGameModeComponent::pressLane( int32 lane )
    {
        if ( isAcceptingInput() == false || lane < 0 || lane >= _pChart->getLaneCount() )
            return false;
        _session.press( lane, _musicTime );
        return true;
    }

    bool RhythmGameModeComponent::releaseLane( int32 lane )
    {
        if ( isAcceptingInput() == false || lane < 0 || lane >= _pChart->getLaneCount() )
            return false;
        _session.release( lane, _musicTime );
        return true;
    }

    void RhythmGameModeComponent::reportEvents()
    {
        _listEvent.clear();
        _session.pollEvents( _listEvent );
        for ( const RhythmEvent& event : _listEvent )
        {
            if ( event._kind == RhythmEvent::Kind::Judged )
                getSoundQueue().queueEvent( event._grade.empty() ? RhythmGameModeComponentInternal::kEventMissed : RhythmGameModeComponentInternal::kEventJudged );
            else if ( event._kind == RhythmEvent::Kind::EmptyPress )
                getSoundQueue().queueEvent( RhythmGameModeComponentInternal::kEventEmptyPress );
            else if ( event._kind == RhythmEvent::Kind::Cleared )
                SW_LOG_INFO_ID( "Rhythm.Cleared", "[Rhythm] cleared - score %#, max combo %#, accuracy %#%%, rank '%#'", _session.getScore(), _session.getMaxCombo(), _session.computeAccuracy(),
                                _session.computeRank().c_str() );
            else if ( event._kind == RhythmEvent::Kind::Failed )
                SW_LOG_INFO_ID( "Rhythm.Failed", "[Rhythm] failed after %# of %# judgments", _session.getJudgedCount(), _pChart->getJudgmentCount() );
        }
    }

    void RhythmGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        const bool bWasFinished = _session.isFinished();
        if ( bWasFinished == false )
        {
            if ( _clock == RhythmClock::Simulation )
                _musicTime += deltaTime;
            _session.update( _musicTime );
        }
        reportEvents();
        if ( _session.isFinished() == false )
            return;
        if ( bWasFinished == false )
        {
            _resultHold.start( _resultHoldSeconds ); // 끝난 걸음 — 결과를 보이기 시작한다
            return;
        }
        if ( _resultHold.update( deltaTime ) == false ) // 다 된 걸음에 한 번만 true 다
            return;
        const bool bCleared = _session.getState() == RhythmPlayState::Cleared;
        (void)finishMinigame( hashed_string( bCleared ? RhythmGameModeComponentInternal::kWinResult : RhythmGameModeComponentInternal::kLoseResult ) ); // 흐름이 없으면 베이스가 알린다
    }

    void RhythmGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        if ( _session.getState() == RhythmPlayState::Cleared )
            addMoneyResult( outListValue, _clearReward );
    }

    void RhythmGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 디렉터는 세울 오브젝트가 없다 — 모습은 클라이언트 모듈(`RhythmViewComponent`)이 읽어 보인다.
        (void)manager;
        (void)bRespawnViews;
    }

    void RhythmGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( RhythmGameModeComponentInternal::kStateFormat );
        if ( io.isOk() == false )
            return;
        // 판은 같은 채보 · 같은 판정 창으로 연 뒤에 읽는다(키트 계약).
        RhythmPlaySession restoredSession;
        Countdown         resultHold = _resultHold;
        if ( io.isReading() && io.check( _pChart != nullptr && openSession( restoredSession ) ) == false )
            return;
        io.value( io.isWriting() ? _session : restoredSession );
        io.value( resultHold );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _session    = std::move( restoredSession );
        _resultHold = resultHold;
    }
} // namespace sw

namespace sw
{
} // namespace sw
