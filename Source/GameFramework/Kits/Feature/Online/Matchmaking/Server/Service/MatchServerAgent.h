/**
 * @file MatchServerAgent.h
 * @brief 전용 게임 서버가 자기에게 배정된 경기를 받습니다. 버스 `mm.assign.<자기 서버 id>` 로 접속할 사람 목록(경기 id, 계정, 팀)을 받아 2분 동안 보관하고, 접속한 계정이 목록에 있는지 확인합니다.
 * @details 게임 서버는 이 객체를 계정 키트의 접속 인증기에 받아들이기 정책(`IConnectAdmission`)으로 넘긴다. 티켓이 맞아도 목록에 없는 계정은 거절된다.
 *          인증기는 네트워크 스레드에서 묻고 배정과 시한은 서버 루프에서 바뀌므로 목록은 잠금 하나로 지킨다.
 *          서버가 매칭 결과를 미리 알고 접속할 때 확인하는 방식은 PlayFab Multiplayer Servers 의 세션 플레이어 목록 확인과 같다.
 *          버스 구독은 서버 조립 단계(또는 `MatchmakingServer`)가 `getAssignTopic` 으로 한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Directory/ServerRecord.h"
#include "GameFramework/Base/Online/Identity/ConnectAdmission.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Shared/MatchmakingTypes.h"

namespace sw
{
    class ServerBus;
    class ServerBusMessage;

    /**
     * @class MatchServerAgent
     * @brief 게임 서버 쪽에서 배정을 받는 객체입니다. `isAdmitted` 외에는 서버 루프 스레드 하나에서 씁니다.
     */
    class SW_KIT_SERVER_API MatchServerAgent final : public IConnectAdmission
    {
    public:
        static constexpr int64 kExpectTtlMs = 120000; ///< 2분이다. 오지 않은 사람은 지운다

        MatchServerAgent();

        /** @brief 이 게임 서버의 설명(서버 id · 주소 · 첫 게임 포트 · 포트 수)을 받습니다. 백필 요청의 서버 정보는 이것으로 채운다 — 부르는 쪽이 따로 적지 않는다. */
        void   initialize( const ServerDescriptor& descriptor );
        string getAssignTopic() const;
        /** @brief 배정 주제(`MatchQueueBus::kAssignTopic`)의 메시지를 처리합니다. 읽지 못했으면(읽기가 로그와 카운터를 남겼다) false 를 반환합니다. */
        [[nodiscard]] bool handleAssign( const ServerBusMessage& message, int64 nowMs );
        /** @brief 경기 중 빈자리를 권한 매처에 보냅니다(버스 `mm.backfill.<모드>`). 요청의 서버 id · 주소 · 포트 · 포트 수는 이 서버의 설명으로 채웁니다(포트 샤딩 — 매처가 채운 사람마다 샤드 포트를 고른다). 보내지 못했으면 false 이며 발행이 로그에 남겼습니다. */
        [[nodiscard]] bool requestBackfill( ServerBus& bus, const MatchBackfillRequest& request ) const;
        /** @brief 시한이 지난 접속 예정자를 지웁니다. */
        void update( int64 nowMs );

        /** @brief 접속한 계정이 접속 예정자이면 경기 id 와 팀을 돌려줍니다. */
        bool findExpected( AccountID accountID, uint64& outMatchID, int32& outTeam ) const;
        /** @brief 접속 예정자이면 true 입니다. 접속 인증기가 네트워크 스레드에서 부릅니다. */
        bool isAdmitted( AccountID accountID ) const override;
        /** @brief 새로 배정된 경기를 게임 로직이 받아 준비합니다. */
        void  drainNewMatches( vector<MatchFormed>& outListMatch ) { _newMatchBuffer.drainTo( outListMatch ); }
        int32 getExpectedCount() const;

    private:
        struct Expected
        {
            uint64 _matchID{ 0 };
            int64  _expiresMs{ 0 };
            int32  _team{ 0 };
        };

        mutable mutex                      _mutex; ///< 접속 예정자 목록을 지킨다(인증기는 네트워크 스레드)
        unordered_map<AccountID, Expected> _mapAccountToExpected;
        EventBuffer<MatchFormed>           _newMatchBuffer;
        ServerDescriptor                   _descriptor; ///< 이 게임 서버(백필 요청에 싣는다)
    };
} // namespace sw
