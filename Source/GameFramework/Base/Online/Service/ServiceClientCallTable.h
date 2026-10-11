/**
 * @file ServiceClientCallTable.h
 * @brief 클라이언트 쪽에서 요청 id 로 사용자 델리게이트를 찾는 표를 정의합니다.
 * @details 키트 클라이언트는 응답 공통 처리기(멤버 함수 델리게이트 하나)에서 요청마다 다른 사용자 델리게이트를 이 표로 찾는다.
 *
 *          - 한 스레드(`OnlineServiceClient::tick` 을 부르는 스레드)에서만 쓴다.
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

#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"

namespace sw
{
    /**
     * @class ServiceClientCallTable
     * @brief 요청 id 와 델리게이트를 짝짓는 표입니다.
     */
    template <typename TDelegate>
    class ServiceClientCallTable
    {
    public:
        /** @brief 요청을 보내고 @p onDone 을 그 요청 id 에 붙인 뒤 요청 id 를 반환합니다. 응답 처리기 @p onResponse 는 `take` 로 @p onDone 을 꺼냅니다. */
        uint64 send( OnlineServiceClient& client, uint16 method, const BitWriter& body, const NetRequestOptions& options, const OnlineResponseDelegate& onResponse,
                     const TDelegate& onDone )
        {
            // 응답 델리게이트 안에서 다시 보낼 수 있으므로, 바깥 보내기의 상태를 지켰다가 돌려놓는다.
            const TDelegate previousDelegate = _sendingDelegate;
            const uint8     bPreviousSending = _bSending;
            const uint8     bPreviousTaken   = _bSendingTaken;
            _sendingDelegate                 = onDone;
            _bSending                        = SW_TRUE;
            _bSendingTaken                   = SW_FALSE;
            const uint64 requestID           = client.sendRequest( method, body, options, onResponse );
            if ( _bSendingTaken == SW_FALSE )
                _mapRequestToDelegate.emplace( requestID, onDone );
            _sendingDelegate = previousDelegate;
            _bSending        = bPreviousSending;
            _bSendingTaken   = bPreviousTaken;
            return requestID;
        }

        /** @brief 델리게이트를 꺼냅니다. 한 번만 꺼낼 수 있으며 없으면 false 입니다. `send` 안에서 바로 온 응답이면 보내는 중인 델리게이트를 꺼냅니다. */
        [[nodiscard]] bool take( uint64 requestID, TDelegate& outDelegate )
        {
            const auto delegateIt = _mapRequestToDelegate.find( requestID );
            if ( delegateIt != _mapRequestToDelegate.end() )
            {
                outDelegate = delegateIt->second;
                _mapRequestToDelegate.erase( delegateIt );
                return true;
            }
            if ( _bSending == SW_TRUE && _bSendingTaken == SW_FALSE )
            {
                outDelegate    = _sendingDelegate;
                _bSendingTaken = SW_TRUE;
                return true;
            }
            return false;
        }

        void   clear() { _mapRequestToDelegate.clear(); }
        size_t getCount() const { return _mapRequestToDelegate.size(); }

    private:
        unordered_map<uint64, TDelegate> _mapRequestToDelegate{};
        TDelegate                        _sendingDelegate{};
        uint8                            _bSending{ SW_FALSE };
        uint8                            _bSendingTaken{ SW_FALSE };
    };
} // namespace sw
