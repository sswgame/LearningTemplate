/**
 * @file MechArenaGameModeComponent.h
 * @brief 기체 대전 키트(`GF_MechArena`)의 디렉터 — 팀 대 팀 한 판을 진행합니다. 키트의 판(`MechArenaWorld`)을 플레이어 입력 명령과 컴퓨터 조종사로 고정 걸음마다 돌립니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트, 엔진 시계 · 렌더를 모른다)을 들고 씬과 잇는다. 입력 · 모습 · HUD 는 클라이언트 모듈
 *          (`GF_Client_MechArena` 의 `MechArenaViewComponent`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          조종사 번호는 플레이어(0) → 동료 → 적 순서다. 기체는 게임 기능이 건 `MechCatalog`, 무기 · 탄은 게임 범위의 `WeaponCatalog` · `ProjectileCatalog`, 근접 기술은 씬이 가리키는 `MoveCatalog` 다.
 *          판이 끝나면 `_resultHoldSeconds` 뒤 미니게임을 끝낸다(디렉터 계약 ③) — 이기면 결과 값 `money`(`_victoryMoney`). 자동 플레이면 플레이어도 컴퓨터가 쥔다.
 *          장르 완결 루프 시나리오: `game/playground/automation/mecharena_slice.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/Countdown.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/MechArena/MechArenaWorld.h"
#include "GameFramework/Kits/Genre/Action/MechArena/MechCatalog.h"
#include "GameFramework/Runtime/Character/Combat/FrameData.h"
#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/MatchGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

namespace sw
{
    /**
     * @class MechArenaGameModeComponent
     * @brief 출격 → 전투(격추 · 리스폰 · 기체 교체) → 팀 게이지나 시간으로 결과를 진행합니다.
     */
    REFLECT( Version = 2, Category = "MechArena", DisplayName = "Mech Arena Game Mode", Tooltip = "Runs one MechArena team match: pilot 0 from input commands (or the computer on auto play), the rest by the computer" )
    class SW_KIT_API MechArenaGameModeComponent : public MatchGameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr int32 kPlayer     = 0;
        static constexpr int32 kPlayerTeam = 0;
        static constexpr int32 kEnemyTeam  = 1;

        MechArenaGameModeComponent();
        virtual ~MechArenaGameModeComponent() override;

        void               serializeGameState( StateIO& io ) override;
        [[nodiscard]] bool readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const override;

        // ---- 명령(게임 스레드) ----
        /**
         * @brief 이번 프레임의 플레이어 입력을 넘깁니다. 이동 · 사격 · 부유는 마지막 값으로 덮고, 나머지 누름(대시 · 점프 · 근접 · 특수기 · 변형 · 록온 · 스킬)은 다음 걸음이 쓸 때까지 모읍니다.
         * @return 판이 열렸고 끝나지 않아 입력을 받으면 true 입니다(`isAcceptingInput`).
         */
        [[nodiscard]] bool submitPlayerInput( const MechInput& input );
        /** @brief 격추되어 리스폰을 기다리는 플레이어의 다음 기체를 덱의 다음 슬롯으로 바꿉니다. 격추되지 않았거나 덱이 하나거나 팀 게이지가 모자라면 false 입니다. */
        [[nodiscard]] bool requestNextMech();

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        /** @brief 판이 열렸고 끝나지 않아 입력 명령을 받으면 true 입니다. */
        bool                  isAcceptingInput() const { return isStarted() && _world.isEnded() == false; }
        const MechArenaWorld& getWorld() const { return _world; }
        /** @brief 조종사의 절대 자리(발밑)입니다. 없는 번호면 원점입니다. */
        AbsolutePosition getPilotPosition( int32 pilot ) const;
        /** @brief 조종사의 체력 비율(0..1)입니다. 격추됐으면 0 입니다. */
        float32 computeHealthRatio( int32 pilot ) const;
        /** @brief 플레이어가 지금 든 주무기의 탄창입니다. 주무기가 없으면 0 입니다. */
        int32 getPlayerAmmo() const;
        /** @brief 플레이어 조종사의 상태입니다(`MechPilotState` — 0 출격 전, 1 움직임, 2 다운, 3 격추, 4 판 끝). */
        int32 getPlayerState() const;
        int32 getPlayerGauge() const { return _world.getTeamGauge( kPlayerTeam ); }
        int32 getEnemyGauge() const { return _world.getTeamGauge( kEnemyTeam ); }
        /** @brief 판이 끝났으면 1 입니다. */
        int32 getEnded() const { return _world.isEnded() ? 1 : 0; }
        /** @brief 플레이어 팀이 이겼으면 true 입니다(끝난 뒤). */
        bool hasWon() const { return _world.isEnded() && _world.getMatch().getWinningTeam() == kPlayerTeam; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateMatch( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        /** @brief 이겼으면 결과 값 `money`(`_victoryMoney`)를 냅니다. 지거나 비기면 넘길 값이 없다. */
        void collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;

    private:
        /** @brief 컴퓨터 조종사의 손놀림입니다. 생각할 때 고른 옆걸음 방향을 다음 생각까지 쥔다. */
        struct BotHand
        {
            Countdown _think{};
            float32   _strafeSign{ 1.0f };
        };

        /** @brief 씬 데이터의 두 팀으로 판을 엽니다(출격은 `startGame`). 목록 길이가 다르거나 기체를 놓을 수 없으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool openWorld( MechArenaWorld& outWorld ) const;
        /** @brief 조종사 하나를 판에 더합니다. 거절되면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool addPilot( MechArenaWorld& inoutWorld, const MechPilotConfig& config );
        /** @brief 받아 둔 입력 명령으로 한 걸음의 입력을 만듭니다. 모은 누름은 여기서 비운다. */
        MechInput consumePlayerInput();
        /** @brief 컴퓨터가 쥔 @p pilot 의 한 걸음 입력입니다 — 가까운 적에게 다가가 쏘고, 거리를 지키며 옆으로 돌고, 닿으면 벤다. */
        MechInput       makeBotInput( int32 pilot, float32 deltaTime );
        optional<int32> findNearestEnemy( int32 pilot ) const;
        /** @brief 판의 알림 @p event 의 소리입니다 — 특수기는 슬롯, 근접은 기술 표, 나머지는 알림마다 하나다. 없으면 빈 이름입니다. */
        hashed_string findEventSound( const MechArenaEvent& event ) const;
        void          settle();

    private:
        PROPERTY( Category = "Match", DisplayName = "Rules", Tooltip = "Match rules (seconds, meters)" )
        MechArenaSettings _rules;
        PROPERTY( Category = "Match", DisplayName = "Move Data", Tooltip = "Move catalog of the melee weapon slots (required)" )
        AssetRef<MoveCatalog> _moveDataPath;
        PROPERTY( Category = "Player", DisplayName = "Player Deck", Tooltip = "Mech ids (catalog) the player can fly, the first one launches" )
        vector<CatalogKey<MechDef>> _listPlayerMech;
        PROPERTY( Category = "Player", DisplayName = "Player Skills", Tooltip = "Skill ids (catalog) the player equips" )
        vector<CatalogKey<MechSkillDef>> _listPlayerSkill;
        PROPERTY( Category = "Player", DisplayName = "Player Spawn", Units = m )
        float3 _playerSpawn;
        PROPERTY( Category = "Teams", DisplayName = "Ally Mechs", Tooltip = "Mech ids (catalog) of the computer pilots on the player's team, one per position" )
        vector<CatalogKey<MechDef>> _listAllyMech;
        PROPERTY( Category = "Teams", DisplayName = "Ally Spawns", Units = m )
        vector<float3> _listAllySpawn;
        PROPERTY( Category = "Teams", DisplayName = "Enemy Mechs", Tooltip = "Mech ids (catalog) of the enemy pilots, one per position" )
        vector<CatalogKey<MechDef>> _listEnemyMech;
        PROPERTY( Category = "Teams", DisplayName = "Enemy Spawns", Units = m )
        vector<float3> _listEnemySpawn;
        PROPERTY( Category = "Computer", DisplayName = "Think Interval", Tooltip = "Seconds between two decisions of a computer pilot", Min = 0.05, Units = s )
        float32 _botThinkSeconds;
        PROPERTY( Category = "Computer", DisplayName = "Keep Distance", Tooltip = "A computer pilot walks in until this close, then circles", Min = 0.1, Units = m )
        float32 _botKeepDistance;
        PROPERTY( Category = "Computer", DisplayName = "Fire Range", Min = 0.1, Units = m )
        float32 _botFireRange;
        PROPERTY( Category = "Computer", DisplayName = "Melee Range", Min = 0.1, Units = m )
        float32 _botMeleeRange;
        PROPERTY( Category = "Computer", DisplayName = "Dash Chance", Tooltip = "Chance to dash at a decision", Min = 0.0, Max = 1.0 )
        float32 _botDashChance;
        PROPERTY( Category = "Computer", DisplayName = "Special Chance", Tooltip = "Chance to use the special weapon at a decision", Min = 0.0, Max = 1.0 )
        float32 _botSpecialChance;
        PROPERTY( Category = "Result", DisplayName = "Victory Money", Tooltip = "Result value 'money' reported on victory", Min = 0 )
        int32 _victoryMoney;

        BoundAssetRef<MechCatalog>       _pMechs;
        BoundAssetRef<WeaponCatalog>     _pWeapons;
        BoundAssetRef<ProjectileCatalog> _pProjectiles;
        BoundAssetRef<MoveCatalog>       _pMoves;
        MechArenaWorld                   _world;
        FixedStepTimer                   _timer; ///< 걸음 하나가 판의 고정 걸음 하나다 — 누름은 걸음에 한 번만 든다
        GameRandom                       _random;
        vector<BotHand>                  _listBotHand;
        vector<MechArenaEvent>           _listEvent;
        MechInput                        _command; ///< 마지막 입력 명령(누름은 다음 걸음까지 모은다) — 클라이언트 입력이라 싣지 않는다
    };
} // namespace sw
