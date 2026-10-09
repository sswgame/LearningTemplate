/**
 * @file GhostMansion.h
 * @brief 저택 — 방 그래프(기반 `AreaGraph`) 위에서 방에 들어가면 그 방 유령이 나오고, 모두 잡으면 불이 켜져(플래그) 열쇠를 주며, 열쇠로 문을 엽니다.
 *        가구를 빨아들이거나 흔들면 숨은 동전 · 보물(기반 `LootTable`)이 나오고, 가구에 숨은 부(Boo)는 들키면 시간 안에 못 잡을 때 옆 방으로 달아나 숨습니다.
 * @details 가구의 전리품은 씨앗과 가구 id 로 굴려 뒤지는 순서와 상관없이 같은 결과입니다. 부가 달아날 방 · 가구도 씨앗 난수라 되풀이됩니다.
 *          그래프 · 플래그 · 전리품 표 · 카탈로그는 빌려 씁니다(저택보다 오래 살아야 한다).
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Utility/Countdown.h"
#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Foundation/Utility/GameRandom.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Horror/GhostHunt/GhostCatalog.h"
#include "GameFramework/Kits/Horror/GhostHunt/GhostEncounter.h"

namespace sw
{
    struct GameStateRefs;

    class Archive;
    class AreaGraph;
    class GameFlags;
    class Inventory;
    class LootCatalog;
    class Wallet;

    /** @brief 열쇠 문 결과입니다. */
    enum class GhostDoorResult : uint8
    {
        Opened = 0,
        AlreadyOpen,
        NeedKey,
        UnknownDoor
    };

    /** @brief 가구 뒤지기 결과입니다. */
    enum class GhostSearchResult : uint8
    {
        Found = 0, ///< 전리품을 굴렸다(빈 결과일 수도 — "없음" 가중치)
        AlreadySearched,
        WrongMode, ///< 그 방법으로는 뒤질 수 없는 가구
        BooFound,  ///< 부가 튀어나왔다(전리품은 다음에)
        UnknownFurniture
    };

    SW_GF_API const utf8* toString( GhostSearchResult result );

    /** @brief 부의 상태입니다. */
    enum class GhostBooState : uint8
    {
        Hiding = 0, ///< 가구 속(가구가 없는 방이면 방 어딘가 — 그 방에 들어가면 나온다)
        Revealed,   ///< 들켰다 — `_escapeTime` 안에 잡아야 한다
        Caught
    };

    /** @brief 부 하나의 지금 상태입니다. */
    struct GhostBooRuntime
    {
        hashed_string _room{};
        hashed_string _furniture{};
        float32       _hp{ 0.0f };
        Countdown     _timer{};
        GhostBooState _state{ GhostBooState::Hiding };
    };
} // namespace sw

namespace sw
{
    /** @brief 저택 알림 종류입니다. */
    enum class GhostMansionEventType : uint8
    {
        RoomLit = 0, ///< `_id` = 방
        KeyAwarded,  ///< `_id` = 열쇠
        DoorOpened,  ///< `_id` = 문
        BooRevealed, ///< `_id` = 부
        BooEscaped,  ///< `_id` = 부, `_room` = 달아난 방
        BooCaught,
        CoinsCollected ///< `_count` = 잡은 유령이 떨어뜨린 동전
    };

    /** @brief 저택 알림 하나입니다. */
    struct GhostMansionEvent
    {
        hashed_string         _id{};
        hashed_string         _room{};
        int32                 _count{ 0 };
        GhostMansionEventType _type{ GhostMansionEventType::RoomLit };
    };
} // namespace sw

namespace sw
{
    /**
     * @class GhostMansion
     * @brief 저택 한 채의 진행입니다. 지금 방의 싸움은 `getEncounter` 로 손전등 · 청소기를 씁니다.
     */
    class SW_GF_API GhostMansion
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "GHMN" );
        static constexpr uint32 kStateVersion = 1;

        GhostMansion();

        /** @brief 새 판을 엽니다. 플래그 · 플레이어 가방(열쇠) · 지갑(동전)은 @p refs 에서 빌립니다 — 가방이 없으면 열쇠가 드는 문은 열리지 않습니다. */
        void initialize( const GhostCatalog* pCatalog, const LootCatalog* pLoot, AreaGraph* pAreaGraph, const GameStateRefs& refs, uint32 seed );

        /** @brief 방에 들어갑니다. 불이 꺼진 방이면 그 방 유령이 (숨은 채로) 나옵니다. 나온 유령 수이고 없는 방이면 −1 입니다. */
        int32 enterRoom( const hashed_string& roomId );
        /** @brief 시간을 흘립니다 — 싸움 · 잡은 유령 세기 · 불 켜기, 들킨 부의 탈출 시간. */
        void update( float32 deltaTime );
        /** @brief 열쇠 문을 엽니다(열쇠 하나를 쓴다). */
        GhostDoorResult unlockDoor( const hashed_string& doorId );
        /** @brief 가구를 뒤집니다. 한 가구는 한 번만 전리품을 줍니다. 나온 것은 @p outLoot 에 더합니다. */
        GhostSearchResult searchFurniture( const hashed_string& furnitureId, GhostSearchMode mode, ItemStackList& outLoot );
        /** @brief 들킨 부에게 피해를 줍니다(청소기). 이번에 잡혔으면 true 입니다. */
        bool damageBoo( const hashed_string& booId, float32 amount );
        /** @brief 쌓인 알림을 @p outListEvent 뒤에 붙이고 비웁니다. */
        void drainEvents( vector<GhostMansionEvent>& outListEvent );
        /** @brief 지금 방 싸움의 알림(나타남 · 공격 · 잡힘 …)을 꺼내 갑니다 — 저택이 `update` 에서 싸움 알림을 받아 잡은 수를 세므로 게임은 여기서 받습니다. */
        void drainGhostEvents( vector<GhostEvent>& outListEvent );

        /**
         * @brief 싸움(`GhostEncounter`) · 난수 · 부(방 · 가구 · 체력 · 시간 · 상태) · 뒤진 가구 · 지금 방 · 씨앗을 씁니다.
         *        카탈로그 · 빌린 방 그래프 · 열쇠 가방 · 지갑 · 플래그(밝힌 방 · 연 문)는 싣지 않고(주인이 싣는다), 알림은 읽을 때 비웁니다.
         */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 같은 카탈로그로 `initialize` 한 뒤에 부릅니다. 깨졌거나 부 · 가구 수가 다르면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        GhostEncounter&        getEncounter() { return _encounter; }
        const GhostEncounter&  getEncounter() const { return _encounter; }
        const hashed_string&   getCurrentRoom() const { return _currentRoom; }
        bool                   isRoomLit( const hashed_string& roomId ) const;
        const GhostBooRuntime* findBoo( const hashed_string& booId ) const;
        int32                  countCaughtBoos() const;
        bool                   isSearched( const hashed_string& furnitureId ) const;

    private:
        int32 findFurnitureIndex( const hashed_string& furnitureId ) const;
        void  lightRoom( const hashed_string& roomId );
        void  moveBooAway( size_t booIndex );
        void  revealBoo( size_t booIndex );
        void  pushEvent( GhostMansionEventType type, const hashed_string& id, const hashed_string& room = hashed_string{}, int32 count = 0 );

        GhostEncounter                 _encounter;
        GameRandom                     _random;
        vector<GhostBooRuntime>        _listBoo;      ///< 카탈로그 부 순서
        vector<uint8>                  _listSearched; ///< 카탈로그 가구 순서
        EventBuffer<GhostMansionEvent> _eventBuffer;
        vector<GhostEvent>             _listGhostEvent; ///< 게임에 넘길 싸움 알림
        hashed_string                  _currentRoom;
        const GhostCatalog*            _pCatalog;
        const LootCatalog*             _pLoot;
        AreaGraph*                     _pAreaGraph;
        GameFlags*                     _pFlags;
        Inventory*                     _pInventory; ///< 플레이어 가방(열쇠)
        Wallet*                        _pWallet;    ///< 빌린 지갑(동전 — 카탈로그 통화)
        uint32                         _seed;
    };
} // namespace sw
