/**
 * @file AsymmetricHorrorGameModeComponent.h
 * @brief 비대칭 공포 키트(`GF_AsymmetricHorror`)의 디렉터 — 생존자 여럿 대 살인마 한 판을 진행합니다. 규칙은 키트의 판(`AsymmetricHorrorMatch`), 무대는 씬 데이터(`AsymmetricHorrorStage`),
 *        살인마와 사람이 없는 생존자는 봇(`AsymmetricHorrorBot`)입니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트)을 들고 씬과 잇는다. 입력 읽기 · 모습 · HUD 는 클라이언트 모듈(`GF_Client_AsymmetricHorror`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다.
 *          생존자 0 은 늘 사람이고, 나머지는 그 생존자의 입력이 처음 온 때부터 사람이 잡는다(한 화면 협동의 중간 참가) — 그 전에는 봇이다. 자동 플레이면 모두 봇이다.
 *          판이 끝나면 결과를 잠시 보이고 미니게임을 `win` · `draw` · `lose` 로 끝낸다. 자리는 모두 절대 자리다.
 *          시나리오: `game/playground/automation/asymmetrichorror_slice.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Time/Countdown.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorBot.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorMatch.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorRules.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorStage.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"

namespace sw
{
    /** @brief 판의 결과입니다(감시 값 `AsymmetricHorror.Result` 의 수). 생존자 쪽에서 본다. */
    ENUM()
    enum class AsymmetricHorrorResult : uint8
    {
        Ongoing = 0,
        Win,
        Draw,
        Lose
    };
} // namespace sw

namespace sw
{
    /**
     * @class AsymmetricHorrorGameModeComponent
     * @brief 수리 → 전원 → 탈출구(또는 해치) → 탈출 · 희생까지 한 판을 생존자 입력 명령과 봇으로 진행합니다.
     */
    REFLECT( Category = "AsymmetricHorror", DisplayName = "Asymmetric Horror Game Mode",
             Tooltip = "Runs one asymmetric horror match: survivors from input commands or bots against a killer bot, to escape or sacrifice" )
    class SW_KIT_API AsymmetricHorrorGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        AsymmetricHorrorGameModeComponent();
        virtual ~AsymmetricHorrorGameModeComponent() override;

        void               serializeGameState( StateIO& io ) override;
        void               collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;
        [[nodiscard]] bool readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const override;

        // ---- 명령(게임 스레드) ----
        /**
         * @brief 생존자 @p survivor 의 이번 프레임 입력을 넘깁니다. 다음 걸음이 씁니다(이동 · 누르고 있음은 마지막 값, 누름은 걸음이 쓸 때까지 모은다).
         * @details 빈 입력이 아니면 그 생존자를 사람이 잡은 것으로 본다.
         * @return 판이 진행 중이고 그 생존자가 있으면 true 입니다.
         */
        [[nodiscard]] bool submitSurvivorInput( int32 survivor, const AsymmetricHorrorCommand& command );

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        const AsymmetricHorrorMatch& getMatch() const { return _match; }
        const AsymmetricHorrorStage& getStage() const { return _stage; }
        /** @brief 판이 열렸고 결과가 나지 않았으면 true 입니다. */
        bool isAcceptingInput() const { return isStarted() && _result == AsymmetricHorrorResult::Ongoing; }
        /** @brief 사람이 잡은 생존자이면 true 입니다. */
        bool isHuman( int32 survivor ) const;
        /** @brief 무대의 자리(절대)를 형식으로 감쌉니다. */
        static AbsolutePosition toAbsolute( const float3& stagePosition ) { return AbsolutePosition{ stagePosition }; }
        AsymmetricHorrorResult  getMatchResult() const { return _result; }
        int32                   getResult() const { return static_cast<int32>( _result ); }
        int32                   getCompletedGenerators() const { return _match.getCompletedGeneratorCount(); }
        /** @brief 전원까지 남은 발전기 수입니다(전원이 들어왔거나 판이 열리지 않았으면 0). */
        int32   countRemainingGenerators() const;
        int32   countOpenGates() const;
        int32   countEscaped() const { return countState( SurvivorState::Escaped ); }
        int32   countSacrificed() const { return countState( SurvivorState::Sacrificed ); }
        bool    areGatesPowered() const { return _match.areGatesPowered(); }
        float32 getPlayerX() const;
        /** @brief 생존자 0 이 하고 있는 일(`SurvivorActivity`)의 수입니다. */
        int32 getPlayerActivity() const;

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               onStateRestored( bool bRestored ) override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 씬 데이터로 판의 설정을 만듭니다. */
        AsymmetricHorrorMatchSettings makeSettings() const;
        /** @brief 생존자 하나의 명령을 판에 적용합니다. */
        void applyCommand( int32 survivor, const AsymmetricHorrorCommand& command, float32 deltaTime );
        /** @brief 쓰기 누름 — 곁에서 할 수 있는 첫 일을 합니다. 할 일이 없으면 아무것도 하지 않습니다. */
        void interact( int32 survivor );
        /** @brief 이벤트를 로그로 내고 결과를 정합니다. */
        void  settle();
        int32 countState( SurvivorState state ) const;

    private:
        PROPERTY( Category = "Match", DisplayName = "Killer", Tooltip = "Killer id (catalog) the bot plays" )
        CatalogKey<AsymmetricHorrorKillerDef> _killer;
        PROPERTY( Category = "Match", DisplayName = "Fixed Step", Min = 0.001, Max = 1.0, Units = s )
        float32 _fixedStep;
        PROPERTY( Category = "Match", DisplayName = "Seed" )
        uint32 _seed;
        PROPERTY( Category = "Match", DisplayName = "Killer Release", Tooltip = "The killer stands still this long after the match opens", Min = 0.0, Units = s )
        float32 _killerReleaseSeconds;
        PROPERTY( Category = "Match", DisplayName = "Stage" )
        AsymmetricHorrorStage _stage;
        PROPERTY( Category = "Bot", DisplayName = "Bot" )
        AsymmetricHorrorBotSettings _botSettings;
        PROPERTY( Category = "Result", DisplayName = "Result Hold", Tooltip = "How long the result stays on screen before the minigame ends", Min = 0.0, Units = s )
        float32 _resultHoldSeconds;
        PROPERTY( Category = "Result", DisplayName = "Escape Reward", Tooltip = "Money the base game gets per escaped survivor", Min = 0 )
        int32 _escapeReward;

        SharedAssetRef<AsymmetricHorrorRulesCatalog> _pCatalog;
        AsymmetricHorrorMatch                        _match;
        AsymmetricHorrorBot                          _bot;         ///< 살인마의 기억(본 자리)은 싣지 않는다 — 불러온 판은 다시 감지한다
        vector<AsymmetricHorrorCommand>              _listCommand; ///< 생존자마다 받아 둔 입력 — 클라이언트가 프레임마다 덮으므로 싣지 않는다
        vector<AsymmetricHorrorEvent>                _listEvent;   ///< 걸음마다 비워 다시 쓰는 알림 그릇
        Countdown                                    _killerRelease;
        Countdown                                    _resultHold;
        PROPERTY( RuntimeState )
        uint32 _humanMask; ///< 사람이 잡은 생존자의 비트(생존자 0 은 늘 사람이라 넣지 않는다)
        PROPERTY( RuntimeState )
        AsymmetricHorrorResult _result;
    };
} // namespace sw
