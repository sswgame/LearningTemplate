/**
 * @file FightingGameModeComponent.h
 * @brief 3D 격투 키트(`GF_Fighting`)의 디렉터 — 대전 한 판을 진행합니다. 키트의 대전(`FightingMatch`)을 플레이어 입력 명령(`submitPlayerInput`)과 컴퓨터 상대로 60 Hz 걸음마다 돌립니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트, 엔진 시계 · 렌더를 모른다)을 들고 씬과 잇는다. 입력 · 모습 · HUD 는 클라이언트 모듈
 *          (`GF_Client_Fighting` 의 `FightingViewComponent`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          캐릭터 수치는 카탈로그(`FighterCatalog`), 대전 규칙 · 컴퓨터 상대의 손놀림은 씬 데이터다. 대전이 끝나면 `_resultHoldSeconds` 뒤 미니게임을 끝낸다(디렉터 계약 ③) —
 *          이기면 결과 값 `money`(`_victoryMoney`)를 내보낸다. 자동 플레이면 플레이어 쪽도 컴퓨터가 쥔다.
 *          장르 완결 루프 시나리오: `game/playground/automation/fighting_slice.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/Fighting/FighterCatalog.h"
#include "GameFramework/Kits/Genre/Action/Fighting/FightingMatch.h"
#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/MatchGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

namespace sw
{
    /**
     * @class FightingGameModeComponent
     * @brief 라운드 → 대전 끝 → 결과를 플레이어 입력 명령과 컴퓨터 상대로 진행합니다. 0 번이 플레이어, 1 번이 상대입니다.
     */
    REFLECT( Category = "Fighting", DisplayName = "Fighting Game Mode", Tooltip = "Runs one Fighting kit match: player 0 from input commands (or the computer on auto play), player 1 by the computer" )
    class SW_KIT_API FightingGameModeComponent : public MatchGameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr int32 kPlayer   = 0;
        static constexpr int32 kOpponent = 1;

        FightingGameModeComponent();
        virtual ~FightingGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 명령(게임 스레드) ----
        /**
         * @brief 이번 프레임의 플레이어 입력을 넘깁니다. 방향(넘패드 1..9, 화면 기준)은 마지막 값으로 덮고, 버튼 누름(카탈로그 `buttons` 순서의 비트)은 다음 걸음이 쓸 때까지 모읍니다.
         * @return 판이 열렸고 대전이 끝나지 않아 입력을 받으면 true 입니다(`isAcceptingInput`). 방향이 1..9 밖이면 받지 않고 false 입니다.
         */
        [[nodiscard]] bool submitPlayerInput( uint8 direction, uint16 pressedButtons );

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        /** @brief 판이 열렸고 대전이 끝나지 않아 입력 명령을 받으면 true 입니다. */
        bool                 isAcceptingInput() const override;
        const FightingMatch& getMatch() const { return _match; }
        /** @brief 캐릭터의 절대 자리입니다(무대 중심이 원점, y 는 점프 · 저글 높이). */
        AbsolutePosition getFighterPosition( int32 player ) const;
        /** @brief 캐릭터의 체력 비율(0..1)입니다. 판이 열리지 않았으면 0 입니다. */
        float32 computeHealthRatio( int32 player ) const;
        /** @brief 라운드의 남은 시간(초)입니다. */
        float32 computeSecondsRemaining() const;
        int32   getOpponentHealth() const { return _match.getFighter( kOpponent )._health; }
        int32   getPlayerRounds() const { return _match.getRoundWins( kPlayer ); }
        int32   getOpponentRounds() const { return _match.getRoundWins( kOpponent ); }
        int32   getRound() const { return _match.getRound(); }
        /** @brief 대전 결과입니다(0 플레이어, 1 상대, 2 무승부, −1 끝나지 않음). */
        int32 getMatchWinner() const { return _match.getMatchWinner(); }
        int32 getPlayerState() const { return static_cast<int32>( _match.getFighter( kPlayer )._state ); }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateMatch( float32 deltaTime ) override;
        /** @brief 이겼으면 결과 값 `money`(`_victoryMoney`)를 냅니다. 지거나 비기면 넘길 값이 없다. */
        void collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;

    private:
        /** @brief 컴퓨터가 쥔 쪽의 손놀림입니다. 생각할 때 고른 방향을 다음 생각까지 쥔다. */
        struct CPUHand
        {
            int32 _thinkFramesLeft{ 0 };
            uint8 _direction{ 0 }; ///< 0 이면 아직 고르지 않았다(중립)
        };

        /** @brief 씬 데이터의 두 캐릭터를 카탈로그에서 찾아 대전을 엽니다. 없거나 규칙이 범위 밖이면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool openMatch( FightingMatch& outMatch ) const;
        /** @brief 받아 둔 입력 명령으로 한 걸음의 입력을 만듭니다. 모은 누름은 여기서 비운다. */
        InputFrame consumePlayerInput();
        /** @brief 컴퓨터가 쥔 @p player 의 한 걸음 입력입니다 — 멀면 다가가고, 닿으면 때리거나 물러선다. */
        InputFrame makeCPUInput( int32 player );
        /** @brief 알림을 로그로 내고 대전이 끝났으면 결과를 보여 주는 시간을 겁니다. */
        /** @brief 대전의 알림 하나의 소리를 쌓습니다 — 기술의 소리(시작 · 맞음 · 막힘)는 기술 표, 나머지는 알림마다 하나다. */
        void queueEventSound( const FightingEvent& event );
        void settle();

    private:
        PROPERTY( Category = "Match", DisplayName = "Player Fighter", Tooltip = "Fighter id (catalog) of player 0" )
        CatalogKey<FighterDef> _playerFighter;
        PROPERTY( Category = "Match", DisplayName = "Opponent Fighter", Tooltip = "Fighter id (catalog) of player 1" )
        CatalogKey<FighterDef> _opponentFighter;
        PROPERTY( Category = "Match", DisplayName = "Rules", Tooltip = "Match rules (frames are 1/60 s)" )
        FightingSettings _rules;
        PROPERTY( Category = "Computer", DisplayName = "Think Frames", Tooltip = "Frames between two decisions of a computer fighter", Min = 1 )
        int32 _cpuThinkFrames;
        PROPERTY( Category = "Computer", DisplayName = "Attack Range", Tooltip = "Floor distance inside which a computer fighter attacks instead of walking in", Min = 0.1, Units = m )
        float32 _cpuAttackRange;
        PROPERTY( Category = "Computer", DisplayName = "Attack Chance", Tooltip = "Chance to attack (otherwise it backs off and guards) at a decision in range", Min = 0.0, Max = 1.0 )
        float32 _cpuAttackChance;
        PROPERTY( Category = "Computer", DisplayName = "Seed", Tooltip = "Seed of the computer decisions (not 0)", Min = 1 )
        uint32 _seed;
        PROPERTY( Category = "Result", DisplayName = "Victory Money", Tooltip = "Result value 'money' reported on victory", Min = 0 )
        int32 _victoryMoney;

        BoundAssetRef<FighterCatalog> _pCatalog;
        SW_CODEC_STATE FightingMatch  _match;
        SW_CODEC_STATE FixedStepTimer _timer; ///< 걸음 하나가 격투 프레임 하나(1/60 초)다
        SW_CODEC_STATE GameRandom     _random;
        SW_CODEC_STATE CPUHand        _arrCPUHand[FightingMatch::kPlayerCount];
        SW_TRANSIENT vector<FightingEvent> _listEvent;
        SW_TRANSIENT uint16                _queuedButtons;    ///< 다음 걸음이 쓸 버튼 누름 — 클라이언트 입력이라 싣지 않는다
        SW_TRANSIENT uint8                 _commandDirection; ///< 마지막 입력 명령의 방향(넘패드) — 클라이언트 입력이라 싣지 않는다
    };
} // namespace sw
