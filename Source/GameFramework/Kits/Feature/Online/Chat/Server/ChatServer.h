/**
 * @file ChatServer.h
 * @brief 채팅의 스트림 바인딩입니다. 프로토콜 표(`OnlineProtocolService`, 영역 `kChat`)로 섭니다.
 * @details 요청은 호출 번호를 요청 태그로 삼아 로직에 맡긴다. 서비스 틱마다 완료는 응답으로, 전달할 메시지는 알림으로, 버스 구독 변경은 호스트로 내보내고, 구독한 버스 메시지는 로직에 넘긴다.
 *          - 계정이 떠나면(`onAccountLeft`) 모든 채널에서 뺀다. 닫힌 연결의 응답은 호스트가 버리고, 호출은 완료할 때 표에서 빠지므로 쌓이지 않는다.
 *          - 호스트에 등록하는 일은 호출하는 쪽이 한다(`host.registerServerService( &server )`, 호스트 `initialize` 전에). 버스 구독은 서비스 틱에서 내며 이때는 호스트가 버스를 받은 뒤이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatService.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Service/OnlineProtocolService.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

namespace sw
{
    class MetricRegistry;

    /**
     * @class ChatServer
     * @brief 채팅 바인딩입니다. 호스트 `tick` 스레드에서 씁니다.
     */
    class SW_KIT_SERVER_API ChatServer final : public OnlineProtocolService<ChatServer, ChatProtocol>
    {
    public:
        ChatServer();

        /** @brief @p pService 는 빌려 씁니다. 첫 서비스 틱 전에 지정합니다. @p pMetricRegistry 가 없으면 지표를 세지 않습니다. */
        void initialize( ChatService* pService, MetricRegistry* pMetricRegistry = nullptr );
        /** @brief 버스 구독을 해제하고 기다리던 호출을 버립니다. 로직은 호출하는 쪽이 종료합니다. */
        void shutdown();

        // 프로토콜 표의 메서드 - 결과는 호출 번호로 `takeCompletions` 에서 받는다. 로그인 · 본문은 기반이 먼저 걸렀다.
        void handleCall( OnlineServerCall<ChatMethod::Join> call, const OnlineCallContext& context, const ChatChannelRequest& request );
        void handleCall( OnlineServerCall<ChatMethod::Leave> call, const OnlineCallContext& context, const ChatChannelRequest& request );
        void handleCall( OnlineServerCall<ChatMethod::Send> call, const OnlineCallContext& context, const ChatSendRequest& request );
        void handleCall( OnlineServerCall<ChatMethod::Whisper> call, const OnlineCallContext& context, const ChatWhisperRequest& request );
        void handleCall( OnlineServerCall<ChatMethod::History> call, const OnlineCallContext& context, const ChatHistoryRequest& request );
        void handleCall( OnlineServerCall<ChatMethod::Report> call, const OnlineCallContext& context, const ChatReportRequest& request );

        // IOnlineServerService
        void onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs ) override;
        void onHostShutdown( OnlineServiceHost& host ) override;
        void onAccountLeft( OnlineServiceHost& host, AccountID accountID ) override;
        void onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message ) override;

    private:
        /** @brief 완료 하나를 그 메서드의 응답 타입(`_method`)으로 끝냅니다. 공통 범주면 공통 오류입니다. */
        void respondCompletion( ChatCompletion& completion );

    private:
        void flushServiceOutput( OnlineServiceHost& host );

    private:
        vector<ChatCompletion>              _listCompletionScratch;
        vector<ChatDelivery>                _listDeliveryScratch;
        vector<ChatBusTopicChange>          _listTopicScratch;
        unordered_map<string, Subscription> _mapTopicSubscription; ///< 버스 주제 → 그 구독. 서버가 한 대면 비어 있다
        ChatService*                        _pService;
    };
} // namespace sw
