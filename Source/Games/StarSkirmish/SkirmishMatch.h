/**
 * @file SkirmishMatch.h
 * @brief StarSkirmish 의 한 판 규칙 — 절차 맵(두 기지 · 광물 · 간헐천 · 절벽), 시작 유닛, 컴퓨터 상대(`RTSCommanderAI`), 정리 사냥, 상태 · 승패 로그입니다.
 *
 * @details 화면 · 입력을 모르는 순수 규칙이라 엔진 밖 하네스에서 그대로 돌려 볼 수 있습니다(키트 `RTSWorld` · `RTSCommanderAI` 만 씁니다).
 *          사람이 0 번을 맡으면 AI 는 1 번만, 자동 플레이면 둘 다 AI 입니다. 두 AI 는 성향이 다릅니다(0 번 러시 · 1 번 운영) — 같은 AI 끼리면 대칭이라
 *          가운데서 서로 지우기만 하고 끝나지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCommanderAI.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"

namespace sw
{
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
    struct SkirmishAIStyle
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
        // 컴퓨터 상대의 성향 — 둘 다 AI 면 0 번 러시 · 1 번 운영(같은 AI 끼리면 대칭이라 끝나지 않는다), 사람과 붙으면 상대 성향이다.
        SkirmishAIStyle _rushAI{};
        SkirmishAIStyle _macroAI{};
        SkirmishAIStyle _opponentAI{};
        // 컴퓨터 상대의 행동 트리 — 데이터(`commander.bt.xml`)에서 읽은 것을 디렉터가 가리킨다. 판이 도는 동안 살아 있어야 하고, AI 는 시작할 때 복사해 쥔다. 컴퓨터 상대가 있으면 필수다.
        const BehaviorTree* _pCommanderTree{ nullptr };
    };
} // namespace sw

namespace sw
{
    /**
     * @class SkirmishMatch
     * @brief 64×64 맵의 1 대 1 한 판입니다. 0 번은 남서쪽, 1 번은 북동쪽(점 대칭)에서 시작합니다.
     */
    class SkirmishMatch
    {
    public:
        static constexpr int32 kMapSize     = 64;
        static constexpr int32 kPlayerCount = 2;

        SkirmishMatch();

        SkirmishMatch( const SkirmishMatch& )            = delete;
        SkirmishMatch& operator=( const SkirmishMatch& ) = delete;

        /**
         * @brief 맵을 칠하고 두 기지(본진 · 일꾼 넷 · 광물 여덟 · 간헐천)를 놓습니다.
         * @param bHumanPlayer 0 번을 사람이 맡는가(아니면 둘 다 AI). 사람 쪽 일꾼도 처음에는 광물을 캐러 간다.
         */
        void initialize( const RTSCatalog* pCatalog, bool bHumanPlayer, const SkirmishMatchSettings& matchSettings );
        /** @brief 월드 · AI 를 돌리고 알림을 AI 에 넘깁니다. 상태는 30 초마다, 승패는 한 번 로그로 남깁니다. */
        void update( float32 deltaTime );
        /** @brief 이번까지 쌓인 월드 알림(화면용 사본)을 꺼냅니다. */
        void drainEvents( vector<RTSEvent>& outListEvent );

        RTSWorld&             getWorld() { return _world; }
        const RTSWorld&       getWorld() const { return _world; }
        bool                  isCliff( int32 x, int32 y ) const;
        bool                  hasHumanPlayer() const { return _bHumanPlayer != SW_FALSE; }
        bool                  isOver() const { return _world.getWinningTeam() >= 0; }
        SkirmishPlayerSummary makeSummary( int32 player ) const;
        /** @brief 플레이어 @p player 를 맡은 컴퓨터 상대입니다. 사람이 맡았거나 범위 밖이면 nullptr 입니다(자동화 탐침 `Skirmish.AITask`). */
        const RTSCommanderAI* findAI( int32 player ) const
        {
            const bool bAIPlayer = 0 <= player && player < kPlayerCount && _arrAIActive[player] != SW_FALSE;
            return bAIPlayer ? &_arrAI[player] : nullptr;
        }
        /** @brief `[Skirmish] t=.. p0 workers .. army .. | p1 ...` 한 줄을 남깁니다. */
        void logStatus() const;

        /** @brief 월드 · AI 진행 · 시계를 씁니다(핫 리로드 · 세이브). */
        void writeState( Archive& outArchive ) const;
        /**
         * @brief `writeState` 의 바이트로 바꿉니다. 같은 카탈로그 · 같은 사람 여부로 `initialize` 한 뒤에 부릅니다.
         * @return 사람 여부가 다르거나 깨졌으면 false 입니다. 월드를 읽은 뒤 뒤가 깨지면 월드만 바뀐 채로 남으므로, 부르는 쪽은 실패하면 다시 `initialize` 합니다.
         */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        void      paintMap();
        void      spawnBase( int32 player, bool bMirror );
        void      spawnResource( const utf8* pDefID, int32 x, int32 y, bool bMirror );
        RTSUnitID spawnAt( const utf8* pDefID, int32 owner, int32 x, int32 y, bool bMirror );
        /** @brief 적 시작 지점에 닿아 놀고 있는 병력을 가장 가까운 남은 적 건물로 보냅니다(멀리 지은 보급고가 남아 판이 끝나지 않는 일을 막는다). */
        void huntRemaining( int32 player );

        RTSWorld              _world;
        RTSCommanderAI        _arrAI[kPlayerCount];
        Wallet                _arrWallet[kPlayerCount]; ///< 플레이어마다 광물 · 가스 — 키트 하나만 쓰는 게임이라 매치가 들고 월드에 빌려 준다
        vector<RTSEvent>      _listEvent;               ///< 화면이 꺼내 갈 사본
        vector<RTSEvent>      _listFrameEvent;
        vector<uint8>         _listCliff;
        SkirmishMatchSettings _settings;
        float32               _statusTimer;
        float32               _huntTimer;
        uint8                 _arrAIActive[kPlayerCount];
        uint8                 _bHumanPlayer;
        uint8                 _bReportedOver;
    };
} // namespace sw
