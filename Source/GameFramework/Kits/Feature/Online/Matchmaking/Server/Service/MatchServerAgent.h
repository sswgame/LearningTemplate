/**
 * @file MatchServerAgent.h
 * @brief 전용 게임 서버가 자기에게 배정된 경기를 받습니다 — 버스 `mm.assign.<자기 서버 id>` → 올 사람 표(경기 id · 계정 · 팀, 2 분). 접속한 계정이 표에 있는지 묻습니다.
 * @details 게임 서버가 UDP 접속을 받을 때(계정 키트 접속 인증기가 준 주체로) `findExpected` — 없으면 거절한다. 결과를 서버가 미리 알고 접속 때 확인하는 것은
 *          PlayFab Multiplayer Servers 의 세션 플레이어 목록 확인과 같다. 버스 구독은 서버 조립(또는 `MatchmakingServer`)이 `getAssignTopic` 으로 한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Shared/MatchmakingTypes.h"

namespace sw
{
    /**
     * @class MatchServerAgent
     * @brief 게임 서버 쪽 배정 받기입니다(서버 루프 스레드 하나).
     */
    class SW_GF_API MatchServerAgent
    {
    public:
        static constexpr int64 kExpectTtlMs = 120000; ///< 2 분 — 오지 않은 사람은 지운다

        MatchServerAgent();

        void   initialize( uint64 serverId );
        string getAssignTopic() const;
        /** @brief 버스 메시지 몸(`MatchmakingProtocol::writeFormed`)입니다. 깨졌으면 false. */
        [[nodiscard]] bool handleAssign( const vector<uint8>& bytes, int64 nowMs );
        /** @brief 시한이 지난 올 사람을 지웁니다. */
        void tick( int64 nowMs );

        /** @brief 접속한 계정이 올 사람인가 — 경기 id · 팀입니다. */
        bool findExpected( AccountId accountId, uint64& outMatchId, int32& outTeam ) const;
        /** @brief 게임 로직이 경기를 차린다(새로 배정된 경기). */
        void  drainNewMatches( vector<MatchFormed>& outListMatch ) { _newMatchBuffer.drainTo( outListMatch ); }
        int32 getExpectedCount() const { return static_cast<int32>( _mapAccountToExpected.size() ); }

    private:
        struct Expected
        {
            uint64 _matchId{ 0 };
            int64  _expiresMs{ 0 };
            int32  _team{ 0 };
        };

        unordered_map<AccountId, Expected> _mapAccountToExpected;
        EventBuffer<MatchFormed>           _newMatchBuffer;
        uint64                             _serverId;
    };
} // namespace sw
