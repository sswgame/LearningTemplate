/**
 * @file SkirmishMatch.h
 * @brief RTS 교전 한 판의 규칙 — 지도 데이터(`SkirmishMap` — 절벽 · 시작 지점 · 처음 놓이는 유닛과 자원)로 판을 열고, 컴퓨터 상대(`RTSCommanderAI`), 정리 사냥, 상태 · 승패 로그를 맡습니다.
 *
 * @details 화면 · 입력을 모르는 순수 규칙이라 엔진 밖 하네스에서 그대로 돌려 볼 수 있습니다(`RTSWorld` · `RTSCommanderAI` 만 씁니다).
 *          사람이 맡은 플레이어(`kHumanPlayer0` · `kHumanPlayer1` 깃발)는 AI 가 돌지 않습니다 — 혼자 하면 0 번만, 2 인 락스텝이면 둘 다, 자동 플레이면 아무도 사람이 아닙니다.
 *          모두 AI 면 시작 지점마다 적은 성향(`SkirmishAIRole`)으로 돕니다 — 같은 AI 끼리면 대칭이라
 *          가운데서 서로 지우기만 하고 끝나지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"
#include "Engine/TileMap/TileMapXML.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCommanderAI.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

namespace sw
{
    class BehaviorTreeAsset;

    /** @brief 한 플레이어의 지금 형편입니다(로그 · 화면 글). */
    struct SkirmishPlayerSummary
    {
        int32 _workers{ 0 };
        int32 _army{ 0 };
        int32 _buildings{ 0 };
        int32 _minerals{ 0 };
        int32 _gas{ 0 };
        int32 _supplyUsed{ 0 };
        int32 _supplyCap{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SkirmishAIStyle
     * @brief 컴퓨터 상대 하나의 성향(경제 · 병력 목표)입니다. 값은 씬 데이터(디렉터의 PROPERTY)에 있고, 코드에는 기본 성향이 없다.
     * @details 적지 않은 성향은 0 이라 판을 열 때 거절한다(`isSet`) — 조용히 키트 기본값으로 돌지 않는다.
     */
    REFLECT()
    struct SW_KIT_API SkirmishAIStyle
    {
        REFLECT_BODY();
        PROPERTY( Min = 1, Tooltip = "Workers the AI trains" )
        int32 _workerTarget{ 0 };
        PROPERTY( Min = 1, Tooltip = "Production buildings the AI builds" )
        int32 _productionTarget{ 0 };
        PROPERTY( Min = 1, Tooltip = "Army size that starts an attack" )
        int32 _attackArmySize{ 0 };
        PROPERTY( Min = 0, Tooltip = "The AI builds supply when the free supply is at most this" )
        int32 _supplyMargin{ 0 };

        /** @brief 목표가 모두 적혔으면 true 입니다(보급 여유는 0 도 뜻이 있다). */
        bool isSet() const { return _workerTarget >= 1 && _productionTarget >= 1 && _attackArmySize >= 1; }
    };
} // namespace sw

namespace sw
{
    /** @brief 모두 컴퓨터인 판에서 그 자리가 쓰는 성향입니다(사람과 붙는 컴퓨터는 상대 성향 `_opponentAI` 다). */
    ENUM()
    enum class SkirmishAIRole : uint8
    {
        Rush,
        Macro,
    };

    /** @brief 플레이어 하나의 시작 지점입니다. 목록의 순서가 플레이어 번호입니다. */
    REFLECT()
    struct SW_KIT_API SkirmishPlayerStart
    {
        REFLECT_BODY();
        PROPERTY( Required, Min = 0, Tooltip = "Team of the player; different teams are enemies" )
        int32 _team{ 0 };
        PROPERTY( Required, Tooltip = "World X, Z of the start (base center): workers start on the nearest minerals and the enemy army attacks here" )
        float2 _position{ 0.0f, 0.0f };
        PROPERTY( Required, Tooltip = "AI style of the player when every player is AI" )
        SkirmishAIRole _aiRole{ SkirmishAIRole::Rush };
    };

    /** @brief 처음 놓이는 유닛 · 건물 · 자원 하나입니다. 목록의 순서대로 놓습니다(유닛 id 가 그 순서다). */
    REFLECT()
    struct SW_KIT_API SkirmishUnitPlacement
    {
        REFLECT_BODY();
        PROPERTY( Required, Tooltip = "Unit id in the unit catalog" )
        CatalogKey<RTSUnitDef> _unitID{};
        PROPERTY( Required, Min = -1, Tooltip = "Player index, -1 = nobody (resources)" )
        int32 _owner{ RTSWorld::kNoOwner };
        PROPERTY( Required, Tooltip = "Cell; the lower-left cell of buildings and resources" )
        int2 _cell{ 0, 0 };
    };

    /**
     * @struct SkirmishMap
     * @brief 교전 지도 하나입니다(씬 데이터). 크기와 절벽은 타일맵 에셋의 데이터 레이어이고, 시작 지점과 배치는 목록이다 — 지도를 바꾸거나 더하는 일에 C++ 가 없다.
     * @details 대칭은 규칙이 아니라 데이터다(양쪽 자리를 모두 적는다). 타일맵의 (x, y) 칸이 그대로 월드의 (x, z) 칸이다.
     */
    REFLECT()
    struct SW_KIT_API SkirmishMap
    {
        REFLECT_BODY();

        /** @brief 절벽 레이어의 이름입니다 — 0 이 아닌 칸이 막힌 땅이다. 타일맵에 없으면 판을 열지 않는다. */
        static constexpr const utf8* kCliffLayer = "cliff";

        PROPERTY( DisplayName = "Tile Map", Tooltip = "Tile map (.tilemap.xml): its size is the map size, its data layer 'cliff' blocks the ground (non-zero)" )
        AssetRef<TileMapXMLData> _tileMapPath;
        PROPERTY( DisplayName = "Player Starts", Tooltip = "One entry per player, in player order" )
        vector<SkirmishPlayerStart> _listPlayerStart;
        PROPERTY( DisplayName = "Placements", Tooltip = "Units, buildings and resources the match opens with, in spawn order" )
        vector<SkirmishUnitPlacement> _listPlacement;
    };
} // namespace sw

namespace sw
{
    /** @brief 한 판의 규칙 수치입니다. 디렉터의 PROPERTY 가 채웁니다. */
    struct SkirmishMatchSettings
    {
        float32 _huntIntervalSeconds{ 4.0f };    ///< 적 시작 지점에 닿아 노는 병력을 남은 적 건물로 보내는 주기
        float32 _arrivedRadius{ 10.0f };         ///< 적 시작 지점에서 이 안이면 "닿았다"(m)
        int32   _startMinerals{ 50 };            ///< 플레이어마다 시작 광물
        float32 _statusIntervalSeconds{ 30.0f }; ///< 판 상황 로그 주기
        // AI 가 쓰는 유닛 카탈로그 id — 디렉터의 PROPERTY(씬 데이터)가 채우고, 비어 있으면 AI 가 그 일을 하지 못한다.
        hashed_string _workerID{};
        hashed_string _depotID{};
        hashed_string _supplyID{};
        hashed_string _productionID{};
        hashed_string _armyUnitID{};
        // 컴퓨터 상대의 성향 — 모두 AI 면 시작 지점이 적은 성향(러시 · 운영), 사람과 붙으면 상대 성향이다.
        SkirmishAIStyle _rushAI{};
        SkirmishAIStyle _macroAI{};
        SkirmishAIStyle _opponentAI{};
        // 컴퓨터 상대의 행동 트리 — 데이터(`commander.bt.xml`)의 받은 때 판(스냅숏)을 쥔다. AI 는 시작할 때 트리를 복사해 쥐고 노드 번호로 돌므로
        // 판 중간에 다시 읽힌 판으로 바뀌면 안 된다. 날 포인터로 들면 다시 읽기 정리 뒤에 끊긴다. 컴퓨터 상대가 있으면 필수다.
        shared_ptr<const BehaviorTreeAsset> _pCommanderTree;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SkirmishMatch
     * @brief 교전 한 판입니다. 크기 · 절벽 · 플레이어 수 · 시작 지점 · 처음 놓이는 것은 지도(`SkirmishMap`)가 정합니다.
     *        시계 · 끝 알림은 리플렉션 상태 칸(`StateIO::reflected`)이고, 월드 · 지갑 · AI 는 손 본문이 그 형식의 `serializeState` 로 싣는다.
     */
    REFLECT()
    class SW_KIT_API SkirmishMatch
    {
    public:
        REFLECT_BODY();

        static constexpr int32 kMinPlayerCount = 2;
        static constexpr int32 kMaxPlayerCount = 8;       ///< 사람 깃발(uint8)의 비트 수
        static constexpr uint8 kHumanPlayer0   = 1u << 0; ///< 사람 깃발 - 0 번을 사람이 맡는다(옛 "사람 여부" 값 1 과 같아 세이브 바이트가 그대로다)
        static constexpr uint8 kHumanPlayer1   = 1u << 1;

        SkirmishMatch();

        SkirmishMatch( const SkirmishMatch& )            = delete;
        SkirmishMatch& operator=( const SkirmishMatch& ) = delete;

        /**
         * @brief 지도를 칠하고 플레이어와 배치를 놓습니다.
         * @param tileMap @p map 이 가리키는 타일맵을 읽은 것입니다(크기 · 절벽 레이어).
         * @param humanMask 사람이 맡는 플레이어 깃발(`kHumanPlayer0` · `kHumanPlayer1`, 0 이면 모두 AI). 사람 쪽 일꾼도 처음에는 광물을 캐러 간다.
         * @return 지도가 틀렸거나(절벽 레이어 없음 · 플레이어 수 · 맵 밖 시작 지점 · 놓을 수 없는 배치) 월드(`RTSWorld::initialize`)가 거절하면 까닭을 남기고 false 입니다. 그러면 판을 시작하지 않는다.
         */
        [[nodiscard]] bool initialize( const RTSCatalog* pCatalog, const SkirmishMap& map, const TileMapXMLData& tileMap, uint8 humanMask, const SkirmishMatchSettings& matchSettings );
        /** @brief 월드를 배속 @p timeScale 로 돌리고 AI 는 월드가 진행한 시간만큼 돌려 알림을 넘깁니다. 상태는 30 초마다, 승패는 한 번 로그로 남깁니다. */
        void update( float32 deltaTime, float32 timeScale );
        /** @brief 이번까지 쌓인 월드 알림(화면용 사본)을 꺼냅니다. */
        void pollEvents( vector<RTSEvent>& outListEvent );

        RTSWorld&       getWorld() { return _world; }
        const RTSWorld& getWorld() const { return _world; }
        bool            hasHumanPlayer() const { return _humanMask != 0; }
        bool            isHumanPlayer( int32 player ) const { return 0 <= player && player < getPlayerCount() && ( _humanMask & ( 1u << player ) ) != 0; }
        /** @brief 플레이어 수(지도의 시작 지점 수)입니다. 판을 열기 전에는 0 입니다. */
        int32 getPlayerCount() const { return static_cast<int32>( _listPlayer.size() ); }
        /** @brief 판 상태(세이브와 같은 바이트)의 해시입니다. 락스텝 두 프로세스가 같은 틱에 같은 값을 내야 합니다. */
        uint32                computeChecksum() const;
        bool                  isOver() const { return _world.getWinningTeam() >= 0; }
        SkirmishPlayerSummary makeSummary( int32 player ) const;
        /** @brief 플레이어 @p player 를 맡은 컴퓨터 상대입니다. 사람이 맡았거나 범위 밖이면 nullptr 입니다(자동화 감시 값 `Skirmish.AITask`). */
        const RTSCommanderAI* findAI( int32 player ) const
        {
            const bool bKnown = 0 <= player && player < getPlayerCount();
            return bKnown ? _listPlayer[static_cast<size_t>( player )]._pAI.get() : nullptr;
        }
        /** @brief 플레이어마다 `[Skirmish] t=.. p0 workers .. army ..` 한 줄을 남깁니다. */
        void logStatus() const;

        /**
         * @brief 월드 · AI 진행 · 시계를 씁니다(핫 리로드 · 세이브).
         *        `writeState` 의 바이트로 바꿉니다. 같은 카탈로그 · 같은 사람 여부로 `initialize` 한 뒤에 부릅니다.
         *        @return 사람 여부가 다르거나 깨졌으면 false 입니다. 월드를 읽은 뒤 뒤가 깨지면 월드만 바뀐 채로 남으므로, 부르는 쪽은 실패하면 다시 `initialize` 합니다.
         */
        SW_DECLARE_STATE_IO_IN_PLACE();

    private:
        /** @brief 플레이어 하나입니다. 컴퓨터 상대는 사람이 맡은 자리에 없습니다. */
        struct Player
        {
            Wallet                     _wallet; ///< 광물 · 가스 — 키트 하나만 쓰는 게임이라 매치가 들고 월드에 빌려 준다
            unique_ptr<RTSCommanderAI> _pAI;
            SkirmishPlayerStart        _start;
        };

        /** @brief 지도가 이 카탈로그로 열 수 있는지 봅니다. 틀린 것을 모두 남깁니다. */
        [[nodiscard]] bool validateMap( const RTSCatalog& catalog, const SkirmishMap& map, const TileMapXMLData& tileMap ) const;
        /** @brief 배치를 목록 순서대로 놓습니다. 놓지 못한 것이 있으면 까닭을 남기고 false 입니다. */
        [[nodiscard]] bool placeUnits( const SkirmishMap& map );
        /** @brief 플레이어 @p player 의 일꾼을 시작 지점에서 가까운 광물로 보냅니다(사람 쪽 일꾼도 처음에는 놀지 않는다). */
        void sendWorkersToMinerals( int32 player );
        /** @brief 적 @p enemy 의 시작 지점에 닿아 놀고 있는 @p player 의 병력을 그 적의 가장 가까운 남은 건물로 보냅니다(멀리 지은 보급고가 남아 판이 끝나지 않는 일을 막는다). */
        void huntRemaining( int32 player, int32 enemy );

    private:
        RTSWorld              _world;
        ConsumerLease         _eventLease; ///< 월드의 이벤트를 꺼내는 표(이 매치 하나만 꺼낸다)
        vector<Player>        _listPlayer; ///< 판을 열 때 크기를 정한다 — 월드가 지갑의 주소를 쥔다
        vector<RTSEvent>      _listEvent;  ///< 화면이 꺼내 갈 사본
        vector<RTSEvent>      _listFrameEvent;
        SkirmishMatchSettings _settings;
        PROPERTY( RuntimeState, Min = 0 )
        float32 _statusTimer;
        PROPERTY( RuntimeState, Min = 0 )
        float32 _huntTimer;
        uint8   _humanMask; ///< 사람이 맡은 플레이어 깃발(`kHumanPlayer0` · `kHumanPlayer1`)
        PROPERTY( RuntimeState, Min = 0, Max = 1 )
        uint8 _bReportedOver;
    };
} // namespace sw
