/**
 * @file MatchmakingClient.h
 * @brief 매칭 클라이언트입니다. 요청은 `call<MatchmakingMethod::QueueJoin>( request, onReply )` 이고, 이 클래스는 알림(파티, 초대, 로비, 매칭 결과)을 모아 둡니다.
 * @details - 요청 · 응답 규칙은 `OnlineProtocolClient` 에 있다. 응답은 메서드마다 따로이고 업무 결과는 `_reply._result`(도메인 값만), 전송 오류와 공통 범주는 `_errorCode` 로 온다.
 *          - 매칭 결과가 `Found` 이면 게임은 계정 클라이언트로 그 서버(`_serverID`, 16진)의 접속 티켓을 받아 `_address:_port` 에 UDP 로 접속한다.
 *          - 마지막으로 받은 파티 스냅샷(알림이나 응답)을 보관한다. 멤버가 비어 있으면 파티가 없는 것이다.
 *
 *          언리얼 Online Services 의 비동기 호출과 완료 델리게이트, 경제와 서버 디렉터리 클라이언트와 같은 구조이다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Event/ConsumerLock.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolClient.h"

namespace sw
{
    /**
     * @class MatchmakingClient
     * @brief 연결 하나의 매칭 클라이언트입니다.
     */
    class SW_KIT_API MatchmakingClient final : public OnlineProtocolClient<MatchmakingClient, MatchmakingProtocol>
    {
    public:
        MatchmakingClient();

        /**
         * @brief 파티 · 초대 · 로비 · 매칭 결과 버퍼를 꺼낼 권한을 @p outLease 에 받습니다.
         * @details 네 버퍼가 한 표를 씁니다(한 클라이언트의 알림은 한 모델이 받는다). 클라이언트마다 한 곳만 받을 수 있어 이미 다른 표가 있으면 false 입니다.
         */
        [[nodiscard]] bool claimNotifications( ConsumerLease& outLease ) { return _notificationConsumerLock.claim( outLease ); }
        /** @brief 아래 `poll*` 는 모두 `claimNotifications` 로 받은 표를 넘겨야 합니다(아니면 어서션이고 꺼내지 않습니다). */
        void pollPartyUpdates( const ConsumerLease& lease, vector<PartySnapshot>& outListParty )
        {
            if ( isConsumerLeaseHeld( lease ) )
                _partyBuffer.moveAllTo( outListParty );
        }
        void pollInvites( const ConsumerLease& lease, vector<PartyInvite>& outListInvite )
        {
            if ( isConsumerLeaseHeld( lease ) )
                _inviteBuffer.moveAllTo( outListInvite );
        }
        void pollLobbyUpdates( const ConsumerLease& lease, vector<LobbySnapshot>& outListLobby )
        {
            if ( isConsumerLeaseHeld( lease ) )
                _lobbyBuffer.moveAllTo( outListLobby );
        }
        void pollMatchResults( const ConsumerLease& lease, vector<MatchAssignment>& outListAssignment )
        {
            if ( isConsumerLeaseHeld( lease ) )
                _matchBuffer.moveAllTo( outListAssignment );
        }
        /** @brief 마지막으로 받은 내 파티입니다. 멤버가 비어 있으면 파티가 없는 것입니다. */
        const PartySnapshot& getParty() const { return _party; }

        // OnlineProtocolClient 의 훅
        void onPush( MatchmakingMethod::Party, const PartySnapshot& body );
        void onPush( MatchmakingMethod::PartyInvited, const PartyInvite& body );
        void onPush( MatchmakingMethod::Lobby, const LobbySnapshot& body );
        void onPush( MatchmakingMethod::Match, const MatchAssignment& body );

    private:
        bool isConsumerLeaseHeld( const ConsumerLease& lease ) const
        {
            SW_ASSERT( _notificationConsumerLock.isHeldBy( lease ) );
            return _notificationConsumerLock.isHeldBy( lease );
        }

    private:
        ConsumerLock                 _notificationConsumerLock;
        EventBuffer<PartySnapshot>   _partyBuffer;
        EventBuffer<PartyInvite>     _inviteBuffer;
        EventBuffer<LobbySnapshot>   _lobbyBuffer;
        EventBuffer<MatchAssignment> _matchBuffer;
        PartySnapshot                _party;
    };
} // namespace sw
