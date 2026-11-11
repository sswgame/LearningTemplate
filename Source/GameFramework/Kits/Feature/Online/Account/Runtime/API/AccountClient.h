/**
 * @file AccountClient.h
 * @brief 계정 키트의 클라이언트입니다. 프로토콜 표(`OnlineProtocolClient`)로 서며, 가입, 로그인, 게스트 로그인, 외부 로그인, 재접속, 로그아웃, 연동, 연동 해제,
 *        탈퇴, 게임 접속 티켓 요청을 보내고 응답과 알림(밀려남)을 꺼냅니다.
 * @details - 로그인이나 재접속에 성공하면 세션 토큰을 메모리에 보관한다. 다시 연결되면(Hello 뒤 `onClientReady`) 그 토큰으로 재접속을 먼저 보낸다.
 *            재접속을 보내기 전에 서비스 클라이언트의 세션 요청 보류(`OnlineServiceClient::holdSessionRequests`)를 걸어, 그동안 모든 키트(이 키트 포함)의 세션 요청을
 *            재접속 응답까지 모은다. 서버가 아직 주체를 모르는 순간에 요청이 닿지 않게 하려는 것이다. 재접속이 실패하면 모은 요청은 그 이유로 끝난다
 *            (공통 범주면 그 범주, 죽은 토큰이면 `kUnauthenticated`).
 *          - 밀려남 알림(`AccountMethod::Revoked`)을 받으면 토큰을 버리고 이벤트를 낸다. 서버가 곧 연결을 닫는다. 토큰을 디스크에 저장할지는 게임이 정한다(`getToken`, `setToken`).
 *          - 게스트 장치 비밀은 부르는 쪽(게임)이 만들어 넘긴다(`guestLogin`). 보관은 게임의 로컬 저장소 슬롯이다(`GameLocalStore`, 장치 키로 봉인).
 *          - 모든 호출과 꺼내기는 `OnlineServiceClient::update` 을 부르는 스레드에서 한다. 응답은 `pollReplies` 큐로 간다(로그인 흐름이 틱마다 꺼낸다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Runtime/Protocol/AccountProtocol.h"
#include "GameFramework/Kits/Feature/Online/Account/Runtime/Protocol/AccountTypes.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolClient.h"
#include "GameFramework/Runtime/Online/Service/OnlineServiceClient.h"

namespace sw
{
    struct NetConnectCredentials;

    /** @brief 클라이언트 요청 종류입니다. */
    enum class AccountClientOperation : uint8
    {
        Register = 0,
        Login,
        GuestLogin,
        PlatformLogin,
        Resume,
        Logout,
        LinkCredential,
        LinkPlatform,
        IssueGameTicket,
        UnlinkPlatform,
        ListLinks,
        RequestDeletion,
        CancelDeletion
    };

    /** @brief 끝난 요청 하나입니다. 전송 오류나 공통 범주이면 `_errorCode`(`OnlineError`)가 0이 아니며 `_result` 도 그 범주입니다(`LoginServiceResult`). */
    struct AccountClientReply
    {
        LoginGrant             _grant{}; ///< 공통 범주면 그 자세한 몸만 담긴다(`kRateLimited` 의 `_retryAfterMs`, `kUpdateRequired` 의 `_storeURL`)
        NetGameTicket          _ticket{};
        AccountLinkSummary     _linkSummary{};
        uint64                 _requestID{ 0 };
        uint16                 _errorCode{ 0 };
        LoginServiceResult     _result{ LoginResult::Ok };
        AccountClientOperation _operation{ AccountClientOperation::Login };
        uint8                  _bAutomatic{ SW_FALSE }; ///< 다시 연결된 뒤 이 객체가 스스로 보낸 재접속이다
    };
} // namespace sw

namespace sw
{
    /** @brief 서버가 세션을 끝냈다는 알림입니다. */
    struct AccountClientEvent
    {
        string            _reasonCode{};
        LoginRevokeReason _reason{ LoginRevokeReason::None };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AccountClient
     * @brief 계정 클라이언트입니다.
     */
    class SW_KIT_API AccountClient final : public OnlineProtocolClient<AccountClient, AccountProtocol>
    {
    public:
        AccountClient();

        /** @brief @p pClient 는 빌려 씁니다. `registerClientService( this )` 는 호출하는 쪽이 서비스 클라이언트를 초기화하기 전에 부릅니다. */
        void initialize( OnlineServiceClient* pClient, const AccountClientInfo& clientInfo );

        uint64 registerAccount( string_view loginName, string_view password );
        uint64 login( string_view loginName, string_view password );
        uint64 guestLogin( const uint8 ( &arrDeviceSecret )[LoginConstant::kDeviceSecretSize] );
        uint64 platformLogin( string_view provider, const vector<uint8>& ticketBytes );
        /** @brief 보관 중인 토큰으로 재접속합니다. 토큰이 없으면 바로 InvalidToken 으로 응답합니다. */
        uint64 resume();
        uint64 logout();
        uint64 linkCredential( string_view loginName, string_view password );
        uint64 linkPlatform( string_view provider, const vector<uint8>& ticketBytes );
        uint64 issueGameTicket( string_view serverID );
        uint64 unlinkPlatform( string_view provider );
        uint64 listLinks();
        uint64 requestDeletion();
        uint64 cancelDeletion();

        int32 pollReplies( vector<AccountClientReply>& outListReply );
        int32 pollEvents( vector<AccountClientEvent>& outListEvent );

        bool                     isLoggedIn() const { return _bLoggedIn == SW_TRUE; }
        const LoginSessionToken& getToken() const { return _token; }
        void                     setToken( const LoginSessionToken& token ) { _token = token; }
        const AccountIdentity&   getIdentity() const { return _identity; }

        /** @brief 접속 티켓으로 UDP 자격(티켓 64 B, 티켓 비밀)을 채웁니다. */
        static void makeConnectCredentials( const NetGameTicket& ticket, NetConnectCredentials& outCredentials );

        // OnlineProtocolClient 의 훅
        void onPush( AccountMethod::Revoked, const AccountRevokedPush& body );
        // IOnlineClientService
        void onClientReady( OnlineServiceClient& client ) override;
        void onClientDisconnected( OnlineServiceClient& client ) override;

    private:
        /** @brief 재접속을 보냅니다. 세션 요청 보류를 먼저 건다 — 응답(`finishResume`)이 푼다. */
        uint64 sendResume( bool bAutomatic );
        void   onGrantReply( const OnlineResult<AccountGrantReply>& result );
        void   onTicketReply( const OnlineResult<AccountTicketReply>& result );
        void   onLinkReply( const OnlineResult<AccountLinkReply>& result );
        /** @brief 응답의 공통 부분(번호, 종류, 결과, 공통 오류의 자세한 몸)을 채웁니다. */
        static AccountClientReply makeReply( uint64 requestID, uint16 method, uint16 errorCode, const vector<uint8>& errorDetail );
        /** @brief 재접속 응답 뒤입니다. 죽은 토큰을 버리고 세션 요청 보류를 풉니다(실패면 모은 요청을 그 이유로 끝낸다). */
        void finishResume( const AccountClientReply& reply );

    private:
        vector<AccountClientReply> _listReply;
        vector<AccountClientEvent> _listEvent;
        AccountClientInfo          _clientInfo;
        AccountIdentity            _identity;
        LoginSessionToken          _token;
        OnlineServiceClient*       _pServiceClient; ///< 세션 요청 보류를 걸고 푸는 데 쓴다. 기반의 서비스 클라이언트와 같다
        uint8                      _bLoggedIn;
        uint8                      _bResuming;
        uint8                      _bAutomaticResume; ///< 날아가는 재접속이 다시 연결된 뒤 이 객체가 스스로 보낸 것이다
    };
} // namespace sw
