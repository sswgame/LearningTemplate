#include "pch.h"

#include "Games/Empty/KitDemo/RhythmDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Casual/Rhythm/RhythmPlaySession.h"
#include "GameFramework/Runtime/Character/Input/TimingJudge.h"

namespace sw
{
    namespace
    {
        struct RhythmDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDRH" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr const utf8* kWindowGrade = "Cool"; ///< 판정 창 하나 — 오토플레이는 판정 시각에 정확히 눌러 늘 이 창에 든다
            static constexpr float32     kWindowWidth = 0.1f;   ///< 이르고 늦은 폭(초)
            static constexpr int32       kWindowScore = 100;    ///< 판정 하나의 점수
            static constexpr float32     kTailMargin  = 1.0f;   ///< 마지막 판정 뒤로 더 흘리는 시간(초)
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoRhythmState, "KitDemo.Rhythm.State", "Rhythm demo state (0 not started, 1 song run finished)", RhythmDemoComponent, &RhythmDemoComponent::getDemoState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoRhythmScore, "KitDemo.Rhythm.Score", "Score after the autoplayed song", RhythmDemoComponent, &RhythmDemoComponent::getScore );
    SW_AUTOMATION_GETTER_WATCH( kitDemoRhythmMaxCombo, "KitDemo.Rhythm.MaxCombo", "Longest combo of the autoplayed song", RhythmDemoComponent, &RhythmDemoComponent::getMaxCombo );
    SW_AUTOMATION_GETTER_WATCH( kitDemoRhythmCleared, "KitDemo.Rhythm.Cleared", "1 when the song ended cleared", RhythmDemoComponent, &RhythmDemoComponent::getCleared );

    RhythmDemoComponent::RhythmDemoComponent()
        : _catalogPath{}
        , _pChart{}
        , _score{ 0 }
        , _maxCombo{ 0 }
        , _cleared{ 0 }
    {
    }

    RhythmDemoComponent::~RhythmDemoComponent() = default;

    bool RhythmDemoComponent::runChart()
    {
        TimingWindow window;
        window._grade      = hashed_string( RhythmDemoComponentInternal::kWindowGrade );
        window._earlyWidth = RhythmDemoComponentInternal::kWindowWidth;
        window._lateWidth  = RhythmDemoComponentInternal::kWindowWidth;
        window._score      = RhythmDemoComponentInternal::kWindowScore;
        TimingJudge judge;
        judge.setWindows( { window } );

        RhythmPlaySettings settings;
        settings._bAutoPlay = SW_TRUE;
        RhythmPlaySession session;
        if ( session.initialize( _pChart.get(), &judge, settings ) == false )
            return false;
        // 오토플레이는 판정 시각에 누르고 롱노트를 끝까지 누른다 — 곡 끝까지 한 번에 흘리면 모든 판정이 끝나 곡을 깬다.
        session.update( _pChart->getEndTime() + RhythmDemoComponentInternal::kTailMargin );
        _score    = static_cast<int32>( session.getScore() );
        _maxCombo = session.getMaxCombo();
        _cleared  = session.getState() == RhythmPlayState::Cleared ? 1 : 0;
        markRunFinished();
        return true;
    }

    bool RhythmDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pChart ) == false )
            return false;
        return runChart();
    }

    void RhythmDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.RhythmDone", "[KitDemo] rhythm: score %#, max combo %#, cleared %#", _score, _maxCombo, _cleared );
    }

    void RhythmDemoComponent::serializeState( StateIO& io )
    {
        serializeResults( io, RhythmDemoComponentInternal::kStateTag, RhythmDemoComponentInternal::kStateVersion, { &_score, &_maxCombo, &_cleared } );
    }
} // namespace sw
