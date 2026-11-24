/**
 * @file CardBattleGameModeComponent.h
 * @brief 카드 게임 키트(`GF_CardGame`)의 혼자 하는 판 디렉터 — 덱 전투(`CardBattle`) 한 판을 진행합니다. 카드 내기 · 턴 끝은 명령(`playCard` · `endTurn`)으로만 들어옵니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트, 엔진 시계 · 렌더를 모른다)을 들고 씬과 잇는다. 입력 · 모습 · HUD 는 클라이언트 모듈(`GF_Client_CardGame` 의 `CardBattleViewComponent`)이
 *          이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다. 자동 플레이(`isAutoPlayOn`)면 낼 수 있는 앞 패부터 내고 턴을 마친다.
 *          둘이 하는 판(허브의 카드 탁자)은 서버가 행동을 중계 · 판정하는 셰딩 카드 판이라 디렉터가 다르다 — 여기는 서버 없이 한 프로세스에서 끝나는 판이다.
 *          덱 · 상대는 카탈로그(`CardBattleCatalog` 의 `<Deck>` · `<Enemy>`)다. 이기면 `win`(상금), 쓰러지면 `lose` 로 미니게임을 끝낸다.
 *          시나리오: `game/playground/automation/cardgame.scenario.xml`.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Time/Countdown.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/CardBattle/CardBattle.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"

namespace sw
{
    /**
     * @class CardBattleGameModeComponent
     * @brief 덱 섞기 → 내 턴(카드 내기) ↔ 상대 턴 → 승패 → 결과를 진행합니다.
     */
    REFLECT( Category = "CardGame", DisplayName = "Card Battle Game Mode", Tooltip = "Runs one deck battle of the card kit from play card and end turn commands, to victory or defeat" )
    class SW_KIT_API CardBattleGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        CardBattleGameModeComponent();
        virtual ~CardBattleGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;
        void collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;

        // ---- 명령(게임 스레드) ----
        /** @brief 손의 @p handIndex 번째 카드를 냅니다. 명령을 받지 않는 때(`isAwaitingCommand`)이거나 낼 수 없는 카드(없는 자리 · 에너지 부족)면 false 입니다. */
        [[nodiscard]] bool playCard( int32 handIndex );
        /** @brief 내 턴을 마칩니다(상대가 할 일을 하고 다음 턴이 열린다). 명령을 받지 않는 때면 false 입니다. */
        [[nodiscard]] bool endTurn();

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        /** @brief 판이 열렸고 내 턴이며 사람이 하면 true 입니다(자동 플레이면 false). */
        bool              isAwaitingCommand() const;
        const CardBattle& getBattle() const { return _battle; }
        /** @brief 손의 @p handIndex 번째 카드를 지금 낼 수 있으면 true 입니다(에너지가 된다). */
        bool isPlayable( int32 handIndex ) const;
        /** @brief 상대가 이번 턴 끝에 할 일입니다. 할 일이 없으면 nullptr 입니다. */
        const CardBattleEffect* findEnemyIntent() const;
        int32                   getPhase() const { return static_cast<int32>( _battle.getPhase() ); }
        int32                   getTurn() const { return _battle.getTurn(); }
        int32                   getPlayerHP() const { return _battle.getPlayerHP(); }
        int32                   getEnemyHP() const { return _battle.getEnemy()._hp; }
        int32                   getHandCount() const { return _battle.getHand().getCount(); }
        int32                   getEnergy() const { return _battle.getEnergy(); }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 카탈로그의 덱 · 상대로 @p outBattle 을 엽니다. 없는 덱 · 상대이거나 덱이 비면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool openBattle( CardBattle& outBattle ) const;
        /** @brief 자동 플레이의 한 수 — 낼 수 있는 앞 패를 내거나, 없으면 턴을 마칩니다. */
        void stepAutoPlay();
        /** @brief 승패가 났으면 로그를 남기고 결과 보이기를 시작합니다. */
        void settle();

    private:
        PROPERTY( Category = "Battle", DisplayName = "Deck", Required, Tooltip = "Deck id in the catalog" )
        CatalogKey<CardBattleDeckDef> _deckID;
        PROPERTY( Category = "Battle", DisplayName = "Enemy", Required, Tooltip = "Enemy id in the catalog" )
        CatalogKey<CardBattleEnemyDef> _enemyID;
        PROPERTY( Category = "Battle", DisplayName = "Player HP", Min = 1 )
        int32 _playerHP;
        PROPERTY( Category = "Battle", DisplayName = "Energy", Tooltip = "Energy every turn", Min = 0 )
        int32 _energyPerTurn;
        PROPERTY( Category = "Battle", DisplayName = "Draw", Tooltip = "Cards drawn every turn", Min = 1 )
        int32 _drawPerTurn;
        PROPERTY( Category = "Battle", DisplayName = "Seed" )
        uint32 _seed;
        PROPERTY( Category = "Match", DisplayName = "Auto Play Interval", Tooltip = "Time between two auto play moves", Min = 0.01, Units = s )
        float32 _autoPlayInterval;
        PROPERTY( Category = "Match", DisplayName = "Result Hold", Tooltip = "Time the result stays on screen before the minigame ends", Min = 0.1, Units = s )
        float32 _resultHoldSeconds;
        PROPERTY( Category = "Match", DisplayName = "Win Reward", Tooltip = "Money for winning the battle", Min = 0 )
        int32 _winReward;

        SharedAssetRef<CardBattleCatalog> _pCatalog;
        CardBattle                        _battle;
        Countdown                         _timer; ///< 자동 플레이의 다음 수 · 결과를 보이는 시간
        PROPERTY( RuntimeState, Min = 0, Max = 1 )
        uint8 _bResultShown : 1; ///< 결과 보이기를 시작했다
        PROPERTY( RuntimeState, Min = 0, Max = 1 )
        uint8                  _bFinishRequested : 1; ///< 미니게임 끝내기를 맡겼다
        [[maybe_unused]] uint8 _reserved         : 6;
    };
} // namespace sw
