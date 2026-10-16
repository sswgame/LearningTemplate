/**
 * @file OnlineKitClient.h
 * @brief 키트 클라이언트(우편함 · 경제 · 순위표 등)의 공통 기반입니다. 요청 하나를 보내고 응답을 키트 응답 구조체로 읽어 호출자 델리게이트에 넘기는 일을 한 곳에 둡니다.
 * @details - 키트 클라이언트는 이 기반을 상속하고 요청 함수(`requestX`)에서 요청 메시지를 만들어 `sendRequest` 를 부른다(본문이 없는 메서드는 `sendCall`).
 *            응답 처리 · 연결 전 실패 · 요청 번호 · 멱등 키는 기반이 맡는다.
 *          - 요청 메시지는 키트 메시지 목록(`TProtocol::MessageTypes`)으로 쓴다. 범위 밖 값이 있으면 보내지 않고 `kInvalidRequest` 와 같은 결과로 콜백한다.
 *          - 요청 번호는 이 클라이언트가 붙인다. 반환값과 응답의 `_requestID` 가 같고, 연결 전(`initialize` 전) 실패도 0 이 아닌 번호로 콜백한다.
 *          - 응답 읽기는 `OnlineReplyUtil::readResponse` 규칙이다. 오류 코드면 `TProtocol::fromErrorCode`, 본문을 못 읽으면 결과가 `Unavailable` 이다.
 *          - 성공 응답(본문을 읽었고 결과가 `Ok`)에 캐시를 고치는 키트는 `onCallSucceeded` 를 재정의한다. 콜백보다 먼저 불린다.
 *          - 콜백은 `OnlineServiceClient::update` 스레드에서 요청마다 정확히 한 번 불린다.
 *          언리얼 Online Services 의 인터페이스마다 `TOnlineAsyncOpHandle<T>` 를 돌려주는 공통 비동기 연산 틀과 같은 자리다(기억에 의존).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineReplyUtil.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/ServiceClientCallTable.h"

namespace sw
{
    /** @brief 키트 클라이언트 응답 하나입니다. @p TReply 는 키트의 응답 구조체(`_result` 필드)입니다. */
    template <typename TReply>
    struct OnlineClientReply
    {
        TReply            _reply{};
        NetIdempotencyKey _idempotencyKey{}; ///< 재시도할 때 그대로 다시 넘기는 키이다. 키 없이 보낸 요청이면 비어 있다
        uint64            _requestID{ 0 };   ///< 요청 함수가 반환한 번호이다
        uint16            _method{ 0 };
        uint16            _errorCode{ 0 }; ///< 전송 오류나 공통 오류(`OnlineError`)이다. 0 이 아니면 `_reply._result` 는 그 코드에 대응하는 결과이다
    };
} // namespace sw

namespace sw
{
    /** @brief 호출에 붙일 키트 고유 정보가 없는 클라이언트의 빈 문맥입니다. */
    struct OnlineCallNoContext
    {
    };
} // namespace sw

namespace sw
{
    /**
     * @class OnlineKitClient
     * @brief 키트 클라이언트의 공통 기반입니다.
     * @tparam TProtocol `fromErrorCode` 와 메시지 타입 목록 `MessageTypes` 를 가진 키트 프로토콜 구조체
     * @tparam TReply 키트 응답 구조체. `_result` 열거형에 `Unavailable` 이 있어야 한다
     * @tparam TCallContext 응답을 받을 때 다시 필요한 요청 쪽 정보(우편 키 등). 없으면 `OnlineCallNoContext`
     */
    template <typename TProtocol, typename TReply, typename TCallContext = OnlineCallNoContext>
    class OnlineKitClient : public IOnlineClientService
    {
    public:
        using Reply         = OnlineClientReply<TReply>;
        using ReplyDelegate = Delegate<void( const Reply& )>;

        OnlineKitClient()
            : _callTable{}
            , _pClient{ nullptr }
            , _nextRequestID{ 1 }
        {
        }

        /** @brief @p pClient 는 빌려 씁니다. `registerClientService( this )` 는 호출하는 쪽이 부릅니다. */
        void initialize( OnlineServiceClient* pClient ) { _pClient = pClient; }

        /** @brief 알림을 보내지 않는 키트는 재정의하지 않습니다. */
        void onServicePush( uint16 kind, BitReader& body ) override
        {
            (void)kind;
            (void)body;
        }

    protected:
        struct Call
        {
            ReplyDelegate     _onReply{};
            TCallContext      _context{};
            NetIdempotencyKey _key{};
            uint64            _requestID{ 0 };
            uint16            _method{ 0 };
        };

        /**
         * @brief @p request 를 키트 메시지로 써서 보내고 요청 번호를 반환합니다.
         * @details 범위 밖 값이 있어 쓰지 못하면 보내지 않고 `kInvalidRequest` 로 콜백합니다. 서버가 같은 본문을 받아 거절한 것과 같은 결과입니다.
         */
        template <typename TRequest>
        uint64 sendRequest( uint16 method, const TRequest& request, const NetIdempotencyKey& key, const ReplyDelegate& onReply, const TCallContext& context = TCallContext{} )
        {
            BitWriter body;
            if ( TProtocol::MessageTypes::write( body, request ) )
                return sendCall( method, body, key, onReply, context );
            return failCall( method, key, onReply, OnlineError::kInvalidRequest );
        }

        /** @brief @p body 를 보내고 요청 번호를 반환합니다. 상태를 바꾸는 메서드는 @p key 를 채워 보냅니다(비어 있으면 서버가 중복을 거르지 않는다). */
        uint64 sendCall( uint16 method, const BitWriter& body, const NetIdempotencyKey& key, const ReplyDelegate& onReply, const TCallContext& context = TCallContext{} )
        {
            if ( _pClient == nullptr )
                return failCall( method, key, onReply, OnlineError::kUnavailable );
            const Call        call{ onReply, context, key, _nextRequestID++, method };
            NetRequestOptions options;
            options._idempotencyKey = key;
            (void)_callTable.send( *_pClient, method, body, options, OnlineResponseDelegate::create<&OnlineKitClient::onResponse>( this ), call ); // 응답은 call._requestID 로 알린다
            return call._requestID;
        }

        /** @brief 본문을 읽었고 결과가 `Ok` 인 응답마다 콜백 전에 불립니다. 캐시를 고치는 키트가 재정의합니다. */
        virtual void onCallSucceeded( const Call& call, const TReply& reply )
        {
            (void)call;
            (void)reply;
        }

    private:
        /** @brief 보내지 못한 요청을 바로 콜백합니다. 결과는 공통 오류 코드 @p errorCode 를 키트 표로 바꾼 값입니다. */
        uint64 failCall( uint16 method, const NetIdempotencyKey& key, const ReplyDelegate& onReply, uint16 errorCode )
        {
            Reply reply;
            reply._requestID      = _nextRequestID++;
            reply._method         = method;
            reply._idempotencyKey = key;
            reply._errorCode      = errorCode;
            OnlineReplyUtil::markError<TProtocol>( errorCode, reply._reply );
            if ( onReply.isBound() )
                onReply( reply );
            return reply._requestID;
        }

        void onResponse( const OnlineResponse& response )
        {
            Call call;
            if ( _callTable.take( response._requestID, call ) == false )
                return;
            Reply reply;
            reply._requestID      = call._requestID;
            reply._method         = call._method;
            reply._idempotencyKey = call._key;
            reply._errorCode      = response._errorCode;
            const bool bBodyRead  = OnlineReplyUtil::readResponse<TProtocol>( response, reply._reply );
            if ( bBodyRead && reply._reply._result == decltype( reply._reply._result )::Ok )
                onCallSucceeded( call, reply._reply );
            if ( call._onReply.isBound() )
                call._onReply( reply );
        }

        ServiceClientCallTable<Call> _callTable;
        OnlineServiceClient*         _pClient;
        uint64                       _nextRequestID;
    };
} // namespace sw
