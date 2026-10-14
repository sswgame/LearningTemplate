/**
 * @file AdminClient.h
 * @brief GM 도구 키트의 클라이언트입니다. 운영 도구와 에디터 패널이 `OnlineServiceClient` 에 등록해 GM 명령을 보냅니다(`IOnlineClientService`).
 * @details 상태를 바꾸는 명령에는 멱등 키를 같이 보내며, 재시도할 때는 응답의 키를 다시 쓴다. GM 연결도 일반 계정 로그인(계정 키트)을 거치고, 권한은 서버가 명령마다 저장소에서 읽는다.
 *          플레이어 게임은 이 키트에 의존하지 않는다. 운영 도구와 에디터만 쓴다. 콜백은 `OnlineServiceClient::tick` 스레드에서 정확히 한 번 불린다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/ServiceClientCallTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Admin/Shared/AdminProtocol.h"

namespace sw
{
    /** @brief GM 명령 응답 하나입니다. */
    struct AdminClientReply
    {
        AdminReply        _reply{};
        NetIdempotencyKey _idempotencyKey{}; ///< 재시도할 때 그대로 다시 넘기는 키이다
        uint64            _requestID{ 0 };
        uint16            _method{ 0 };
        uint16            _errorCode{ 0 }; ///< 전송 오류나 공통 오류(`OnlineError`)이다
    };
} // namespace sw

namespace sw
{
    /** @class AdminClient @brief GM 명령 클라이언트입니다. */
    class SW_KIT_API AdminClient final : public IOnlineClientService
    {
    public:
        using ReplyDelegate = Delegate<void( const AdminClientReply& )>;

        AdminClient();

        /** @brief @p pClient 는 빌려 씁니다. `registerClientService( this )` 는 호출하는 쪽이 부릅니다. */
        void initialize( OnlineServiceClient* pClient );
        /** @brief 명령 하나를 보내고 요청 번호를 반환합니다. 상태를 바꾸는 명령에서 @p key 가 비어 있으면 새로 만듭니다. */
        uint64 sendCommand( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key, ReplyDelegate onReply );

        // IOnlineClientService
        uint16 getMethodRange() const override { return OnlineMethodRange::kAdmin; }
        uint32 getProtocolVersion() const override { return AdminProtocol::kVersion; }
        void   onServicePush( uint16 kind, BitReader& body ) override;

    private:
        struct PendingCall
        {
            ReplyDelegate     _onReply{};
            NetIdempotencyKey _key{};
            uint64            _requestID{ 0 };
            uint16            _method{ 0 };
        };

        void onResponse( const OnlineResponse& response );

        ServiceClientCallTable<PendingCall> _callTable;
        OnlineServiceClient*                _pClient;
        uint64                              _nextRequestID;
    };
} // namespace sw
