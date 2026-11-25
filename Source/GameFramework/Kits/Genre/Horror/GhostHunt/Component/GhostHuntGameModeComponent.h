/**
 * @file GhostHuntGameModeComponent.h
 * @brief 유령 사냥 키트(`GF_GhostHunt`)의 디렉터 — 저택 한 채를 진행합니다. 규칙은 키트의 저택(`GhostMansion`)과 싸움(`GhostEncounter`)이고, 방은 방 그래프(`AreaGraph`)의 사각형입니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트)을 들고 씬과 잇는다. 입력 읽기 · 모습 · HUD 는 클라이언트 모듈(`GF_Client_GhostHunt`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다.
 *          플레이어는 방 사각형 안을 걷고 이어진 방으로 넘어간다. 잠긴 길을 밀면 그 연결의 종류(`Link kind`)를 열쇠 문 id 로 열어 본다. 유령은 나타나면 플레이어 쪽으로 다가오고,
 *          흡입되는 동안 도망 방향으로 플레이어를 끈다. 유령이 정해진 방의 불을 모두 켜면 이기고, 체력이 다하면 진다.
 *          한 판의 지갑 · 깃발 · 가방은 디렉터가 가지며 바탕 판에는 모은 동전만 결과로 나간다. 자리는 모두 절대 자리다.
 *          시나리오: `game/playground/automation/ghosthunt_slice.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Horror/GhostHunt/GhostCatalog.h"
#include "GameFramework/Kits/Genre/Horror/GhostHunt/GhostMansion.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/MatchGameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/LootTable.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"
#include "GameFramework/Runtime/World/Land/AreaGraph.h"

namespace sw
{
    /** @brief 사냥의 결과입니다(감시 값 `GhostHunt.Result` 의 수). */
    ENUM()
    enum class GhostHuntResult : uint8
    {
        Ongoing = 0,
        Win, ///< 모든 방의 불을 켰다
        Lose ///< 체력이 다했다
    };
} // namespace sw

namespace sw
{
    /** @brief 플레이어가 한 걸음에 내는 명령입니다. */
    struct GhostHuntCommand
    {
        float2                 _move;
        uint8                  _bLight        : 1; ///< 손전등을 비추고 있다
        uint8                  _bStrobe       : 1; ///< 스트로브를 모으고 있다(떼면 터진다)
        uint8                  _bVacuum       : 1; ///< 청소기를 켜고 있다
        uint8                  _bUseTriggered : 1; ///< 서지 · 가구 뒤지기
        [[maybe_unused]] uint8 _reserved      : 4;

        GhostHuntCommand()
            : _move{ 0.0f, 0.0f }
            , _bLight{ SW_FALSE }
            , _bStrobe{ SW_FALSE }
            , _bVacuum{ SW_FALSE }
            , _bUseTriggered{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @class GhostHuntGameModeComponent
     * @brief 방마다 유령을 기절시켜 빨아들이고 불을 켜며 열쇠로 다음 방을 여는 저택 한 채를 입력 명령으로 진행합니다.
     */
    REFLECT( Category = "GhostHunt", DisplayName = "Ghost Hunt Game Mode",
             Tooltip = "Runs one ghost mansion: rooms of the area graph, ghosts stunned by light and caught by the vacuum, keys and doors, until every room is lit" )
    class SW_KIT_API GhostHuntGameModeComponent : public MatchGameModeComponent
    {
    public:
        REFLECT_BODY();

        GhostHuntGameModeComponent();
        virtual ~GhostHuntGameModeComponent() override;

        void               serializeGameState( StateIO& io ) override;
        void               collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;
        [[nodiscard]] bool readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const override;

        // ---- 명령(게임 스레드) ----
        /**
         * @brief 이번 프레임의 플레이어 입력을 넘깁니다. 다음 걸음이 씁니다(이동 · 누르고 있음은 마지막 값, 누름은 걸음이 쓸 때까지 모은다).
         * @return 판이 진행 중이면 true 입니다.
         */
        [[nodiscard]] bool submitPlayerInput( const GhostHuntCommand& command );

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        const GhostMansion& getMansion() const { return _mansion; }
        const AreaGraph&    getGraph() const { return _graph; }
        /** @brief 이 판의 카탈로그입니다. 열기 전이면 nullptr 입니다. */
        const GhostCatalog* findCatalog() const { return _pCatalog.get(); }
        /** @brief 판이 열렸고 결과가 나지 않았으면 true 입니다. */
        bool isAcceptingInput() const override { return isStarted() && _result == GhostHuntResult::Ongoing; }
        /** @brief 플레이어의 절대 자리입니다. */
        AbsolutePosition getPlayerPosition() const { return toAbsolute( _playerPosition ); }
        /** @brief 바라보는 쪽(XZ 단위 벡터)입니다. */
        float3 getFacing() const { return float3{ _facing._x, 0.0f, _facing._y }; }
        bool   isLightOn() const { return _command._bLight == SW_TRUE; }
        /** @brief 지금 지날 수 있는 연결이면 true 입니다(잠긴 문은 false). */
        bool isLinkOpen( const AreaLink& link ) const { return _graph.canTraverse( link._from, link._to, _flags ); }
        /** @brief 지도 자리(미터)를 절대 자리로 옮깁니다. */
        static AbsolutePosition toAbsolute( const float2& mapPosition ) { return AbsolutePosition{
            float3{ mapPosition._x, 0.0f, mapPosition._y }
 }; }
        /** @brief 가구 @p furniture 의 지도 자리입니다 — 방의 안쪽 벽(+Z)을 따라 카탈로그 순서로 놓인다. 방이 없으면 원점입니다. */
        float2          computeFurniturePosition( const GhostFurnitureDef& furniture ) const;
        GhostHuntResult getHuntResult() const { return _result; }
        int32           getResult() const { return static_cast<int32>( _result ); }
        int32           getRemainingGhosts() const { return _mansion.getEncounter().countRemaining(); }
        int32           getCaughtBoos() const { return _mansion.countCaughtBoos(); }
        /** @brief 유령이 정해진 방(카탈로그의 방) 가운데 불이 켜진 수입니다. */
        int32   countLitRooms() const;
        int32   countRooms() const;
        int32   getCoins() const;
        int32   getKeyCount() const { return _bag.getSlotCount() - _bag.countEmptySlots(); }
        float32 getHealth() const { return _health; }
        float32 getHealthRatio() const { return _maxHealth > 0.0f ? _health / _maxHealth : 0.0f; }
        float32 getPlayerX() const { return _playerPosition._x; }
        float32 getPlayerZ() const { return _playerPosition._y; }
        bool    isSucking() const { return _mansion.getEncounter().getSuctionTarget() != 0; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               onStateRestored( bool bRestored ) override;
        void               updateMatch( float32 deltaTime ) override;

    private:
        /** @brief 지갑 · 깃발 · 가방 · 방 그래프를 비우고 저택을 새로 엽니다(판을 열 때 · 상태를 읽기 전). 저택이 거절하면 이유를 남기고 false 입니다. */
        [[nodiscard]] bool openMansion();
        /** @brief 방 @p roomID 에 들어가 그 방의 유령을 자리에 놓습니다. */
        void enterRoom( const hashed_string& roomID );
        /** @brief 걷고, 이어진 방으로 넘어가고, 잠긴 길이면 열쇠 문을 열어 봅니다. */
        void movePlayer( const float2& delta );
        /** @brief 손전등 · 스트로브 · 청소기 · 쓰기를 적용합니다. */
        void applyTools( const GhostHuntCommand& command, float32 deltaTime );
        /** @brief 청소기 — 기절한 유령을 빨아들이거나(줄다리기) 들킨 부를 깎습니다. */
        void applyVacuum( const GhostHuntCommand& command, float32 deltaTime );
        /** @brief 쓰기 — 서지 게이지가 찼으면 터뜨리고, 아니면 곁의 가구를 뒤집니다. */
        void use();
        /** @brief 나타난 유령을 플레이어 쪽으로 다가오게 합니다. */
        void moveGhosts( float32 deltaTime );
        /** @brief 이벤트를 피해 · 로그 · 결과로 옮깁니다. */
        void settle();

    private:
        PROPERTY( Required, Category = "Mansion", DisplayName = "Area Graph", Tooltip = "Rooms (rectangles in metres) and links; the kind of a locked link is the door id of the catalog" )
        AssetRef<AreaGraph> _areaGraph;
        PROPERTY( Required, Category = "Mansion", DisplayName = "Loot", Tooltip = "Loot tables of the furniture" )
        AssetRef<LootCatalog> _lootCatalog;
        PROPERTY( Category = "Mansion", DisplayName = "Start Room" )
        hashed_string _startRoom;
        PROPERTY( Category = "Mansion", DisplayName = "Seed", Min = 1 )
        uint32 _seed;
        PROPERTY( Category = "Mansion", DisplayName = "Bag Slots", Min = 1 )
        int32 _bagSlotCount;
        PROPERTY( Category = "Player", DisplayName = "Move Speed", Min = 0.0, Units = "m/s" )
        float32 _moveSpeed;
        PROPERTY( Category = "Player", DisplayName = "Max Health", Min = 1.0 )
        float32 _maxHealth;
        PROPERTY( Category = "Player", DisplayName = "Use Radius", Tooltip = "How near furniture the use command and the vacuum reach", Min = 0.1, Units = m )
        float32 _useRadius;
        PROPERTY( Category = "Ghost", DisplayName = "Ghost Speed", Min = 0.0, Units = "m/s" )
        float32 _ghostSpeed;
        PROPERTY( Category = "Ghost", DisplayName = "Ghost Reach", Tooltip = "An attack lands on a player nearer than this", Min = 0.0, Units = m )
        float32 _ghostReach;
        PROPERTY( Category = "Ghost", DisplayName = "Ghost Spacing", Tooltip = "Distance between the ghosts of a room where they first hide", Min = 0.0, Units = m )
        float32 _ghostSpacing;

        BoundAssetRef<GhostCatalog>   _pCatalog;
        BoundAssetRef<AreaGraph>      _pGraphAsset;
        BoundAssetRef<LootCatalog>    _pLoot;
        BoundAssetRef<ItemCatalog>    _pItemCatalog; ///< 가방 · 상자의 겹침 수(게임 범위의 아이템 카탈로그)
        SW_CODEC_STATE AreaGraph      _graph;        ///< 방문 상태를 가진 이 판의 사본(저택이 빌린다)
        SW_CODEC_STATE GameFlags      _flags;        ///< 한 판의 불 · 문(저택이 빌린다)
        SW_CODEC_STATE Inventory      _bag;          ///< 한 판의 열쇠 가방(저택이 빌린다)
        SW_CODEC_STATE Wallet         _wallet;       ///< 한 판의 동전(저택이 빌린다)
        SW_CODEC_STATE GhostMansion   _mansion;
        SW_TRANSIENT GhostHuntCommand _command;          ///< 마지막 입력 — 클라이언트가 프레임마다 덮으므로 싣지 않는다
        SW_TRANSIENT vector<GhostEvent> _listGhostEvent; ///< 걸음마다 비워 다시 쓰는 알림 그릇
        SW_TRANSIENT vector<GhostMansionEvent> _listMansionEvent;
        PROPERTY( RuntimeState )
        float2 _playerPosition;
        PROPERTY( RuntimeState )
        float2 _facing;
        PROPERTY( RuntimeState, Min = 0.0 )
        float32 _health;
        PROPERTY( RuntimeState, Min = 0, Max = 1 )
        uint8 _bStrobeHeld; ///< 앞 걸음에 스트로브를 모으고 있었다(뗀 걸음에 터진다)
        PROPERTY( RuntimeState )
        GhostHuntResult _result;
    };
} // namespace sw
