/**
 * @file MatchMaker.h
 * @brief 모드 하나의 대기열과 경기 만들기를 담당합니다. 결과가 결정적이며 전송과 저장은 모릅니다. 권한 서버의 서비스 스레드 하나에서 씁니다.
 * @details - 앵커: 가장 오래 기다린 티켓부터 처리한다. 실력 허용 범위는 기본값에 초당 넓힘을 기다린 초만큼 곱해 더한 값이며 상한이 있다.
 *            후보는 평균 실력 차가 두 티켓의 허용 범위 중 큰 쪽 안에 속하는 티켓이다.
 *          - 지역: 같은 지역끼리 묶는다. 앵커나 후보가 `_regionRelaxMs` 를 넘게 기다렸으면 지역을 따지지 않는다. 경기의 지역은 앵커의 지역이다.
 *          - 채우기: 후보를 앵커에 가까운 실력 순(같으면 오래 기다린 순)으로 넣어 슬롯(팀 수 × 팀 인원)을 정확히 채운다.
 *            파티는 쪼개지 않는다. 큰 티켓부터 슬롯이 남은 팀 중 실력 합이 가장 낮은 팀에 넣는 그리디 방식이며, 나눌 수 없으면 이번 처리에서는 그 앵커를 건너뛴다.
 *          - 시한: `_maxWaitMs` 를 넘긴 티켓은 빼서 돌려준다.
 *          - 백필: 새 경기를 만들기 전에 경기 중 빈 슬롯(`MatchBackfillRequest`)를 오래된 요청부터 채운다. 티켓은 오래 기다린 순으로 보고,
 *            파티를 쪼개지 않고 한 팀의 빈 슬롯에 다 들어가야 하며, 평균 실력 차가 티켓의 허용 범위 안이고 지역이 같거나 완화돼야 한다.
 *
 *          PlayFab Matchmaking 의 규칙 세 가지(실력 확장, 지역 완화, 팀 균형)를 데이터 구조체 하나(`MatchModeDefinition`)로 표현한 것이다.
 */
#pragma once
#include "Core/Container/IDAllocator.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingTypes.h"

namespace sw
{
    /**
     * @class MatchMaker
     * @brief 모드 하나의 대기열입니다.
     */
    class SW_KIT_SERVER_API MatchMaker
    {
    public:
        MatchMaker();

        /**
         * @brief 규칙과 경기 id 시드로 빈 대기열을 만듭니다. 시드는 권한 서버 id 의 아래쪽 32비트이며, 권한이 넘어가도 경기 id 가 겹치지 않습니다.
         * @return 규칙이 선언된 범위 밖이면 오류를 남기고 false 입니다. 대기열은 그대로입니다.
         */
        [[nodiscard]] bool initialize( const MatchModeDefinition& definition, uint64 matchIDSeed );

        /** @brief 경기를 만듭니다. 만든 경기는 @p outListMatch 뒤에, 시한을 넘긴 티켓은 @p outListTimedOut 뒤에 덧붙입니다. */
        void process( int64 nowMs, vector<MatchFormed>& outListMatch, vector<MatchTicket>& outListTimedOut );

        /** @brief 빈 슬롯을 채워 @p outListBackfilled 뒤에 덧붙입니다. 다 찼거나 시한이 지난 요청은 지웁니다. `process` 보다 먼저 부릅니다. */
        void processBackfills( int64 nowMs, vector<MatchBackfilled>& outListBackfilled );
        /** @brief 빈 슬롯 요청을 넣습니다. 같은 경기 id 가 있으면 바꿉니다. 모드가 다르거나 팀 수가 다르거나 빈 슬롯이 음수면 Invalid 입니다. */
        MatchmakingResult addBackfill( const MatchBackfillRequest& request );
        int32             getBackfillCount() const { return static_cast<int32>( _listBackfill.size() ); }

        /** @brief 티켓을 넣습니다. 인원이 0이거나 팀 인원을 넘으면 PartyTooLarge, 같은 id 가 있으면 AlreadyQueued 를 반환합니다. */
        MatchmakingResult  addTicket( const MatchTicket& ticket );
        [[nodiscard]] bool removeTicket( uint64 ticketID );
        bool               hasTicket( uint64 ticketID ) const;
        /** @brief 티켓을 모두 뺍니다. 규칙과 다음 경기 id 는 그대로라 권한을 되찾아도 경기 id 가 겹치지 않습니다. 권한을 잃은 대기열이 씁니다. */
        void clearTickets()
        {
            _listTicket.clear();
            _listBackfill.clear();
        }

        int32                      getTicketCount() const { return static_cast<int32>( _listTicket.size() ); }
        const MatchModeDefinition& getDefinition() const { return _definition; }
        /** @brief 티켓의 현재 실력 허용 범위입니다. 테스트와 클라이언트의 "넓히는 중" 표시에 씁니다. */
        int32 computeRatingWindow( const MatchTicket& ticket, int64 nowMs ) const;

    private:
        [[nodiscard]] bool tryFormMatch( int32 anchorIndex, int64 nowMs, vector<uint8>& inoutListUsed, MatchFormed& outMatch ) const;
        [[nodiscard]] bool partitionIntoTeams( const vector<int32>& listTicketIndex, MatchFormed& outMatch ) const;

        vector<MatchTicket>          _listTicket;
        vector<MatchBackfillRequest> _listBackfill; ///< 들어온 순서(오래된 것 먼저)이다
        MatchModeDefinition          _definition;
        IDAllocator<uint64>          _matchIDAllocator;
    };
} // namespace sw
