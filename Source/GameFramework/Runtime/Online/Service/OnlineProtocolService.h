/**
 * @file OnlineProtocolService.h
 * @brief 프로토콜 표(`OnlineProtocolTable`)로 선 키트 서버의 공통 기반입니다. 메서드 번호 나누기, 본문 읽기, 멱등 키 검사, 지표, 응답 보존을 한 곳에서 맡습니다.
 * @details 키트는 `switch ( context._method )` 와 본문 읽기를 쓰지 않습니다. 표의 메서드마다 `handleCall( OnlineServerCall<XxxMethod::Claim> call, const OnlineCallContext&, const XxxRequest& )`
 *          오버로드를 두면 되고, 빠지거나 요청 타입이 틀리면 컴파일되지 않습니다. 응답은 나중에(저장소 완료에서) 같은 표지 `call` 로 합니다.
 *
 *          - 응답 타입이 표지의 메서드에서 나오므로 메서드에 맞지 않는 응답은 컴파일되지 않습니다.
 *          - 결과는 두 길입니다. 키트 업무 결과는 `respond`(응답 칸 `_result` — 끝난 결과이므로 요청 층이 기억합니다), 공통 범주는 `fail`(`OnlineError` — 재시도할 범주는 기억하지 않습니다,
 *            `OnlineServiceHost::respondError`). 어느 길인지가 보존을 정하므로 키트가 보존을 고를 일이 없습니다.
 *          - 모르는 메서드와 깨진 본문의 답은 모든 키트가 `kInvalidRequest` 로 같습니다. 멱등 키가 필수인 메서드(`RequiresIdempotencyKey`)에 키가 없어도 같습니다.
 *          - 지표는 `service_requests_total{result}` 에 키트 결과와 공통 범주를 함께 셉니다(`ServiceMetrics::initializeWithErrors`).
 *          - 응답은 한 번입니다. 호출은 표에 오른 뒤 `respond` · `fail` 로 빠지고, 모르는 표지는 경고 하나로 끝납니다(`ServiceServerCallTable.UnknownCall`).
 *          gRPC 비동기 서버의 메서드 표, Nakama `RegisterRpc` 의 자리입니다(기억에 의존).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/Runtime/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Runtime/Online/Service/ServiceServerCallTable.h"

namespace sw
{
    /** @brief 응답을 기다리는 호출의 표지입니다. 메서드가 타입에 있어 그 메서드의 응답만 받습니다. 값으로 복사해 저장소 작업에 넘깁니다. */
    template <typename TMethod>
    struct OnlineServerCall
    {
        uint64 _callID{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class OnlineServerCallTable
     * @brief 응답을 기다리는 호출(토큰, 받은 시각, 메서드 번호)의 표입니다. 응답 타입을 모르며, 타입 있는 앞은 `OnlineProtocolService` 입니다.
     */
    class OnlineServerCallTable final : public ServiceServerCallTableBase
    {
    public:
        /** @brief 지표를 등록합니다. 결과 라벨은 @p TResult 의 열거자와 공통 범주입니다. 레지스트리가 null 이면 세지 않습니다. */
        template <typename TMethodName, typename TResult>
        void initializeWithErrors( MetricRegistry* pRegistry, const utf8* pServiceName )
        {
            _serviceName = pServiceName;
            _metrics.initializeWithErrors<TMethodName, TResult>( pRegistry, pServiceName );
        }

        uint64 open( OnlineServiceHost& host, const NetRequestToken& token, int32 methodIndex )
        {
            const uint64 callID = allocateCallID();
            _listCall.push_back( Call{ token, &host, callID, nowNanoseconds(), methodIndex } );
            return callID;
        }

        /** @brief @p message 로 응답하고 @p resultIndex 로 셉니다. 끝난 결과이므로 요청 층이 기억합니다. 모르는 id 면 경고를 남기고 false 입니다. */
        template <typename TMessages, typename TMessage>
        bool respond( uint64 callID, const TMessage& message, int32 resultIndex )
        {
            Call call;
            if ( take( callID, call ) == false )
                return false;
            (void)call._pHost->template respondMessage<TMessages>( call._token, message );
            _metrics.observeRequest( call._methodIndex, resultIndex, call._receivedNanoseconds );
            return true;
        }

        /** @brief 공통 오류로 응답하고 그 범주로 셉니다. 다시 시도할 범주는 기억하지 않습니다(`OnlineServiceHost::respondError`). 모르는 id 면 경고를 남기고 false 입니다. */
        bool fail( uint64 callID, uint16 errorCode )
        {
            Call call;
            if ( take( callID, call ) == false )
                return false;
            (void)call._pHost->respondError( call._token, errorCode );
            _metrics.observeError( call._methodIndex, errorCode, call._receivedNanoseconds );
            return true;
        }

        /** @brief 종료 때 남은 호출을 응답 없이 버립니다. 남은 것이 있으면 오류(`ServiceServerCallTable.AbandonedCalls`)를 남깁니다. */
        void abandonAll()
        {
            if ( _listCall.empty() )
                return;
            reportAbandoned( _listCall.size() );
            _listCall.clear();
        }

        /** @brief 기다리는 호출 수를 지표 `service_store_pending` 에 씁니다. 서비스가 틱마다 부릅니다. */
        void publishPendingCount() { _metrics.setPendingStoreWorkCount( static_cast<int32>( _listCall.size() ) ); }

        int32 getCount() const { return static_cast<int32>( _listCall.size() ); }
        bool  isEmpty() const { return _listCall.empty(); }

    private:
        struct Call
        {
            NetRequestToken    _token{};
            OnlineServiceHost* _pHost{ nullptr };
            uint64             _callID{ 0 };
            int64              _receivedNanoseconds{ 0 };
            int32              _methodIndex{ 0 };
        };

        [[nodiscard]] bool take( uint64 callID, Call& outCall )
        {
            for ( size_t callIndex = 0; callIndex < _listCall.size(); ++callIndex )
            {
                if ( _listCall[callIndex]._callID != callID )
                    continue;
                outCall = _listCall[callIndex];
                _listCall.erase( _listCall.begin() + static_cast<ptrdiff_t>( callIndex ) );
                return true;
            }
            warnUnknownCall( callID );
            return false;
        }

    private:
        vector<Call> _listCall{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class OnlineProtocolService
     * @brief 프로토콜 표로 선 키트 서버의 공통 기반입니다. 호스트 콜백은 `IOnlineServerService` 그대로이고, 요청 받기만 이 기반이 닫습니다.
     * @tparam TService 파생 서비스. 표의 메서드마다 `handleCall` 오버로드를 가집니다
     * @tparam TProtocol 표 `Table`, 메시지 목록 `MessageTypes`, 결과 열거형 `ResultName`, 등록 항목 `kOnlineEntry` 를 가진 키트 프로토콜 구조체
     */
    template <typename TService, typename TProtocol>
    class OnlineProtocolService : public IOnlineServerService
    {
    public:
        // IOnlineServerService
        const OnlineProtocolEntry& getProtocol() const final { return TProtocol::kOnlineEntry; }
        bool                       isAnonymousMethod( uint16 method ) const final { return TProtocol::Table::isAnonymous( method ); }
        void                       onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) final
        {
            RequestVisitor visitor{ *this, host, context, body };
            if ( TProtocol::Table::visitMethod( context._method, visitor ) == false )
                (void)host.respondError( context._token, OnlineError::kInvalidRequest ); // 메서드 라벨이 없어 지표에 들지 않는다
        }

        /** @brief 키트 응답 @p reply 로 끝냅니다. 저장소 완료가 부릅니다. */
        template <typename TMethod>
        bool respond( OnlineServerCall<TMethod> call, const typename TMethod::Reply& reply )
        {
            return _calls.template respond<typename TProtocol::MessageTypes>( call._callID, reply, getResultIndex( reply ) );
        }

        /** @brief 공통 범주 @p errorCode 로 끝냅니다. 저장소 완료가 부릅니다. */
        template <typename TMethod>
        bool fail( OnlineServerCall<TMethod> call, uint16 errorCode )
        {
            return _calls.fail( call._callID, errorCode );
        }

        /** @brief 저장 함수의 결과로 끝냅니다. 공통 범주면 `fail`, 아니면 `respond` 입니다. */
        template <typename TMethod, typename TResult>
        bool finish( OnlineServerCall<TMethod> call, const OnlineOutcome<TResult>& outcome, const typename TMethod::Reply& reply )
        {
            return outcome.isFailed() ? fail( call, outcome._errorCode ) : respond( call, reply );
        }

    protected:
        /** @brief 호출 표의 지표를 등록합니다. 파생의 `initialize` 가 부릅니다. */
        void initializeCalls( MetricRegistry* pRegistry, const utf8* pServiceName )
        {
            _calls.template initializeWithErrors<typename TProtocol::Table::MethodTable::MethodName, typename TProtocol::ResultName>( pRegistry, pServiceName );
        }

    private:
        struct RequestVisitor
        {
            OnlineProtocolService&   _service;
            OnlineServiceHost&       _host;
            const OnlineCallContext& _context;
            BitReader&               _body;

            template <typename TMethod>
            void visit()
            {
                _service.template receive<TMethod>( _host, _context, _body );
            }
        };

        template <typename TMethod>
        void receive( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
        {
            const OnlineServerCall<TMethod> call{ _calls.open( host, context._token, TMethod::kIndex ) };
            typename TMethod::Request       request{};
            const bool                      bBodyRead = TProtocol::MessageTypes::read( body, request ) == FixedReadResult::Ok;
            const bool                      bKeyOk    = TMethod::kRequiresIdempotencyKey == false || context._idempotencyKey.isValid();
            if ( bBodyRead == false || bKeyOk == false )
            {
                (void)fail( call, OnlineError::kInvalidRequest );
                return;
            }
            static_cast<TService*>( this )->handleCall( call, context, request );
        }

        /** @brief 지표의 결과 번호입니다. 결과 칸이 없는 응답은 끝났으면 언제나 `Ok`(0)입니다. */
        template <typename TReply>
        static int32 getResultIndex( const TReply& reply )
        {
            if constexpr ( OnlineReplyHasResult<TReply>::value )
                return static_cast<int32>( reply._result );
            else
            {
                (void)reply;
                return 0;
            }
        }

    protected:
        OnlineServerCallTable _calls{};
    };
} // namespace sw
