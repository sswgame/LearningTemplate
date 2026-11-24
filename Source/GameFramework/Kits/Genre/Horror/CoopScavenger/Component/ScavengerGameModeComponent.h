/**
 * @file ScavengerGameModeComponent.h
 * @brief 협동 수집 공포 키트(`GF_CoopScavenger`)의 디렉터 — 할당량 한 주기(또는 여러 주기)의 원정을 진행합니다. 규칙은 키트의 원정(`ScavengerExpedition`)이고, 디렉터는 승무원의 걸음 ·
 *        위협의 움직임 · 우주선 레버를 씬 데이터의 시간으로 돌립니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트)을 들고 씬과 잇는다. 입력 읽기 · 모습 · HUD 는 클라이언트 모듈(`GF_Client_CoopScavenger`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다.
 *          걸음은 이어진 곳(방 · 바깥 · 우주선) 한 칸이고, 든 무게가 걸음 시간을 늘린다. 승무원 0 은 늘 사람이고, 나머지는 그 승무원의 입력이 처음 온 때부터 사람이 잡는다(그 전에는 봇).
 *          한 판의 지갑 · 깃발 · 우주선 창고는 디렉터가 가지며 바탕 판에는 번 돈만 결과로 나간다. 자리는 모두 절대 자리다.
 *          시나리오: `game/playground/automation/coopscavenger_slice.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/Countdown.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Object/GameObject/WorldOrigin.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Horror/CoopScavenger/ScavengerCatalog.h"
#include "GameFramework/Kits/Genre/Horror/CoopScavenger/ScavengerExpedition.h"
#include "GameFramework/Runtime/Character/AI/SpawnDirector.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

namespace sw
{
    /** @brief 원정의 결과입니다(감시 값 `Scavenger.Result` 의 수). */
    ENUM()
    enum class ScavengerResult : uint8
    {
        Ongoing = 0,
        Win, ///< 정한 주기 수의 할당량을 채웠다
        Lose ///< 할당량을 못 채웠다
    };
} // namespace sw

namespace sw
{
    /** @brief 승무원 하나가 한 걸음에 내는 명령입니다. 사람의 입력과 봇이 같은 모양을 쓴다. */
    struct ScavengerCommand
    {
        hashed_string          _moveTo;              ///< 걸어갈 이웃(비면 서 있는다)
        uint8                  _bUseTriggered   : 1; ///< 줍기 · 싣기 · 팔기 · 잠긴 문 열기
        uint8                  _bLeverTriggered : 1; ///< 착륙 · 이륙
        [[maybe_unused]] uint8 _reserved        : 6;

        ScavengerCommand()
            : _moveTo{}
            , _bUseTriggered{ SW_FALSE }
            , _bLeverTriggered{ SW_FALSE }
            , _reserved{ 0 }
        {
        }

        bool isIdle() const { return _moveTo.empty() && _bUseTriggered == SW_FALSE && _bLeverTriggered == SW_FALSE; }
    };
} // namespace sw

namespace sw
{
    /** @brief 위성 위의 위협 하나입니다. 원정의 스폰 감독이 낸 번호로 묶입니다. */
    REFLECT()
    struct ScavengerThreat
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _areaID{};
        PROPERTY()
        uint32 _spawnID{ 0 };
        PROPERTY()
        float32 _stepRemaining{ 0.0f };
        PROPERTY()
        float32 _lifeRemaining{ 0.0f };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bIndoor{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScavengerGameModeComponent
     * @brief 궤도 → 착륙 → 고철 나르기 → 이륙 → 회사 매입 → 할당량 판정을 승무원 명령과 봇으로 진행합니다.
     */
    REFLECT( Category = "CoopScavenger", DisplayName = "Scavenger Game Mode",
             Tooltip = "Runs a coop scavenger expedition: crew from input commands or bots hauls scrap, threats roam, the company buys and the quota decides" )
    class SW_KIT_API ScavengerGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        ScavengerGameModeComponent();
        virtual ~ScavengerGameModeComponent() override;

        void               serializeGameState( StateIO& io ) override;
        void               collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;
        [[nodiscard]] bool readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const override;

        // ---- 명령(게임 스레드) ----
        /**
         * @brief 승무원 @p player 의 이번 프레임 명령을 넘깁니다. 다음 걸음이 씁니다(걸어갈 곳은 마지막 값, 누름은 걸음이 쓸 때까지 모은다).
         * @details 빈 명령이 아니면 그 승무원을 사람이 잡은 것으로 본다.
         * @return 판이 진행 중이고 그 승무원이 있으면 true 입니다.
         */
        [[nodiscard]] bool submitCrewCommand( int32 player, const ScavengerCommand& command );

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        const ScavengerExpedition&     getExpedition() const { return _expedition; }
        const vector<ScavengerThreat>& getThreats() const { return _listThreat; }
        /** @brief 이 판의 카탈로그입니다. 열기 전이면 nullptr 입니다. */
        const ScavengerCatalog* findCatalog() const { return _pCatalog.get(); }
        /** @brief 판이 열렸고 결과가 나지 않았으면 true 입니다. */
        bool isAcceptingInput() const { return isStarted() && _result == ScavengerResult::Ongoing; }
        bool isHuman( int32 player ) const;
        /** @brief 승무원 @p player 가 있는 곳에서 @p direction(x 오른쪽 · y 앞) 쪽의 이웃입니다. 그쪽에 이웃이 없으면 빈 이름입니다(잠긴 문의 이웃도 답한다). */
        hashed_string findNeighborToward( int32 player, const float2& direction ) const;
        /** @brief 곳 @p areaID 의 절대 자리입니다(지도 칸 × 간격). 지도에 없는 곳(착륙 전의 우주선)은 원점입니다. */
        AbsolutePosition computeAreaPosition( const hashed_string& areaID ) const;
        ScavengerResult  getExpeditionResult() const { return _result; }
        int32            getResult() const { return static_cast<int32>( _result ); }
        int32            getPhase() const { return static_cast<int32>( _expedition.getPhase() ); }
        int32            getQuota() const { return _expedition.getQuota().getQuota(); }
        int32            getFulfilled() const { return _expedition.getQuota().getFulfilled(); }
        int32            getDaysLeft() const { return _expedition.getQuota().getDaysLeft(); }
        int32            getShipValue() const { return _expedition.computeShipValue(); }
        int32            getCredits() const { return static_cast<int32>( _expedition.getCredits() ); }
        int32            getGroundScrapCount() const { return static_cast<int32>( _expedition.getFacility().getGroundScrap().size() ); }
        int32            getThreatCount() const { return static_cast<int32>( _listThreat.size() ); }
        /** @brief 승무원 0 이 우주선 안이면 true 입니다. */
        bool isPlayerInShip() const;
        /** @brief 모든 승무원이 든 것의 수입니다. */
        int32 countCarried() const;
        /** @brief 지금 위성에 내려 흐른 하루의 비율(0 도착, 1 자동 이륙)입니다. */
        float32 computeDayRatio() const;

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               onStateRestored( bool bRestored ) override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 지갑 · 깃발 · 창고를 비우고 원정을 새로 엽니다(판을 열 때 · 상태를 읽기 전). 원정이 거절하면 이유를 남기고 false 입니다. */
        [[nodiscard]] bool openExpedition();
        /** @brief 승무원 하나의 명령을 적용합니다. */
        void applyCommand( int32 player, const ScavengerCommand& command, float32 deltaTime );
        /** @brief 쓰기 — 회사의 우주선 안이면 팔고, 우주선 안이면 싣고, 바닥 고철이 있으면 줍고, 아니면 곁의 잠긴 문을 엽니다. */
        void use( int32 player );
        /** @brief 레버 — 궤도면 (마감 날이면 회사로 잡고) 내리고, 우주선 안이면 이륙합니다. */
        void pullLever( int32 player );
        /** @brief 사람이 없는 승무원의 명령입니다. */
        ScavengerCommand decideBot( int32 player ) const;
        /** @brief @p player 에서 @p targetID 로 가는 길의 다음 칸입니다. 길이 없거나 이미 그곳이면 빈 이름입니다. */
        hashed_string findStepToward( int32 player, const hashed_string& targetID ) const;
        void          updateThreats( float32 deltaTime );
        /** @brief 이벤트를 위협 · 로그 · 결과로 옮깁니다. */
        void settle();

    private:
        PROPERTY( Category = "Expedition", DisplayName = "Company Moon", Tooltip = "Moon id (catalog) the ship goes to on the deadline day" )
        CatalogKey<ScavengerMoonDef> _companyMoon;
        PROPERTY( Required, Category = "Expedition", DisplayName = "Threat Table", Tooltip = "Budget and entries of the threats; tag entries Indoor or Outdoor" )
        AssetRef<SpawnTable> _threatTable;
        PROPERTY( Category = "Expedition", DisplayName = "Seed", Min = 1 )
        uint32 _seed;
        PROPERTY( Category = "Expedition", DisplayName = "Crew", Min = 1, Max = 8 )
        int32 _crewCount;
        PROPERTY( Category = "Expedition", DisplayName = "Cycles To Win", Min = 1 )
        int32 _cyclesToWin;
        PROPERTY( Category = "Expedition", DisplayName = "Ship Storage Slots", Min = 1 )
        int32 _storageSlotCount;
        PROPERTY( Category = "Crew", DisplayName = "Step Time", Tooltip = "Time one area takes to cross with empty hands", Min = 0.01, Units = s )
        float32 _stepSeconds;
        PROPERTY( Category = "Crew", DisplayName = "Area Spacing", Tooltip = "Distance between neighbouring areas of the layout", Min = 0.1, Units = m )
        float32 _areaSpacing;
        PROPERTY( Category = "Threat", DisplayName = "Threat Step Time", Min = 0.01, Units = s )
        float32 _threatStepSeconds;
        PROPERTY( Category = "Threat", DisplayName = "Threat Life Time", Min = 0.01, Units = s )
        float32 _threatLifeSeconds;
        PROPERTY( Category = "Threat", DisplayName = "Threat Damage", Tooltip = "Health a crew member loses when a threat steps into the same area", Min = 0.0 )
        float32 _threatDamage;
        PROPERTY( Category = "Result", DisplayName = "Result Hold", Tooltip = "How long the result stays on screen before the minigame ends", Min = 0.0, Units = s )
        float32 _resultHoldSeconds;

        SharedAssetRef<ScavengerCatalog> _pCatalog;
        SharedAssetRef<SpawnTable>       _pThreatTable;
        Wallet                           _wallet;  ///< 한 판의 회사 돈(원정이 빌린다)
        GameFlags                        _flags;   ///< 한 판의 문 잠금(원정이 빌린다)
        Inventory                        _storage; ///< 우주선 창고(원정이 빌린다)
        ScavengerExpedition              _expedition;
        vector<ScavengerCommand>         _listCommand; ///< 승무원마다 받아 둔 입력 — 클라이언트가 프레임마다 덮으므로 싣지 않는다
        vector<Countdown>                _listStep;    ///< 승무원마다 다음 걸음까지
        vector<ScavengerEvent>           _listEvent;   ///< 걸음마다 비워 다시 쓰는 알림 그릇
        Countdown                        _resultHold;
        PROPERTY( RuntimeState )
        vector<ScavengerThreat> _listThreat;
        PROPERTY( RuntimeState )
        uint32 _humanMask; ///< 사람이 잡은 승무원의 비트(승무원 0 은 늘 사람이라 넣지 않는다)
        PROPERTY( RuntimeState )
        ScavengerResult _result;
    };
} // namespace sw
