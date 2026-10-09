/**
 * @file MatchMaker.h
 * @brief 모드 하나의 대기열과 경기 만들기 — 결정적이고 전송 · 저장을 모릅니다. 권한 서버의 서비스 스레드 하나에서 씁니다.
 * @details - 닻: 가장 오래 기다린 표부터. 실력 창 = 기본 + 초당 넓힘 × 기다린 초(상한). 후보는 평균 실력 차가 두 표의 창 중 큰 것 안인 표.
 *          - 지역: 같은 지역끼리. 닻이나 후보가 `_regionRelaxMs` 넘게 기다렸으면 가리지 않는다(경기의 지역은 닻의 것).
 *          - 채우기: 후보를 닻에 가까운 실력 순(같으면 오래 기다린 순)으로 넣어 자리(팀 수 × 팀 인원)를 정확히 채운다. 파티는 쪼개지 않는다 —
 *            큰 표부터 자리가 남은 팀 중 실력 합이 가장 낮은 팀에 넣고(그리디), 나눌 수 없으면 이번 처리에서는 그 닻을 건너뛴다.
 *          - 시한: `_maxWaitMs` 를 넘긴 표는 빼서 돌려준다.
 *          PlayFab Matchmaking 의 규칙 셋(실력 확장 · 지역 풀기 · 팀 균형)을 데이터 한 구조체(`MatchModeDefinition`)로 둔 모양이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/MatchmakingTypes.h"

namespace sw
{
    /**
     * @class MatchMaker
     * @brief 모드 하나의 대기열입니다.
     */
    class SW_GF_API MatchMaker
    {
    public:
        MatchMaker();

        /** @brief 규칙과 경기 id 씨앗(권한 서버 id 아래 32 비트 — 권한이 넘어가도 경기 id 가 겹치지 않는다)으로 비운 대기열을 만듭니다. */
        void initialize( const MatchModeDefinition& definition, uint64 matchIdSeed );

        /** @brief 경기를 만듭니다 — 만든 경기는 @p outListMatch, 시한을 넘긴 표는 @p outListTimedOut 뒤에 덧붙입니다. */
        void process( int64 nowMs, vector<MatchFormed>& outListMatch, vector<MatchTicket>& outListTimedOut );

        /** @brief 표를 넣습니다. 인원이 0 이거나 팀 인원을 넘으면 PartyTooLarge, 같은 id 가 있으면 AlreadyQueued 입니다. */
        MatchmakingResult  addTicket( const MatchTicket& ticket );
        [[nodiscard]] bool removeTicket( uint64 ticketId );
        bool               hasTicket( uint64 ticketId ) const;

        int32                      getTicketCount() const { return static_cast<int32>( _listTicket.size() ); }
        const MatchModeDefinition& getDefinition() const { return _definition; }
        /** @brief 표의 지금 실력 창입니다(시험 · 클라이언트의 "넓히는 중" 표시). */
        int32 computeRatingWindow( const MatchTicket& ticket, int64 nowMs ) const;

    private:
        [[nodiscard]] bool tryFormMatch( int32 anchorIndex, int64 nowMs, vector<uint8>& inoutListUsed, MatchFormed& outMatch ) const;
        [[nodiscard]] bool partitionIntoTeams( const vector<int32>& listTicketIndex, MatchFormed& outMatch ) const;

        vector<MatchTicket> _listTicket;
        MatchModeDefinition _definition;
        uint64              _nextMatchId;
    };
} // namespace sw
