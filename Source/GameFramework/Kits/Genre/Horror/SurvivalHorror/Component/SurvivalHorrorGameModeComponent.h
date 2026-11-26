/**
 * @file SurvivalHorrorGameModeComponent.h
 * @brief 생존 공포 키트(`GF_SurvivalHorror`)의 디렉터 — 집 한 채를 빠져나가는 판을 진행합니다. 규칙은 키트의 세션(`SurvivalHorrorSession`)과 초자연 전투(`SurvivalHorrorEncounter`)이고,
 *        방은 방 그래프(`AreaGraph`)의 사각형, 쓸 수 있는 자리는 씬 데이터(`SurvivalHorrorStage`)입니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트)을 들고 씬과 잇는다. 입력 읽기 · 모습 · HUD 는 클라이언트 모듈(`GF_Client_SurvivalHorror`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다.
 *          잠긴 길을 밀면 그 연결의 종류(`Link kind`)를 열쇠 자물쇠 id 로 열어 본다. 괴물 곁에서 쓰기를 누르면 전투가 열리고 차례가 데이터의 간격으로 흐르며, 그동안 걷지 못한다.
 *          목표 방에 들면 이기고 체력이 다하면 진다. 한 판의 깃발 · 가방 · 상자는 디렉터가 가지며 바탕 판에는 상금만 결과로 나간다. 자리는 모두 절대 자리다.
 *          시나리오: `game/playground/automation/survivalhorror_slice.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/Time/Countdown.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/SurvivalHorrorCatalog.h"
#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/SurvivalHorrorEncounter.h"
#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/SurvivalHorrorSession.h"
#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/SurvivalHorrorStage.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/MatchGameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/GridInventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/World/Land/AreaGraph.h"

namespace sw
{
    /** @brief 판의 결과입니다(감시 값 `SurvivalHorror.Result` 의 수). */
    ENUM()
    enum class SurvivalHorrorResult : uint8
    {
        Ongoing = 0,
        Win, ///< 목표 방에 들었다
        Lose ///< 체력이 다했다
    };
} // namespace sw

namespace sw
{
    /** @brief 플레이어가 한 걸음에 내는 명령입니다. */
    struct SurvivalHorrorCommand
    {
        float2                 _move;
        uint8                  _bUseTriggered   : 1; ///< 곁의 자리를 쓴다
        uint8                  _bLightTriggered : 1; ///< 손전등을 켜고 끈다
        uint8                  _bHealTriggered  : 1; ///< 가방의 회복 아이템을 쓴다(없으면 섞을 수 있는 둘을 섞는다)
        [[maybe_unused]] uint8 _reserved        : 5;

        SurvivalHorrorCommand()
            : _move{ 0.0f, 0.0f }
            , _bUseTriggered{ SW_FALSE }
            , _bLightTriggered{ SW_FALSE }
            , _bHealTriggered{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @class SurvivalHorrorGameModeComponent
     * @brief 열쇠 · 단서 · 자물쇠 · 어둠 · 괴물을 지나 목표 방까지 가는 판을 입력 명령으로 진행합니다.
     */
    REFLECT( Category = "SurvivalHorror", DisplayName = "Survival Horror Game Mode",
             Tooltip = "Runs one survival horror house: rooms of the area graph, items, keys, clues, puzzles, darkness and turn based fights, until the goal room" )
    class SW_KIT_API SurvivalHorrorGameModeComponent : public MatchGameModeComponent
    {
    public:
        REFLECT_BODY();

        SurvivalHorrorGameModeComponent();
        virtual ~SurvivalHorrorGameModeComponent() override;

        void               serializeGameState( StateIO& io ) override;
        void               collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;
        [[nodiscard]] bool readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const override;

        // ---- 명령(게임 스레드) ----
        /**
         * @brief 이번 프레임의 플레이어 입력을 넘깁니다. 다음 걸음이 씁니다(이동은 마지막 값, 누름은 걸음이 쓸 때까지 모은다).
         * @return 판이 진행 중이면 true 입니다.
         */
        [[nodiscard]] bool submitPlayerInput( const SurvivalHorrorCommand& command );

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        const SurvivalHorrorSession& getSession() const { return _session; }
        const SurvivalHorrorStage&   getStage() const { return _stage; }
        const AreaGraph&             getGraph() const { return _graph; }
        /** @brief 이 판의 카탈로그입니다. 열기 전이면 nullptr 입니다. */
        const SurvivalHorrorCatalog* findCatalog() const { return _pCatalog.get(); }
        /** @brief 판이 열렸고 결과가 나지 않았으면 true 입니다. */
        bool isAcceptingInput() const override { return isStarted() && _result == SurvivalHorrorResult::Ongoing; }
        /** @brief 전투 중이면 true 입니다. */
        bool isFighting() const { return _encounterSpot >= 0; }
        /** @brief 이미 줍거나 쓰러뜨린 자리이면 true 입니다. */
        bool isSpotUsed( size_t spot ) const { return spot < _listUsedSpot.size() && _listUsedSpot[spot] == SW_TRUE; }
        /** @brief 플레이어의 절대 자리입니다. */
        AbsolutePosition getPlayerPosition() const { return toAbsolute( _playerPosition ); }
        /** @brief 지도 자리(미터)를 절대 자리로 옮깁니다. */
        static AbsolutePosition toAbsolute( const float2& mapPosition ) { return AbsolutePosition{
            float3{ mapPosition._x, 0.0f, mapPosition._y }
 }; }
        /** @brief 지금 지날 수 있는 연결이면 true 입니다(잠긴 문은 false). */
        bool                 isLinkOpen( const AreaLink& link ) const { return _graph.canTraverse( link._from, link._to, _flags ); }
        bool                 isInDarkness() const;
        SurvivalHorrorResult getGameResult() const { return _result; }
        int32                getResult() const { return static_cast<int32>( _result ); }
        float32              getHealth() const { return _session.getHealth(); }
        float32              getHealthRatio() const;
        float32              getSanity() const { return _session.getSanity().getValue(); }
        float32              getSanityRatio() const { return _session.getSanity().getRatio(); }
        float32              getBatteryRatio() const { return _session.getBattery().getRatio(); }
        bool                 isFlashlightOn() const { return _session.isFlashlightOn(); }
        int32                getItemCount() const { return static_cast<int32>( _bag.getItems().size() ); }
        /** @brief 아직 쓰러뜨리지 않은 괴물 수입니다. */
        int32   countMonsters() const;
        float32 getPlayerX() const { return _playerPosition._x; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               onStateRestored( bool bRestored ) override;
        void               updateMatch( float32 deltaTime ) override;

    private:
        /** @brief 깃발 · 가방 · 상자 · 방 그래프를 비우고 세션을 새로 엽니다(판을 열 때 · 상태를 읽기 전). 세션이 거절하면 이유를 남기고 false 입니다. */
        [[nodiscard]] bool openSession();
        /** @brief 걷고, 이어진 방으로 넘어가고, 잠긴 길이면 열쇠로 열어 봅니다. */
        void movePlayer( const float2& delta );
        /** @brief 지금 방의 괴물들을 목격합니다(정신력이 깎인다). */
        void witnessMonsters();
        /** @brief 곁의 가장 가까운 자리를 씁니다. */
        void use();
        /** @brief 자리 하나를 씁니다. */
        void useSpot( size_t spotIndex );
        /** @brief 상자가 비었으면 가방의 첫 물건(열쇠 빼고)을 넣고, 들었으면 첫 물건을 꺼냅니다. */
        void useItemBox();
        /** @brief 추리 — 가진 단서의 필요한 연결을 보드에 잇고 답을 냅니다. */
        void submitDeduction( const SurvivalHorrorSpot& spot );
        /** @brief 회복 아이템을 쓰거나, 없으면 섞을 수 있는 둘을 섞습니다. */
        void heal();
        /** @brief 괴물 자리 @p spotIndex 와 싸움을 엽니다. 탄약이 있으면 한 발을 써 힘 주사위를 더합니다. */
        void startEncounter( size_t spotIndex );
        /** @brief 전투를 데이터의 간격으로 한 차례씩 진행합니다. */
        void updateEncounter( float32 deltaTime );
        /** @brief 이 전투의 조사자입니다(씬 데이터 + 탄약 보너스). */
        vector<SurvivalHorrorInvestigator> makeInvestigators() const;
        /** @brief 이벤트를 로그로 내고 결과를 정합니다. */
        void settle();

    private:
        PROPERTY( Required, Category = "House", DisplayName = "Area Graph", Tooltip = "Rooms (rectangles in metres) and links; the kind of a locked link is the key lock id of the catalog" )
        AssetRef<AreaGraph> _areaGraph;
        PROPERTY( Category = "House", DisplayName = "Stage" )
        SurvivalHorrorStage _stage;
        PROPERTY( Category = "House", DisplayName = "Item Box Slots", Min = 1 )
        int32 _boxSlotCount;
        PROPERTY( Category = "House", DisplayName = "Seed", Min = 1 )
        uint32 _seed;
        PROPERTY( Category = "Player", DisplayName = "Move Speed", Min = 0.0, Units = "m/s" )
        float32 _moveSpeed;
        PROPERTY( Category = "Player", DisplayName = "Use Radius", Min = 0.1, Units = m )
        float32 _useRadius;
        PROPERTY( Category = "Fight", DisplayName = "Investigator", Tooltip = "Dice and points the player fights with" )
        SurvivalHorrorInvestigator _investigator;
        PROPERTY( Category = "Fight", DisplayName = "Ammo Item", Tooltip = "Item one shot of which a fight spends (empty: no gun)" )
        CatalogKey<SurvivalHorrorItemDef> _ammoItem;
        PROPERTY( Category = "Fight", DisplayName = "Ammo Strength", Tooltip = "Combat dice one shot adds", Min = 0 )
        int32 _ammoStrength;
        PROPERTY( Category = "Fight", DisplayName = "Damage Per Point", Tooltip = "Health or sanity one lost point of the fight costs", Min = 0.0 )
        float32 _damagePerPoint;
        PROPERTY( Category = "Fight", DisplayName = "Turn Interval", Min = 0.01, Units = s )
        float32 _turnSeconds;
        PROPERTY( Category = "Result", DisplayName = "Win Reward", Tooltip = "Money the base game gets for getting out", Min = 0 )
        int32 _winReward;

        BoundAssetRef<SurvivalHorrorCatalog>   _pCatalog;
        BoundAssetRef<AreaGraph>               _pGraphAsset;
        BoundAssetRef<ItemCatalog>             _pItemCatalog; ///< 가방 · 상자의 겹침 수(게임 범위의 아이템 카탈로그)
        SW_CODEC_STATE AreaGraph               _graph;        ///< 방문 상태를 가진 이 판의 사본(세션이 빌린다)
        SW_CODEC_STATE GameFlags               _flags;        ///< 한 판의 문 · 퍼즐(세션이 빌린다)
        SW_CODEC_STATE GridInventory           _bag;          ///< 한 판의 격자 가방(세션이 빌린다)
        SW_CODEC_STATE Inventory               _itemBox;      ///< 한 판의 아이템 상자(세션이 빌린다)
        SW_CODEC_STATE SurvivalHorrorSession   _session;
        SW_CODEC_STATE SurvivalHorrorEncounter _encounter;   ///< `_encounterSpot` 이 있을 때만 뜻이 있다
        SW_TRANSIENT SurvivalHorrorCommand     _command;     ///< 마지막 입력 — 클라이언트가 프레임마다 덮으므로 싣지 않는다
        SW_TRANSIENT vector<SurvivalHorrorEvent> _listEvent; ///< 걸음마다 비워 다시 쓰는 알림 그릇
        SW_CODEC_STATE vector<uint8> _listUsedSpot;          ///< 자리마다 줍거나 쓰러뜨렸는지(씬 데이터 순서)
        SW_CODEC_STATE Countdown     _turn;
        PROPERTY( RuntimeState )
        float2 _playerPosition;
        PROPERTY( RuntimeState, Min = -1 )
        int32 _encounterSpot; ///< 싸우는 괴물 자리(-1 이면 싸우지 않는다)
        PROPERTY( RuntimeState, Min = 0, Max = 1 )
        uint8 _bArmed; ///< 이 전투에 탄약을 썼다
        PROPERTY( RuntimeState )
        SurvivalHorrorResult _result;
    };
} // namespace sw
