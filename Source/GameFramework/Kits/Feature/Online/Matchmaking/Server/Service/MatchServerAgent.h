/**
 * @file MatchServerAgent.h
 * @brief 전용 게임 서버가 자기에게 배정된 경기를 받습니다. 버스 `mm.assign.<자기 서버 id>` 로 접속할 사람 목록(경기 id, 계정, 팀)을 받아 2분 동안 보관하고, 접속한 계정이 목록에 있는지 확인합니다.
 * @details 게임 서버는 UDP 접속을 받을 때 계정 키트의 접속 인증기가 준 주체로 `findExpected` 를 부르며, 목록에 없으면 거절한다.
 *          서버가 매칭 결과를 미리 알고 접속할 때 확인하는 방식은 PlayFab Multiplayer Servers 의 세션 플레이어 목록 확인과 같다.
 *          버스 구독은 서버 조립 단계(또는 `MatchmakingServer`)가 `getAssignTopic` 으로 한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Shared/MatchmakingTypes.h"

namespace sw
{
    /**
     * @class MatchServerAgent
     * @brief 게임 서버 쪽에서 배정을 받는 객체입니다. 서버 루프 스레드 하나에서 씁니다.
     */
    class SW_KIT_SERVER_API MatchServerAgent
    {
    public:
        static constexpr int64 kExpectTtlMs = 120000; ///< 2분이다. 오지 않은 사람은 지운다

        MatchServerAgent();

        void   initialize( uint64 serverID );
        string getAssignTopic() const;
        /** @brief 버스 메시지 본문(`MatchmakingProtocol::writeFormed`)을 처리합니다. 손상됐으면 false 를 반환합니다. */
        [[nodiscard]] bool handleAssign( const vector<uint8>& bytes, int64 nowMs );
        /** @brief 시한이 지난 접속 예정자를 지웁니다. */
        void tick( int64 nowMs );

        /** @brief 접속한 계정이 접속 예정자이면 경기 id 와 팀을 돌려줍니다. */
        bool findExpected( AccountID accountID, uint64& outMatchID, int32& outTeam ) const;
        /** @brief 새로 배정된 경기를 게임 로직이 받아 준비합니다. */
        void  drainNewMatches( vector<MatchFormed>& outListMatch ) { _newMatchBuffer.drainTo( outListMatch ); }
        int32 getExpectedCount() const { return static_cast<int32>( _mapAccountToExpected.size() ); }

    private:
        struct Expected
        {
            uint64 _matchID{ 0 };
            int64  _expiresMs{ 0 };
            int32  _team{ 0 };
        };

        unordered_map<AccountID, Expected> _mapAccountToExpected;
        EventBuffer<MatchFormed>           _newMatchBuffer;
        uint64                             _serverID;
    };
} // namespace sw
