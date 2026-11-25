/**
 * @file PartyGameModeComponent.h
 * @brief 파티 아레나 키트(`GF_PartyArena`)의 디렉터 — 라운드 시리즈 한 판을 진행합니다. 라운드(`PartyRoundSeries`)마다 트램펄린 무대(`TrampolineArena`)를 열고, 사람은 명령(`submitPlayerInput`)으로, 나머지는 봇(`TrampolineBot`)으로 겨룹니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트, 엔진 시계 · 렌더를 모른다)을 들고 씬과 잇는다. 입력 · 모습 · HUD 는 클라이언트 모듈(`GF_Client_PartyArena` 의 `PartyArenaViewComponent`)이
 *          이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다. 자동 플레이(`isAutoPlayOn`)면 사람도 봇이 움직인다.
 *          무대 좌표는 절대 자리(가운데가 원점)다. 먼저 우승 점수에 닿거나 라운드 상한까지 하면 끝난다 — 상한에서는 총점이 가장 높은 한 명이 이기고 같으면 무승부다.
 *          우승하면 `win`(상금), 아니면 `lose` 로 미니게임을 끝낸다. 시나리오: `game/playground/automation/partyarena.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Time/Countdown.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/PartyArena/PartyItemSpawner.h"
#include "GameFramework/Kits/Genre/Casual/PartyArena/PartyRoundSeries.h"
#include "GameFramework/Kits/Genre/Casual/PartyArena/TrampolineArena.h"
#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/MatchGameModeComponent.h"

namespace sw
{
    struct AbsolutePosition;

    /** @brief 판의 단계입니다(감시 값 `PartyArena.Phase` 의 수). */
    ENUM()
    enum class PartyGamePhase : uint8
    {
        Round = 0,    ///< 라운드를 겨루는 중
        Intermission, ///< 라운드 사이에 쉰다
        Finished      ///< 우승자가 정해졌다(무승부면 우승자가 없다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class PartyGameModeComponent
     * @brief 라운드 열기 → 겨루기 → 순위 점수 → 다음 라운드 → 우승을 진행합니다.
     */
    REFLECT( Version = 3, Category = "PartyArena", DisplayName = "Party Game Mode", Tooltip = "Runs one PartyArena round series on the trampoline arena: the player from input commands, the rivals by bots" )
    class SW_KIT_API PartyGameModeComponent : public MatchGameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr int32 kLocalPlayer = 0;  ///< 사람의 번호다
        static constexpr int32 kNoWinner    = -1; ///< 우승자가 없다(끝나지 않았거나 무승부)

        PartyGameModeComponent();
        virtual ~PartyGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;
        void collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;

        // ---- 명령(게임 스레드) ----
        /** @brief 사람의 이번 프레임 입력입니다(눌림은 다음 고정 걸음까지 남는다 — `TrampolineArena::setInput`). 입력을 받지 않는 때(`isAcceptingInput`)면 false 입니다. */
        [[nodiscard]] bool submitPlayerInput( const TrampolineInput& input );

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        /** @brief 라운드를 겨루는 중이고 사람이 움직이면 true 입니다(자동 플레이면 false). */
        bool                    isAcceptingInput() const;
        const TrampolineArena&  getArena() const { return _arena; }
        const PartyRoundSeries& getSeries() const { return _series; }
        /** @brief 사람 @p player 의 절대 자리입니다. 없는 번호면 원점입니다. */
        AbsolutePosition getPlayerPosition( int32 player ) const;
        PartyGamePhase   getGamePhase() const { return _phase; }
        int32            getPhase() const { return static_cast<int32>( _phase ); }
        int32            getRound() const { return _series.getRoundNumber(); }
        int32            getWinner() const { return _winner; }
        int32            getPlayerRoundScore() const;
        int32            getPlayerTotal() const;
        int32            getPlayerRingOutCount() const { return _playerRingOutCount; }
        float32          getPlayerX() const;

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateMatch( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 카탈로그의 라운드 · 점수로 시리즈를 엽니다. 열지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool openSeries( PartyRoundSeries& outSeries ) const;
        /** @brief 시리즈 @p series 의 지금 라운드로 무대를 준비합니다(출발은 하지 않는다). 설정이 틀렸으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool openRound( const PartyRoundSeries& series, TrampolineArena& outArena ) const;
        /** @brief 무대 · 시리즈 이벤트를 로그로 냅니다. */
        void reportEvents();
        /** @brief 끝난 라운드의 점수를 시리즈에 넣고 다음 단계로 넘깁니다. */
        void settleRound();
        /** @brief 우승자 @p winner 로 판을 끝냅니다. */
        void finishSeries( int32 winner );
        /** @brief 라운드 상한에서 총점이 가장 높은 한 명입니다. 같은 총점이 둘이면 `kNoWinner` 입니다. */
        int32 findLeader() const;

    private:
        PROPERTY( Category = "Data", DisplayName = "Items", Tooltip = "Arena item file; empty plays without items" )
        AssetRef<PartyItemSpawner> _items;
        PROPERTY( Category = "Match", DisplayName = "Bots", Tooltip = "Players moved by bots", Min = 1, Max = 3 )
        int32 _botCount;
        PROPERTY( Category = "Match", DisplayName = "Round Limit", Tooltip = "The series ends after this many rounds even without a winner", Min = 1 )
        int32 _roundLimit;
        PROPERTY( Category = "Match", DisplayName = "Intermission", Min = 0.1, Units = s )
        float32 _intermissionSeconds;
        PROPERTY( Category = "Match", DisplayName = "Seed" )
        uint32 _seed;
        PROPERTY( Category = "Match", DisplayName = "Win Reward", Tooltip = "Money for winning the series", Min = 0 )
        int32 _winReward;
        PROPERTY( Category = "Arena", DisplayName = "Arena Settings" )
        TrampolineSettings _arenaSettings;

        BoundAssetRef<PartyRoundSeries> _pCatalog;
        BoundAssetRef<PartyItemSpawner> _pItems; ///< 아이템 없는 판이면 비었다
        PartyRoundSeries                _series;
        TrampolineArena                 _arena;
        vector<TrampolineEvent>         _listArenaEvent;
        vector<PartyItemEvent>          _listItemEvent;
        vector<PartySeriesEvent>        _listSeriesEvent;
        Countdown                       _phaseTimer; ///< 쉬는 시간
        PROPERTY( RuntimeState )
        PartyGamePhase _phase;
        PROPERTY( RuntimeState, Min = -1 )
        int32 _winner;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _playerRingOutCount; ///< 사람이 떨어진 수(모든 라운드)
    };
} // namespace sw
