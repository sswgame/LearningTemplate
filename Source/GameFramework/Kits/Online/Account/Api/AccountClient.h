/**
 * @file AccountClient.h
 * @brief 계정 키트의 클라이언트 — `OnlineServiceClient` 에 올리는 `IOnlineClientService`. 가입 · 로그인 · 게스트 · 외부 로그인 · 재접속 · 로그아웃 · 연동 · 해제 ·
 *        탈퇴 · 게임 접속 표를 보내고 응답 · 알림(밀려남)을 꺼냅니다.
 * @details - 로그인 · 재접속에 성공하면 세션 토큰을 메모리에 든다. 다시 연결되면(Hello 뒤 `onClientReady`) 그 토큰으로 재접속을 먼저 보내고, 그동안 낸 세션 요청은
 *            재접속 응답까지 모았다가 보낸다(서버가 아직 주체를 모르는 순간에 닿지 않게). 재접속이 실패하면 모은 요청은 그 결과로 끝난다.
 *          - 밀려남 알림(`kPushRevoked`)을 받으면 토큰을 버리고 사건을 낸다 — 서버가 곧 연결을 닫는다. 토큰을 디스크에 둘지는 게임이 정한다(`getToken` · `setToken`).
 *          - 게스트 장치 비밀은 `AccountDeviceSecret`(로컬 저장 봉인 Encrypted)이 마련한다.
 *          - 모든 호출 · 꺼내기는 `OnlineServiceClient::tick` 의 스레드.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Account/Protocol/AccountTypes.h"

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

    /** @brief 끝난 요청 하나입니다. 전송 · 공통 오류면 `_errorCode`(`OnlineError`)가 0 이 아니고 `_result` 는 그 뜻에 가까운 값입니다. */
    struct AccountClientReply
    {
        LoginGrant             _grant{};
        NetGameTicket          _ticket{};
        AccountLinkSummary     _linkSummary{};
        uint64                 _requestId{ 0 };
        uint16                 _errorCode{ 0 };
        LoginResult            _result{ LoginResult::Ok };
        AccountClientOperation _operation{ AccountClientOperation::Login };
        uint8                  _bAutomatic{ SW_FALSE }; ///< 다시 연결된 뒤 이 객체가 스스로 보낸 재접속
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
    class SW_GF_API AccountClient final : public IOnlineClientService
    {
    public:
        AccountClient();

        /** @brief @p pClient 는 빌려 쓴다 — `registerClientService( this )` 는 부르는 쪽이(서비스 클라이언트 초기화 전에). */
        void initialize( OnlineServiceClient* pClient, const AccountClientInfo& clientInfo );

        uint64 registerAccount( string_view loginName, string_view password );
        uint64 login( string_view loginName, string_view password );
        uint64 guestLogin( const uint8 ( &arrDeviceSecret )[LoginConstant::kDeviceSecretSize] );
        uint64 platformLogin( string_view provider, const vector<uint8>& ticketBytes );
        /** @brief 들고 있는 토큰으로 재접속합니다(토큰이 없으면 바로 InvalidToken 응답). */
        uint64 resume();
        uint64 logout();
        uint64 linkCredential( string_view loginName, string_view password );
        uint64 linkPlatform( string_view provider, const vector<uint8>& ticketBytes );
        uint64 issueGameTicket( string_view serverId );
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

        /** @brief 접속 표로 UDP 자격(표 64 B · 표 비밀)을 채웁니다. */
        static void makeConnectCredentials( const NetGameTicket& ticket, NetConnectCredentials& outCredentials );

        // IOnlineClientService
        uint16 getMethodRange() const override;
        uint32 getProtocolVersion() const override;
        void   onServicePush( uint16 kind, BitReader& body ) override;
        void   onClientReady( OnlineServiceClient& client ) override;
        void   onClientDisconnected( OnlineServiceClient& client ) override;

    private:
        struct PendingCall
        {
            uint64                 _requestId{ 0 };
            AccountClientOperation _operation{ AccountClientOperation::Login };
            uint8                  _bAutomatic{ SW_FALSE };
        };

        struct DeferredCall
        {
            vector<uint8>          _bodyBytes{};
            uint64                 _requestId{ 0 };
            uint16                 _method{ 0 };
            AccountClientOperation _operation{ AccountClientOperation::Logout };
        };

        uint64 send( AccountClientOperation operation, uint16 method, const BitWriter& body, bool bNeedsSession, bool bAutomatic = false );
        void   transmit( AccountClientOperation operation, uint16 method, const vector<uint8>& bodyBytes, uint64 requestId, bool bAutomatic );
        void   onResponse( const OnlineResponse& response );
        void   finishDeferred( LoginResult failure );

        unordered_map<uint64, PendingCall> _mapClientIdToCall; ///< 서비스 클라이언트의 요청 id → 이 객체의 요청
        vector<AccountClientReply>         _listReply;
        vector<AccountClientEvent>         _listEvent;
        vector<DeferredCall>               _listDeferred;
        AccountClientInfo                  _clientInfo;
        AccountIdentity                    _identity;
        LoginSessionToken                  _token;
        PendingCall                        _sendingCall; ///< `sendRequest` 가 그 자리에서 응답을 부를 때의 짝
        OnlineServiceClient*               _pClient;
        uint64                             _nextRequestId;
        uint8                              _bLoggedIn;
        uint8                              _bResuming;
        uint8                              _bSending;
    };
} // namespace sw
