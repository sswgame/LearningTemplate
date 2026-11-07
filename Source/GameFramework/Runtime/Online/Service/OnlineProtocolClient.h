/**
 * @file OnlineProtocolClient.h
 * @brief 프로토콜 표(`OnlineProtocolTable`)로 선 키트 클라이언트의 공통 기반입니다. `call<XxxMethod::Claim>( request, onReply )` 하나가 키트마다 손으로 쓰던 요청 함수를 대신합니다.
 * @details - 요청 타입과 응답 타입이 메서드 서술자에서 나오므로 메서드에 맞지 않는 요청은 컴파일되지 않습니다.
 *          - 결과는 두 층입니다. 공통 범주(전송 · 인증 · 과부하 · 일시 장애 · 버전)는 `_errorCode`(`OnlineError`), 키트 업무 결과는 응답의 `_result` 입니다.
 *            공통 범주 표(`XxxProtocol::kArrCommonResult`)가 있는 키트는 `_errorCode` 가 0 이 아닐 때 `_result` 를 그 줄의 값으로 채웁니다. 표에 없는 코드는 `kUnavailable` 줄의 값입니다.
 *            재시도 여부는 `isRetryable` 이 범주에서 끌어내므로 키트가 고를 일이 없습니다.
 *          - 멱등 키가 필수인 메서드(`RequiresIdempotencyKey`)는 키를 넘기지 않으면 새로 만듭니다. 재시도할 때는 결과의 `_idempotencyKey` 를 다시 넘깁니다.
 *          - 캐시를 고치는 키트는 `onCallSucceeded( XxxMethod::Claim, request, reply )` 를 메서드마다 오버로드합니다. 콜백보다 먼저 불립니다. 오버로드가 없는 메서드는 기본 템플릿이 받습니다
 *            (파생은 `using Base::onCallSucceeded;` 를 적습니다). 알림은 `onPush( XxxMethod::NewMail, body )` 오버로드이며, 표에 적은 알림마다 필수라 빠지면 컴파일되지 않습니다.
 *          - 콜백은 요청마다 정확히 한 번 불립니다. 보내지 못한 요청(연결 전, 범위 밖 값, 멈춘 클라이언트)은 `call` 안에서 바로, 나머지는 `OnlineServiceClient::update` 를 부르는 스레드에서 불립니다.
 *          gRPC 생성 stub, 언리얼 Online Services 의 `TOnlineAsyncOpHandle<FOp>` 에 해당합니다(기억에 의존).
 */
#pragma once
#include "Core/Container/IDAllocator.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/Runtime/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Runtime/Online/Service/ServiceClientCallTable.h"

#include <tuple>

namespace sw
{
    /** @brief 메서드 하나의 끝난 호출입니다. @p TReply 는 메서드 서술자의 응답 타입입니다. */
    template <typename TReply>
    struct OnlineResult
    {
        TReply            _reply{};          ///< `_errorCode` 가 `kOk` 일 때만 서버가 보낸 값이다
        NetIdempotencyKey _idempotencyKey{}; ///< 재시도할 때 그대로 다시 넘기는 키이다. 키 없이 보낸 요청이면 비어 있다
        uint64            _requestID{ 0 };   ///< `call` 이 반환한 번호이다
        uint16            _method{ 0 };      ///< 서술자의 번호(`kID`)이다. 메서드를 실행 때 고르는 도구(GM)가 응답을 가른다
        uint16            _errorCode{ OnlineError::kOk };

        /** @brief 공통 오류 없이 답을 받았고 키트 결과(`_result` 가 있는 응답)가 `Ok` 이면 true 입니다. */
        [[nodiscard]] bool isOk() const
        {
            if ( _errorCode != OnlineError::kOk )
                return false;
            if constexpr ( OnlineReplyHasResult<TReply>::value )
                return _reply._result == decltype( _reply._result )::Ok;
            else
                return true;
        }
        /** @brief 같은 멱등 키로 다시 시도할 결과인지입니다. 범주에서 나오므로 키트마다 판정하지 않습니다. */
        [[nodiscard]] bool isRetryable() const { return OnlineError::isRetryable( _errorCode ); }
        /** @brief 클라이언트를 업데이트해야 하는 결과인지입니다. 다시 시도해도 소용이 없습니다. */
        [[nodiscard]] bool isUpdateRequired() const { return _errorCode == OnlineError::kVersionMismatch || _errorCode == OnlineError::kUpdateRequired; }
    };

    /** @brief 메서드 @p TMethod 의 응답을 받는 델리게이트입니다. */
    template <typename TMethod>
    using OnlineReplyDelegate = Delegate<void( const OnlineResult<typename TMethod::Reply>& )>;
} // namespace sw

namespace sw
{
    /**
     * @class OnlineProtocolClient
     * @brief 프로토콜 표로 선 키트 클라이언트의 공통 기반입니다.
     * @tparam TClient 파생 클라이언트(응답 · 알림 훅을 정적으로 부른다)
     * @tparam TProtocol 표 `Table`, 메시지 목록 `MessageTypes`, 등록 항목 `kOnlineEntry` 를 가진 키트 프로토콜 구조체
     */
    template <typename TClient, typename TProtocol>
    class OnlineProtocolClient : public IOnlineClientService
    {
    public:
        OnlineProtocolClient()
            : _tables{}
            , _pClient{ nullptr }
            , _requestIDAllocator{}
        {
        }

        /** @brief @p pClient 는 빌려 씁니다. `registerClientService( this )` 는 호출하는 쪽이 부릅니다(`OnlineSession` 이 맡습니다). */
        void initialize( OnlineServiceClient* pClient ) { _pClient = pClient; }

        /**
         * @brief @p request 를 보내고 요청 번호를 반환합니다.
         * @param key 재시도면 앞 결과의 `_idempotencyKey` 입니다. 비어 있고 메서드가 키를 요구하면 새로 만듭니다.
         */
        template <typename TMethod>
        uint64 call( const typename TMethod::Request& request, const OnlineReplyDelegate<TMethod>& onReply, const NetIdempotencyKey& key = NetIdempotencyKey{} )
        {
            const NetIdempotencyKey usedKey = TMethod::kRequiresIdempotencyKey && key.isValid() == false ? NetIdempotencyKey::makeRandom() : key;
            if ( _pClient == nullptr )
                return failCall<TMethod>( onReply, usedKey, OnlineError::kUnavailable );
            const PendingCall<TMethod> pending{ onReply, request, usedKey, _requestIDAllocator.allocate() };
            NetRequestOptions          options;
            options._idempotencyKey = usedKey;
            (void)getTable<TMethod>().template sendMessage<typename TProtocol::MessageTypes>( *_pClient, TMethod::kID, request, options,
                                                                                              OnlineResponseDelegate::create<&OnlineProtocolClient::template onResponse<TMethod>>( this ), pending );
            return pending._requestID; // 응답은 pending._requestID 로 알린다
        }

        /** @brief 본문이 없는 요청입니다. */
        template <typename TMethod>
        uint64 call( const OnlineReplyDelegate<TMethod>& onReply, const NetIdempotencyKey& key = NetIdempotencyKey{} )
        {
            return call<TMethod>( typename TMethod::Request{}, onReply, key );
        }

        // IOnlineClientService
        const OnlineProtocolEntry& getProtocol() const final { return TProtocol::kOnlineEntry; }
        void                       onServicePush( uint16 kind, BitReader& body ) final
        {
            PushVisitor visitor{ *static_cast<TClient*>( this ), body };
            (void)TProtocol::Table::visitPush( kind, visitor );
        }

    protected:
        /** @brief 성공 응답(`OnlineResult::isOk`)마다 콜백 전에 불립니다. 캐시를 고치는 메서드만 파생이 오버로드합니다. */
        template <typename TMethod>
        void onCallSucceeded( TMethod, const typename TMethod::Request& request, const typename TMethod::Reply& reply )
        {
            (void)request;
            (void)reply;
        }

    private:
        /** @brief 응답을 기다리는 호출 하나입니다. 응답이 올 때 캐시 훅이 요청을 보도록 요청을 함께 둡니다. */
        template <typename TMethod>
        struct PendingCall
        {
            OnlineReplyDelegate<TMethod> _onReply{};
            typename TMethod::Request    _request{};
            NetIdempotencyKey            _key{};
            uint64                       _requestID{ 0 };
        };

        template <typename TMethod>
        using PendingTable = ServiceClientCallTable<PendingCall<TMethod>>;
        using TableTuple   = typename TProtocol::Table::template MethodTuple<PendingTable>;

        struct PushVisitor
        {
            TClient&   _client;
            BitReader& _body;

            template <typename TPush>
            void visit()
            {
                typename TPush::Body body{};
                if ( TProtocol::MessageTypes::read( _body, body ) == FixedReadResult::Ok )
                    _client.onPush( TPush{}, body );
            }
        };

        template <typename TMethod>
        PendingTable<TMethod>& getTable()
        {
            return std::get<PendingTable<TMethod>>( _tables );
        }

        /** @brief 서비스 클라이언트 없이(`initialize` 전) 부른 요청을 바로 콜백합니다. */
        template <typename TMethod>
        uint64 failCall( const OnlineReplyDelegate<TMethod>& onReply, const NetIdempotencyKey& key, uint16 errorCode )
        {
            OnlineResult<typename TMethod::Reply> result;
            result._requestID      = _requestIDAllocator.allocate();
            result._method         = TMethod::kID;
            result._idempotencyKey = key;
            result._errorCode      = errorCode;
            markCommonResult( result );
            if ( onReply.isBound() )
                onReply( result );
            return result._requestID;
        }

        /** @brief 공통 범주로 끝난 호출의 키트 결과를 표에서 채웁니다. 화면 · 모델은 결과 하나로 문구를 고른다. */
        template <typename TReply>
        static void markCommonResult( OnlineResult<TReply>& inoutResult )
        {
            if constexpr ( OnlineProtocolHasCommonResult<TProtocol>::value && OnlineReplyHasResult<TReply>::value )
            {
                using TResult = decltype( inoutResult._reply._result );
                if ( inoutResult._errorCode == OnlineError::kOk )
                    return;
                const TResult unavailable  = OnlineError::toResult( OnlineError::kUnavailable, TProtocol::kArrCommonResult, TResult::Ok );
                inoutResult._reply._result = OnlineError::toResult( inoutResult._errorCode, TProtocol::kArrCommonResult, unavailable );
            }
            else
            {
                (void)inoutResult;
            }
        }

        template <typename TMethod>
        void onResponse( const OnlineResponse& response )
        {
            using Reply = typename TMethod::Reply;
            PendingCall<TMethod> pending;
            if ( getTable<TMethod>().take( response._requestID, pending ) == false )
                return;
            OnlineResult<Reply> result;
            result._requestID      = pending._requestID;
            result._method         = TMethod::kID;
            result._idempotencyKey = pending._key;
            result._errorCode      = response._errorCode;
            if ( response.isOk() )
            {
                BitReader body( response._pBody, response._bodySize );
                if ( TProtocol::MessageTypes::read( body, result._reply ) != FixedReadResult::Ok )
                {
                    result._reply     = Reply{};
                    result._errorCode = OnlineError::kUnavailable; // 같은 스키마 해시인데 못 읽었다. 키트 결과로 꾸미지 않고 일시 장애로 알린다
                }
            }
            markCommonResult( result );
            if ( result.isOk() )
                static_cast<TClient*>( this )->onCallSucceeded( TMethod{}, pending._request, result._reply );
            if ( pending._onReply.isBound() )
                pending._onReply( result );
        }

    private:
        TableTuple           _tables;
        OnlineServiceClient* _pClient;
        IDAllocator<uint64>  _requestIDAllocator;
    };
} // namespace sw
