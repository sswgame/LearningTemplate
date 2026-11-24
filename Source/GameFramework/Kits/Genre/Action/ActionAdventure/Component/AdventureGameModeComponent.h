/**
 * @file AdventureGameModeComponent.h
 * @brief 액션 어드벤처 키트(`GF_ActionAdventure`)의 디렉터 — 플레이어를 지역 사이로 걷게 하고(입력 명령), 지역에 들어서면 진행 규칙(`AdventureCampaign`)을 돌리고,
 *        봉화 · 야영지를 쓰게 하고, 퀘스트 보상을 주고, 자동 저장에 지역 이동 · 보스 앞 · 체크포인트를 알립니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트의 `AdventureCampaign` · `AdventureRegionCatalog`)을 들고 씬과 잇는다. 입력 읽기 · 모습은 클라이언트 모듈
 *          (`GF_Client_ActionAdventure` 의 `AdventureAvatarComponent`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          지역 전투는 이 디렉터가 돌리지 않는다 — 전투 키트를 섞는 것은 게임이다. 전투를 도는 쪽은 같은 오브젝트의 뒤에 붙어 기다리는 전투(`getPendingEncounter`)를 열고(`openEncounter` —
 *          게이트가 닫힌다) 피해(`applyPlayerDamage`) · 보스 앞(`announceBoss`) · 클리어(`completeEncounter`)를 알린다. 도는 쪽이 없으면 게이트는 닫히지 않는다(탐험만 한다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/ActionAdventure/AdventureCampaign.h"
#include "GameFramework/Kits/Genre/Action/ActionAdventure/AdventureRegionCatalog.h"
#include "GameFramework/Kits/Genre/Action/ActionAdventure/AdventureVitals.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Grid/FacingDirection.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"

namespace sw
{
    class GameStateComponent;
    class QuestCatalog;

    /**
     * @class AdventureGameModeComponent
     * @brief 지역을 걷는 액션 어드벤처 한 판입니다. 같은 오브젝트의 월드 상태(`GameStateComponent`, 맨 앞 — 깃발)와 플레이어 상태 오브젝트(`_playerState` — 조종자)의 퀘스트 일지(`QuestLogComponent`)를 열고 지갑(`WalletComponent`)을 쓴다.
     * @details 지역 표(`AdventureRegionCatalog`) · 퀘스트(`QuestCatalog`)는 게임 기능 `ActionAdventure` 가 미니게임 범위에 건 것을 같이 쥔다(확장 팩이 층으로 항목을 더한다).
     *          - 걷기: 지역 밖으로는 나가지 못한다(지역이 땅이다). 전투가 열린 동안에는 그 지역 경계 안에 묶인다(게이트가 닫힌다).
     *          - 지역 진입: 진행 규칙에 알리고 자동 저장에 지역 이동을 알린다. 깨지 않은 전투가 있으면 기다리는 전투로 둔다.
     *          - 쓰기: 곁의 봉화를 켜거나(퀘스트 `Light`), 곁의 야영지에서 쉰다(체력을 채우고 일어설 지역 · 자동 저장 체크포인트). 전투 중에는 쓰지 못한다.
     *          - 쓰러지면 전투가 닫히고 일어설 지역의 자리에서 체력을 채워 일어선다. 그 지역의 전투는 깨지 않은 채로 남는다.
     *          - 자동 저장 요청은 게임 스레드에서 낸다(틱 뒤 플러시) — 관리자는 게임 스레드 전용이다.
     *          - 불러온 판의 지역이 지금 표에 없으면(확장 팩을 끈 판) 플레이어를 일어설 지역(없으면 시작 지역)으로 옮긴다 — 판은 이어진다.
     *          카메라는 `FollowCameraComponent` 가 `readCameraFocus` 로 따라간다.
     */
    REFLECT( Category = "ActionAdventure", DisplayName = "Adventure Game Mode",
             Tooltip = "Runs an action adventure: regions, beacons, camps, quest rewards and autosave hooks; region fights are run by another game mode behind it on the same object" )
    class SW_KIT_API AdventureGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();
        SW_REGISTERED_COMPONENT( AdventureGameModeComponent );

        static constexpr uint32 kStateTag     = FourCCUtil::make( "VLDR" );
        static constexpr uint32 kStateVersion = 2;

        AdventureGameModeComponent();
        virtual ~AdventureGameModeComponent() override;

        void               serializeGameState( StateIO& io ) override;
        [[nodiscard]] bool readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const override;

        // ---- 명령(게임 스레드) ----
        /**
         * @brief 이번 프레임의 플레이어 입력을 넘깁니다. 다음 규칙 걸음(`updateGame`)이 걷기 · 쓰기로 씁니다.
         * @details 이동은 마지막 값으로 덮고, 쓰기 누름은 걸음이 쓸 때까지 모은다(한 걸음 사이에 들어온 누름을 잃지 않는다).
         * @return 판이 열렸으면 true 입니다. 아니면 받지 않고 false 입니다.
         */
        [[nodiscard]] bool submitPlayerInput( const float2& move, bool bUsePressed );

        // ---- 전투를 도는 쪽의 명령(틱 뒤 게임 스레드) ----
        /** @brief 지금 지역에 들어서며 기다리게 된 전투의 지역입니다(열기 전). 없으면 빈 이름입니다. */
        const hashed_string& getPendingEncounter() const { return _pendingEncounter; }
        /** @brief 전투가 열린 지역입니다(게이트가 닫혔다). 열린 전투가 없으면 빈 이름입니다. */
        const hashed_string& getEncounterRegion() const { return _encounterRegion; }
        /** @brief 기다리는 전투를 열고 그 지역을 돌려줍니다(게이트가 닫힌다). 기다리는 전투가 없거나 표에서 빠졌으면 nullptr 입니다. */
        [[nodiscard]] const AdventureRegionDef* openEncounter();
        /** @brief 열린 전투의 보스가 곧 나옵니다 — 자동 저장에 보스 앞을 알립니다. */
        void announceBoss();
        /** @brief 4분의 1 하트 단위 피해입니다. 이번에 쓰러졌으면 true 이고, 전투를 닫고 일어설 지역의 자리에서 일어서 있습니다. */
        [[nodiscard]] bool applyPlayerDamage( int32 quarters );
        /** @brief 열린 전투를 깼습니다. 클리어 깃발 · 퀘스트(`Clear` · `Defeat`)를 알리고 그 보상까지 준 뒤 게이트를 엽니다. 열린 전투가 없으면 아무것도 하지 않습니다. */
        void completeEncounter();
        /** @brief 깨지 않고 열린 전투를 닫습니다(전투 지역이 다시 읽은 표에서 빠진 때). 다시 들어서면 다시 기다린다. */
        void cancelEncounter() { _encounterRegion = hashed_string{}; }

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        /** @brief 쥔 지역 표입니다. 판을 열기 전에는 nullptr 입니다. */
        const AdventureRegionCatalog* findRegions() const { return _pRegions.get(); }
        const AdventureCampaign&      getCampaign() const { return _campaign; }
        const float2&                 getPlayerPosition() const { return _playerPosition; }
        FacingDirection               getFacing() const { return _facing; }
        float32                       getPlayerX() const { return _playerPosition._x; }
        float32                       getPlayerZ() const { return _playerPosition._y; }
        int32                         getHealthQuarters() const { return _campaign.getVitals().computeHealthQuarters(); }
        int32                         getHeartCount() const { return _campaign.getVitals().getHeartCount(); }
        int32                         getFallCount() const { return _fallCount; }
        const hashed_string&          getCurrentRegion() const { return _currentRegion; }
        const hashed_string&          getRespawnRegion() const { return _campaign.getRespawnRegion(); }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        bool               hasPendingSpawn() const override;

    private:
        /** @brief 받아 둔 이동 명령으로 걷습니다. 지역 밖(전투 중이면 그 지역 밖)으로는 가지 않는다. */
        void movePlayer( const float2& move, float32 deltaTime, const AdventureRegionCatalog& regions );
        /** @brief 지금 자리의 지역이 바뀌었으면 진입을 처리합니다. */
        void updateRegion( const AdventureRegionCatalog& regions, GameStateComponent& state, QuestLog& questLog );
        /** @brief 곁의 봉화를 켜거나 야영지에서 쉽니다. */
        void interactWithSite( const AdventureRegionDef& region, GameStateComponent& state, QuestLog& questLog );
        /** @brief 쌓인 퀘스트 알림을 보상으로 바꿉니다. */
        void applyQuestEvents( QuestLog& questLog, Wallet& wallet );
        /** @brief 쓰러졌습니다. 전투를 닫고 일어설 지역의 자리에서 일어섭니다. */
        void handlePlayerDowned( const AdventureRegionCatalog& regions );
        /** @brief 불러온 판이 지금 표에 없는 지역을 가리키면 일어설 자리로 옮깁니다. */
        void reconcileWithRegions( const AdventureRegionCatalog& regions );

    private:
        PROPERTY( Category = "Start", DisplayName = "Start Region", Tooltip = "Region id (regions.xml) a new game starts in, at its respawn point" )
        CatalogKey<AdventureRegionDef> _startRegion;
        PROPERTY( Category = "Start", DisplayName = "Flag Prefix", Tooltip = "Name in front of this game's progress flags in the shared world state (<prefix>.beacon.<region>); required" )
        hashed_string _flagPrefix;
        PROPERTY( Category = "Player", DisplayName = "Move Speed", Min = 0.0, Units = "m/s" )
        float32 _moveSpeed;
        PROPERTY( Category = "Player", DisplayName = "Use Radius", Tooltip = "How near a beacon or camp the use command reaches", Min = 0.1, Units = m )
        float32 _useRadius;
        PROPERTY( Category = "Player", DisplayName = "Vitals" )
        AdventureVitalsSettings _vitalsSettings;

        SharedAssetRef<AdventureRegionCatalog> _pRegions;
        shared_ptr<const QuestCatalog>         _pQuests; ///< 일지가 처음 열 때의 판을 계속 빌린다(`QuestLogComponent::initialize` 는 첫 호출만 듣는다) — 그 판을 쥔다
        AdventureCampaign                      _campaign;
        PROPERTY( RuntimeState )
        float2              _playerPosition;
        float2              _commandMove;       ///< 마지막 입력 명령의 이동 축 — 클라이언트가 프레임마다 덮는 입력이라 싣지 않는다
        FacingDirection     _facing;            ///< 리플렉션 열거가 아니다 — 손 본문이 싣는다
        hashed_string       _currentRegion;     ///< 지금 서 있는 지역(비면 다음 틱이 진입을 처리한다)
        hashed_string       _pendingEncounter;  ///< 지금 지역에 들어서며 기다리게 된 전투(열리면 비운다)
        hashed_string       _encounterRegion;   ///< 전투가 열린 지역(탐험 중이면 비었다)
        hashed_string       _pendingArea;       ///< 게임 스레드에서 자동 저장에 알릴 지역 이동
        hashed_string       _pendingBoss;       ///< 보스 앞
        hashed_string       _pendingCheckpoint; ///< 체크포인트(야영지)
        vector<QuestEvent>  _listQuestEvent;    ///< 프레임마다 비워 다시 쓰는 알림 그릇(할당을 되풀이하지 않는다)
        vector<WalletEvent> _listWalletEvent;
        vector<HealthEvent> _listHealthEvent;
        PROPERTY( RuntimeState, Min = 0 )
        int32                  _fallCount;
        uint8                  _bUseQueued : 1; ///< 다음 걸음이 쓸 쓰기 누름
        [[maybe_unused]] uint8 _reserved   : 7;
    };
} // namespace sw
