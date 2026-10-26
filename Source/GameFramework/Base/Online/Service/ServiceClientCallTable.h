/**
 * @file ServiceClientCallTable.h
 * @brief 클라이언트 쪽에서 요청 id 로 사용자 델리게이트를 찾는 테이블을 정의합니다.
 * @details 키트 클라이언트는 응답 공통 처리기(멤버 함수 델리게이트 하나)에서 요청마다 다른 사용자 델리게이트를 이 테이블로 찾는다.
 *
 *          - 한 스레드(`OnlineServiceClient::update` 을 부르는 스레드)에서만 쓴다.
 *          - 보내기는 `send`, `sendMessage`, `reject` 로만 한다. 키트 메시지는 `sendMessage` 가 쓰고, 쓰지 못하면(범위 밖 값) 보내지 않고 `kInvalidRequest` 응답으로 끝낸다.
 *            그 응답도 다른 실패와 같이 응답 처리기로 오므로, 키트 클라이언트마다 쓰기 실패를 따로 처리하는 길이 없다.
 *          - 보내지 못한 요청(초기화 전, 멈춤, 쓰기 실패)도 0 이 아닌 요청 id 를 받고, 응답 델리게이트가 보내기 함수 안에서 바로 한 번 불린다.
 *            `OnlineServiceClient::sendRequest` 는 즉시 실패(초기화 전, 버전 불일치, 끊김)를 응답 델리게이트로 부를 수 있다.
 *            그래서 요청 id 를 받은 뒤에 테이블에 넣으면 그 응답이 사용자 델리게이트를 찾지 못한다.
 *            `send` 는 보내는 동안 온 응답에 보내는 중인 델리게이트를 내준다. 거래 클라이언트의 `_sendingCall` 과 같은 역할이다.
 * @code
 *     uint64 XClient::requestFoo( FooDelegate onFoo )
 *     {
 *         return _fooCallTable.send( *_pClient, XMethod::kFoo, body, NetRequestOptions{}, OnlineResponseDelegate::create<&XClient::onFooResponse>( this ), onFoo );
 *     }
 *     void XClient::onFooResponse( const OnlineResponse& response )
 *     {
 *         FooDelegate onFoo;
 *         if ( _fooCallTable.take( response._requestID, onFoo ) && onFoo.isBound() )
 *             onFoo( response._errorCode, ... ); // _errorCode: OnlineError::kOk, 공통 코드, 키트 코드. 전송 실패는 kUnavailable
 *     }
 * @endcode
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"

namespace sw
{
    /**
     * @class ServiceClientCallTable
     * @brief 요청 id 와 델리게이트를 짝짓는 테이블입니다.
     * @details 클라이언트, 본문, 옵션, 응답 델리게이트 타입은 `send` 의 템플릿 인자라 이 헤더는 `OnlineServiceClient.h` 를 끌어오지 않는다.
     *          @p TClient 는 `sendRequest( method, body, options, onResponse )`, `rejectRequest( errorCode, onResponse )`, `peekNextRequestID()` 를 가진다(`OnlineServiceClient`).
     *          대기 중인 호출은 버리는 API 가 없다. 클라이언트는 요청마다 콜백을 정확히 한 번 부르므로(끊김과 시한도 실패 응답이다) 테이블은 응답으로만 빈다.
     */
    template <typename TDelegate>
    class ServiceClientCallTable
    {
    public:
        /** @brief 요청을 보내고 @p onDone 을 그 요청 id 에 붙인 뒤 요청 id 를 반환합니다. 응답 처리기 @p onResponse 는 `take` 로 @p onDone 을 꺼냅니다. */
        template <typename TClient, typename TBody, typename TOptions, typename TOnResponse>
        uint64 send( TClient& client, uint16 method, const TBody& body, const TOptions& options, const TOnResponse& onResponse, const TDelegate& onDone )
        {
            return dispatch( client, onDone, [&]()
            { return client.sendRequest( method, body, options, onResponse ); } );
        }

        /**
         * @brief @p message 를 키트 메시지 목록 @p TMessages 로 써서 보냅니다(`send`).
         * @details 범위 밖 값이 있어 쓰지 못하면 보내지 않고 `kInvalidRequest` 응답으로 끝냅니다(`reject`). 서버가 같은 본문을 거절한 것과 같은 결과입니다.
         */
        template <typename TMessages, typename TClient, typename TMessage, typename TOptions, typename TOnResponse>
        uint64 sendMessage( TClient& client, uint16 method, const TMessage& message, const TOptions& options, const TOnResponse& onResponse, const TDelegate& onDone )
        {
            BitWriter body;
            if ( TMessages::write( body, message ) )
                return send( client, method, body, options, onResponse, onDone );
            return reject( client, OnlineError::kInvalidRequest, onResponse, onDone );
        }

        /** @brief 보내지 않은 요청 하나를 @p errorCode 응답으로 끝내고 요청 id 를 반환합니다. 응답은 이 함수 안에서 @p onResponse 로 오고, 처리기는 `take` 로 @p onDone 을 꺼냅니다. */
        template <typename TClient, typename TOnResponse>
        uint64 reject( TClient& client, const uint16 errorCode, const TOnResponse& onResponse, const TDelegate& onDone )
        {
            return dispatch( client, onDone, [&]()
            { return client.rejectRequest( errorCode, onResponse ); } );
        }

        /** @brief 델리게이트를 꺼냅니다. 한 번만 꺼낼 수 있으며 없으면 false 입니다. `send` 안에서 바로 온 그 요청의 응답이면 보내는 중인 델리게이트를 꺼냅니다. */
        [[nodiscard]] bool take( uint64 requestID, TDelegate& outDelegate )
        {
            const auto delegateIt = _mapRequestToDelegate.find( requestID );
            if ( delegateIt != _mapRequestToDelegate.end() )
            {
                outDelegate = delegateIt->second;
                _mapRequestToDelegate.erase( delegateIt );
                return true;
            }
            const bool bTakeSendingDelegate = _sendingRequestID != 0 && requestID == _sendingRequestID && _bSendingTaken == SW_FALSE;
            if ( bTakeSendingDelegate )
            {
                outDelegate    = _sendingDelegate;
                _bSendingTaken = SW_TRUE;
                return true;
            }
            return false;
        }

        /** @brief 응답을 기다리는 호출 수입니다. */
        size_t getCount() const { return _mapRequestToDelegate.size(); }

    private:
        /** @brief @p issue 가 요청 id 를 받는 동안 즉시 온 응답이 @p onDone 을 꺼내 가게 하고, 꺼내 가지 않았으면 테이블에 넣습니다. */
        template <typename TClient, typename TIssue>
        uint64 dispatch( TClient& client, const TDelegate& onDone, const TIssue& issue )
        {
            // 응답 델리게이트 안에서 다시 보낼 수 있으므로, 바깥 보내기의 상태를 지켰다가 돌려놓는다.
            const TDelegate previousDelegate  = _sendingDelegate;
            const uint64    previousRequestID = _sendingRequestID;
            const uint8     bPreviousTaken    = _bSendingTaken;
            _sendingDelegate                  = onDone;
            // 보내는 중에 바로 온 응답은 이 id 일 때만 이 호출의 것이다. 다른 id 의 응답이 보내는 중인 델리게이트를 가져가지 않게 한다.
            _sendingRequestID      = client.peekNextRequestID();
            _bSendingTaken         = SW_FALSE;
            const uint64 requestID = issue();
            SW_ASSERT( requestID == _sendingRequestID );
            if ( _bSendingTaken == SW_FALSE )
                _mapRequestToDelegate.emplace( requestID, onDone );
            _sendingDelegate  = previousDelegate;
            _sendingRequestID = previousRequestID;
            _bSendingTaken    = bPreviousTaken;
            return requestID;
        }

        unordered_map<uint64, TDelegate> _mapRequestToDelegate{};
        TDelegate                        _sendingDelegate{};
        uint64                           _sendingRequestID{ 0 }; ///< 보내는 중인 요청의 id(0 = 보내는 중 아님)
        uint8                            _bSendingTaken{ SW_FALSE };
    };
} // namespace sw
