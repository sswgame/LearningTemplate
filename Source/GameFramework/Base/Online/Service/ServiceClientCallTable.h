/**
 * @file ServiceClientCallTable.h
 * @brief 클라이언트 쪽에서 요청 id 로 사용자 델리게이트를 찾는 표를 정의합니다.
 * @details 키트 클라이언트는 응답 공통 처리기(멤버 함수 델리게이트 하나)에서 요청마다 다른 사용자 델리게이트를 이 표로 찾는다.
 *
 *          - 한 스레드(`OnlineServiceClient::update` 을 부르는 스레드)에서만 쓴다.
 *          - 보내기는 `send` 로만 한다.
 *            `OnlineServiceClient::sendRequest` 는 그 자리에서 실패(초기화 전, 버전 불일치, 끊김)를 응답 델리게이트로 부를 수 있다.
 *            그래서 요청 id 를 받은 뒤에 표에 넣으면 그 응답이 사용자 델리게이트를 찾지 못한다.
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
 *             onFoo( response._errorCode, ... ); // _errorCode: OnlineError::kOk · 공통 · 키트 코드, 전송 실패는 kUnavailable
 *     }
 * @endcode
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"

namespace sw
{
    /**
     * @class ServiceClientCallTable
     * @brief 요청 id 와 델리게이트를 짝짓는 표입니다.
     * @details 클라이언트 · 몸 · 옵션 · 응답 델리게이트 타입은 `send` 의 템플릿 인자라 이 헤더는 `OnlineServiceClient.h` 를 끌어오지 않는다.
     *          @p TClient 는 `sendRequest( method, body, options, onResponse )` 와 `peekNextRequestID()` 를 가진다(`OnlineServiceClient`).
     *          대기 중인 호출은 버리는 API 가 없다 — 클라이언트는 요청마다 콜백을 정확히 한 번 부르므로(끊김 · 시한도 실패 응답) 표는 응답으로만 빈다.
     */
    template <typename TDelegate>
    class ServiceClientCallTable
    {
    public:
        /** @brief 요청을 보내고 @p onDone 을 그 요청 id 에 붙인 뒤 요청 id 를 반환합니다. 응답 처리기 @p onResponse 는 `take` 로 @p onDone 을 꺼냅니다. */
        template <typename TClient, typename TBody, typename TOptions, typename TOnResponse>
        uint64 send( TClient& client, uint16 method, const TBody& body, const TOptions& options, const TOnResponse& onResponse, const TDelegate& onDone )
        {
            // 응답 델리게이트 안에서 다시 보낼 수 있으므로, 바깥 보내기의 상태를 지켰다가 돌려놓는다.
            const TDelegate previousDelegate  = _sendingDelegate;
            const uint64    previousRequestID = _sendingRequestID;
            const uint8     bPreviousTaken    = _bSendingTaken;
            _sendingDelegate                  = onDone;
            // 보내는 중에 바로 온 응답은 이 id 일 때만 이 호출의 것이다 — 다른 id 의 응답이 보내는 중인 델리게이트를 가져가지 않게 한다.
            _sendingRequestID      = client.peekNextRequestID();
            _bSendingTaken         = SW_FALSE;
            const uint64 requestID = client.sendRequest( method, body, options, onResponse );
            SW_ASSERT( requestID == _sendingRequestID );
            if ( _bSendingTaken == SW_FALSE )
                _mapRequestToDelegate.emplace( requestID, onDone );
            _sendingDelegate  = previousDelegate;
            _sendingRequestID = previousRequestID;
            _bSendingTaken    = bPreviousTaken;
            return requestID;
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
        unordered_map<uint64, TDelegate> _mapRequestToDelegate{};
        TDelegate                        _sendingDelegate{};
        uint64                           _sendingRequestID{ 0 }; ///< 보내는 중인 요청의 id(0 = 보내는 중 아님)
        uint8                            _bSendingTaken{ SW_FALSE };
    };
} // namespace sw
