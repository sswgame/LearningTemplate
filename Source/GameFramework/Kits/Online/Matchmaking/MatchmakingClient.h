/**
 * @file MatchmakingClient.h
 * @brief 매칭 클라이언트 — `OnlineServiceClient` 에 올리는 `IOnlineClientService`. 파티 · 로비 · 대기열 요청을 보내고, 알림(파티 · 초대 · 로비 · 매칭 결과)을 모아 둡니다.
 * @details - 요청마다 완료 델리게이트를 정확히 한 번(`tick` 스레드) — 업무 결과는 `_reply._result`, 전송 · 공통 오류는 `_errorCode`(그때 결과는 `fromErrorCode`).
 *          - 매칭 결과가 `Found` 면 게임은 계정 클라이언트로 그 서버(`_serverId` 16 진)의 접속 표를 받아 `_address:_port` 에 UDP 로 붙는다.
 *          - 마지막 파티 스냅숏을 든다(알림 · 응답) — 빈 회원이면 파티가 없다.
 *          언리얼 Online Services 의 비동기 호출 + 완료 델리게이트, 경제 · 서버 디렉터리 클라이언트와 같은 모양이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/ServiceClientCallTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Matchmaking/MatchmakingProtocol.h"

namespace sw
{
    /** @brief 매칭 클라이언트 응답 하나입니다. */
    struct MatchmakingClientReply
    {
        MatchmakingReply _reply{};
        uint64           _requestId{ 0 };
        uint16           _errorCode{ 0 }; ///< 전송 · 공통 오류(`OnlineError`) — 0 이 아니면 `_reply._result` 는 그 코드의 결과
    };
} // namespace sw

namespace sw
{
    /**
     * @class MatchmakingClient
     * @brief 연결 하나의 매칭 창구입니다.
     */
    class SW_GF_API MatchmakingClient final : public IOnlineClientService
    {
    public:
        using ReplyDelegate = Delegate<void( const MatchmakingClientReply& )>;

        MatchmakingClient();

        /** @brief @p pClient 는 빌려 쓴다 — `registerClientService( this )` 는 부르는 쪽이(초기화 전에). */
        void initialize( OnlineServiceClient* pClient );

        uint64 createParty( const ReplyDelegate& onReply );
        uint64 inviteToParty( AccountId targetId, const ReplyDelegate& onReply );
        uint64 acceptPartyInvite( uint64 partyId, const ReplyDelegate& onReply );
        uint64 leaveParty( const ReplyDelegate& onReply );
        uint64 kickFromParty( AccountId targetId, const ReplyDelegate& onReply );
        /** @brief @p request 의 이름 · 모드 · 정원 · 설정으로 로비를 만듭니다. */
        uint64 createLobby( const LobbySnapshot& request, const ReplyDelegate& onReply );
        uint64 listLobbies( string_view modeId, const ReplyDelegate& onReply );
        uint64 joinLobby( uint64 lobbyId, const ReplyDelegate& onReply );
        uint64 leaveLobby( uint64 lobbyId, const ReplyDelegate& onReply );
        uint64 setLobbyReady( uint64 lobbyId, bool bReady, const ReplyDelegate& onReply );
        uint64 startLobby( uint64 lobbyId, const ReplyDelegate& onReply );
        /** @brief 줄 섭니다(파티 장이면 파티 전체). 결과는 매칭 결과 알림으로 온다. */
        uint64 joinQueue( string_view modeId, string_view region, const ReplyDelegate& onReply );
        uint64 leaveQueue( const ReplyDelegate& onReply );

        void drainPartyUpdates( vector<PartySnapshot>& outListParty ) { _partyBuffer.drainTo( outListParty ); }
        void drainInvites( vector<PartyInvite>& outListInvite ) { _inviteBuffer.drainTo( outListInvite ); }
        void drainLobbyUpdates( vector<LobbySnapshot>& outListLobby ) { _lobbyBuffer.drainTo( outListLobby ); }
        void drainMatchResults( vector<MatchAssignment>& outListAssignment ) { _matchBuffer.drainTo( outListAssignment ); }
        /** @brief 마지막으로 받은 내 파티입니다(빈 회원 = 파티 없음). */
        const PartySnapshot& getParty() const { return _party; }

        // IOnlineClientService
        uint16 getMethodRange() const override { return OnlineMethodRange::kMatchmaking; }
        uint32 getProtocolVersion() const override { return MatchmakingProtocol::kVersion; }
        void   onServicePush( uint16 kind, BitReader& body ) override;

    private:
        uint64 send( uint16 method, const BitWriter& body, const ReplyDelegate& onReply );
        void   onResponse( const OnlineResponse& response );

        ServiceClientCallTable<ReplyDelegate> _callTable;
        EventBuffer<PartySnapshot>            _partyBuffer;
        EventBuffer<PartyInvite>              _inviteBuffer;
        EventBuffer<LobbySnapshot>            _lobbyBuffer;
        EventBuffer<MatchAssignment>          _matchBuffer;
        PartySnapshot                         _party;
        OnlineServiceClient*                  _pClient;
    };
} // namespace sw
