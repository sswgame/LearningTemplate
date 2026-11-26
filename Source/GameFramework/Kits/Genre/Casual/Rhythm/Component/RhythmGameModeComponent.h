/**
 * @file RhythmGameModeComponent.h
 * @brief 리듬 키트(`GF_Rhythm`)의 디렉터 — 한 곡을 진행합니다. 채보(`RhythmChart`)로 판(`RhythmPlaySession`)을 열고, 음악 시각 명령(`submitMusicTime`)과 레인 명령(`pressLane` · `releaseLane`)으로 판정합니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트, 엔진 시계 · 렌더를 모른다)을 들고 씬과 잇는다. 음악 · 입력 · 모습 · HUD 는 클라이언트 모듈(`GF_Client_Rhythm` 의 `RhythmViewComponent`)이
 *          이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다. 자동 플레이(`isAutoPlayOn`)면 판이 판정 시각에 스스로 누른다.
 *          곡의 시각은 데이터가 고른 시계 하나다(`RhythmClock`) — 음악이면 클라이언트가 오디오 구간 시계를 넘기고(벽시계를 쓰지 않는다), 시뮬레이션이면 프레임 시간을 더한다(소리 없는 판 · 테스트).
 *          곡을 깨면 `win`(상금), 라이프가 다하면 `lose` 로 미니게임을 끝낸다. 시나리오: `game/playground/automation/rhythm.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/Time/Countdown.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/Rhythm/RhythmChart.h"
#include "GameFramework/Kits/Genre/Casual/Rhythm/RhythmPlaySession.h"
#include "GameFramework/Runtime/Character/Input/TimingJudge.h"
#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/MatchGameModeComponent.h"

namespace sw
{
    /** @brief 곡의 시각을 정하는 시계입니다. */
    ENUM()
    enum class RhythmClock : uint8
    {
        Music = 0, ///< 클라이언트가 넘기는 음악 시각(`submitMusicTime`) — 넘기기 전에는 곡이 흐르지 않는다
        Simulation ///< 프레임 시간을 더한다
    };

} // namespace sw

namespace sw
{
    /**
     * @class RhythmGameModeComponent
     * @brief 곡 열기 → 판정 → 깸(또는 실패) → 결과를 진행합니다.
     */
    REFLECT( Category = "Rhythm", DisplayName = "Rhythm Game Mode", Tooltip = "Runs one Rhythm song: song time and lane presses arrive as commands, judged to a clear or a failure" )
    class SW_KIT_API RhythmGameModeComponent : public MatchGameModeComponent
    {
    public:
        REFLECT_BODY();

        RhythmGameModeComponent();
        virtual ~RhythmGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;
        void collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;

        // ---- 명령(게임 스레드) ----
        /**
         * @brief 지금 음악 시각(초)입니다. 다음 규칙 걸음이 여기까지 흘린다. 앞 시각보다 이르면 앞 시각이 이긴다(곡은 되감기지 않는다).
         * @return 음악 시계로 도는 열린 판이 아직 끝나지 않았으면 true, 아니면 받지 않고 false 입니다.
         */
        [[nodiscard]] bool submitMusicTime( float32 seconds );
        /** @brief 지금 시각에 레인 @p lane 을 누릅니다. 입력을 받지 않는 때(`isAcceptingInput`)이거나 없는 레인이면 false 입니다. */
        [[nodiscard]] bool pressLane( int32 lane );
        /** @brief 지금 시각에 레인 @p lane 을 뗍니다(롱노트 끝). 받지 않는 때 · 없는 레인이면 false 입니다. */
        [[nodiscard]] bool releaseLane( int32 lane );

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        /** @brief 판이 열렸고 진행 중이며 사람이 치면 true 입니다(자동 플레이면 false). */
        bool isAcceptingInput() const override;
        /** @brief 이 판의 채보입니다. 열기 전이면 nullptr 입니다. */
        const RhythmChart*       findChart() const { return _pChart.get(); }
        const RhythmPlaySession& getSession() const { return _session; }
        RhythmClock              getClock() const { return _clock; }
        float32                  getMusicTime() const { return _musicTime; }
        float32                  getMaxLife() const { return _playSettings._maxLife; }
        int32                    getState() const { return static_cast<int32>( _session.getState() ); }
        int32                    getJudged() const { return _session.getJudgedCount(); }
        int32                    getMissed() const { return _session.getMissCount(); }
        int32                    getMaxCombo() const { return _session.getMaxCombo(); }
        int32                    getScore() const { return static_cast<int32>( _session.getScore() ); }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateMatch( float32 deltaTime ) override;

    private:
        /** @brief 채보와 판정 칸으로 판을 엽니다. 창이 없거나 수치가 틀렸으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool openSession( RhythmPlaySession& outSession ) const;
        /** @brief 판정 이벤트를 로그로 냅니다. */
        void reportEvents();

    private:
        PROPERTY( Category = "Song", DisplayName = "Chart", Required, Tooltip = "Rhythm chart (lanes, bpm, notes)" )
        AssetRef<RhythmChart> _chart;
        PROPERTY( Category = "Song", DisplayName = "Clock", Tooltip = "Music follows the audio segment clock the client hands over; Simulation adds frame time" )
        RhythmClock _clock;
        PROPERTY( Required, Category = "Judge", DisplayName = "Play", Tooltip = "Judgment windows, ranks and life of the song; every value is required" )
        RhythmPlaySettings _playSettings;
        PROPERTY( Category = "Match", DisplayName = "Clear Reward", Tooltip = "Money for clearing the song", Min = 0 )
        int32 _clearReward;

        BoundAssetRef<RhythmChart>       _pChart;
        SW_TRANSIENT TimingJudge         _judge; ///< 판이 빌려 쓴다 — 판보다 먼저 선언한다
        SW_CODEC_STATE RhythmPlaySession _session;
        SW_TRANSIENT vector<RhythmEvent> _listEvent;
        PROPERTY( RuntimeState, Min = 0 )
        float32 _musicTime; ///< 지금 곡 시각(초)
    };
} // namespace sw
