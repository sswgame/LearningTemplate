/**
 * @file ChatServer.h
 * @brief 채팅의 스트림 바인딩 — `IOnlineService`(영역 `kChat`). 요청을 꼬리표(`ServicePendingTable`)로 로직에 맡기고, 서비스 틱마다 완료 → 답 · 전달 → 알림 ·
 *        버스 구독 바뀜 → 호스트로 내고, 구독한 버스 메시지를 로직에 넘깁니다.
 * @details - 응답 몸 = `ChatProtocol::writeReply`(업무 결과 + 칸). 공통 오류는 깨진 몸(`kInvalidRequest`) · 모르는 메서드(`kNotFound`) · 로직 없음(`kUnavailable`)뿐이다.
 *          - 계정이 떠나면(`onAccountLeft`) 모든 채널에서 뺀다. 닫힌 연결의 답은 호스트가 버리고, 꼬리표는 완료 때 꺼내므로 쌓이지 않는다.
 *          - 호스트에 올리는 것은 부르는 쪽이(`host.registerService( &server )` — 호스트 `initialize` 전에). 버스 구독은 서비스 틱에 낸다(호스트가 버스를 받은 뒤).
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
     * @brief 채팅 바인딩입니다(호스트 `tick` 스레드).
     */
    class SW_GF_API ChatServer final : public IOnlineService
    {
    public:
        ChatServer();

        /** @brief @p pService 는 빌려 쓴다(첫 서비스 틱 전에). */
        void initialize( ChatService* pService );
        /** @brief 버스 구독을 풀고 기다리던 꼬리표를 버립니다. 로직은 부르는 쪽이 내린다. */
        void shutdown();

        // IOnlineService
        uint16 getMethodRange() const override { return OnlineMethodRange::kChat; }
        uint32 getProtocolVersion() const override { return ChatProtocol::kVersion; }
        void   onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void   onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;
        void   onHostShutdown( OnlineServiceHost& host ) override;
        void   onAccountLeft( OnlineServiceHost& host, AccountId accountId ) override;
        void   onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message ) override;

    private:
        void flushServiceOutput( OnlineServiceHost& host );

        ServicePendingTable        _pendingTable;
        vector<ChatCompletion>     _listCompletionScratch;
        vector<ChatDelivery>       _listDeliveryScratch;
        vector<ChatBusTopicChange> _listTopicScratch;
        vector<string>             _listSubscribedTopic; ///< 내릴 때 풀 주제
        ChatService*               _pService;
        OnlineServiceHost*         _pHost; ///< 버스를 구독한 호스트(풀 때)
    };
} // namespace sw
