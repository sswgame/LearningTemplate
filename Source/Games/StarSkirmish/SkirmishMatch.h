/**
 * @file SkirmishMatch.h
 * @brief StarSkirmish 의 한 판 규칙 — 절차 맵(두 기지 · 광물 · 간헐천 · 절벽), 시작 유닛, 컴퓨터 상대(`RtsAiController`), 정리 사냥, 상태 · 승패 로그입니다.
 *
 * @details 화면 · 입력을 모르는 순수 규칙이라 엔진 밖 하네스에서 그대로 돌려 볼 수 있습니다(키트 `RtsWorld` · `RtsAiController` 만 씁니다).
 *          사람이 0 번을 맡으면 AI 는 1 번만, 자동 플레이면 둘 다 AI 입니다. 두 AI 는 성향이 다릅니다(0 번 러시 · 1 번 운영) — 같은 AI 끼리면 대칭이라
 *          가운데서 서로 지우기만 하고 끝나지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/Kits/Strategy/RealTimeStrategy/RtsAiController.h"
#include "GameFramework/Kits/Strategy/RealTimeStrategy/RtsWorld.h"

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
        void initialize( const RtsCatalog* pCatalog, bool bHumanPlayer );
        /** @brief 월드 · AI 를 돌리고 알림을 AI 에 넘깁니다. 상태는 30 초마다, 승패는 한 번 로그로 남깁니다. */
        void update( float32 deltaTime );
        /** @brief 이번까지 쌓인 월드 알림(화면용 사본)을 꺼냅니다. */
        void drainEvents( vector<RtsEvent>& outListEvent );

        RtsWorld&             getWorld() { return _world; }
        const RtsWorld&       getWorld() const { return _world; }
        bool                  isCliff( int32 x, int32 y ) const;
        bool                  hasHumanPlayer() const { return _bHumanPlayer != SW_FALSE; }
        bool                  isOver() const { return _world.getWinningTeam() >= 0; }
        SkirmishPlayerSummary makeSummary( int32 player ) const;
        /** @brief `[Skirmish] t=.. p0 workers .. army .. | p1 ...` 한 줄을 남깁니다. */
        void logStatus() const;

    private:
        void      paintMap();
        void      spawnBase( int32 player, bool bMirror );
        void      spawnResource( const utf8* pDefId, int32 x, int32 y, bool bMirror );
        RtsUnitId spawnAt( const utf8* pDefId, int32 owner, int32 x, int32 y, bool bMirror );
        /** @brief 적 시작 지점에 닿아 놀고 있는 병력을 가장 가까운 남은 적 건물로 보냅니다(멀리 지은 보급고가 남아 판이 끝나지 않는 일을 막는다). */
        void huntRemaining( int32 player );

        RtsWorld         _world;
        RtsAiController  _arrAi[kPlayerCount];
        vector<RtsEvent> _listEvent; ///< 화면이 꺼내 갈 사본
        vector<RtsEvent> _listFrameEvent;
        vector<uint8>    _listCliff;
        float32          _statusTimer;
        float32          _huntTimer;
        uint8            _arrAiActive[kPlayerCount];
        uint8            _bHumanPlayer;
        uint8            _bReportedOver;
    };
} // namespace sw
