/**
 * @file ChatServer.h
 * @brief 채팅의 스트림 바인딩입니다. `IOnlineHostService`(영역 `kChat`)를 구현합니다.
 * @details 요청은 요청 태그(`ServicePendingTable`)와 함께 로직에 맡긴다. 서비스 틱마다 완료는 응답으로, 전달할 메시지는 알림으로, 버스 구독 변경은 호스트로 내보내고, 구독한 버스 메시지는 로직에 넘긴다.
 *          - 응답 본문은 `ChatProtocol::writeReply`(업무 결과와 필드)이다. 공통 오류는 손상된 본문(`kInvalidRequest`), 모르는 메서드(`kNotFound`), 로직 없음(`kUnavailable`)뿐이다.
 *          - 계정이 떠나면(`onAccountLeft`) 모든 채널에서 뺀다. 닫힌 연결의 응답은 호스트가 버리고, 요청 태그는 완료할 때 꺼내므로 쌓이지 않는다.
 *          - 호스트에 등록하는 일은 호출하는 쪽이 한다(`host.registerHostService( &server )`, 호스트 `initialize` 전에). 버스 구독은 서비스 틱에서 내며 이때는 호스트가 버스를 받은 뒤이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Service/ServicePendingTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatService.h"

namespace sw
{
    /**
     * @class ChatServer
     * @brief 채팅 바인딩입니다. 호스트 `tick` 스레드에서 씁니다.
     */
    class SW_KIT_SERVER_API ChatServer final : public IOnlineHostService
    {
    public:
        ChatServer();

        /** @brief @p pService 는 빌려 씁니다. 첫 서비스 틱 전에 지정합니다. */
        void initialize( ChatService* pService );
        /** @brief 버스 구독을 해제하고 기다리던 요청 태그를 버립니다. 로직은 호출하는 쪽이 종료합니다. */
        void shutdown();

        // IOnlineHostService
        uint16 getMethodRange() const override { return OnlineMethodRange::kChat; }
        uint32 getProtocolVersion() const override { return ChatProtocol::kVersion; }
        void   onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void   onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;
        void   onHostShutdown( OnlineServiceHost& host ) override;
        void   onAccountLeft( OnlineServiceHost& host, AccountID accountID ) override;
        void   onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message ) override;

    private:
        void flushServiceOutput( OnlineServiceHost& host );

        ServicePendingTable        _pendingTable;
        vector<ChatCompletion>     _listCompletionScratch;
        vector<ChatDelivery>       _listDeliveryScratch;
        vector<ChatBusTopicChange> _listTopicScratch;
        vector<string>             _listSubscribedTopic; ///< 종료할 때 해제할 주제이다
        ChatService*               _pService;
        OnlineServiceHost*         _pHost; ///< 버스를 구독한 호스트이다. 구독을 해제할 때 쓴다
    };
} // namespace sw
