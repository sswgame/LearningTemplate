/**
 * @file OnlineProtocolService.h
 * @brief 프로토콜 표(`OnlineProtocolTable`)로 선 키트 서버의 공통 기반입니다. 메서드 번호 나누기, 본문 읽기, 멱등 키 검사, 지표, 응답 보존을 한 곳에서 맡습니다.
 * @details 키트는 `switch ( context._method )` 와 본문 읽기를 쓰지 않습니다. 표의 메서드마다 `handleCall( OnlineServerCall<XxxMethod::Claim> call, const OnlineCallContext&, const XxxRequest& )`
 *          오버로드를 두면 되고, 빠지거나 요청 타입이 틀리면 컴파일되지 않습니다. 응답은 나중에(저장소 완료에서) 같은 표지 `call` 로 합니다.
 *
 *          - 응답 타입이 표지의 메서드에서 나오므로 메서드에 맞지 않는 응답은 컴파일되지 않습니다.
 *          - 결과는 두 길입니다. 키트 업무 결과는 `respond`(응답 칸 `_result` — 끝난 결과이므로 요청 층이 기억합니다), 공통 범주는 `fail`(`OnlineError` — 재시도할 범주는 기억하지 않습니다,
 *            `OnlineServiceHost::respondError`). 어느 길인지가 보존을 정하므로 키트가 보존을 고를 일이 없습니다.
 *          - 키트 결과 열거형에는 공통 범주가 없다. 로직은 `OnlineServiceResult::fail` 로 내고 `finish` · `finishCall` 이 공통 오류로 끝낸다. 그래서 지표 · 보존이 형식에서 나온다.
 *            응답에 `isRetryable()` 이 있으면(공통 범주가 아니면서 같은 키로 다시 할 업무 결과, 스토어 대기) 그 응답은 기억하지 않습니다.
 *          - 완료 큐로 답하는 로직(`XxxService::takeCompletions`)은 호출 번호(`call._callID`)를 요청 태그로 받고, 바인딩이 `completeCall` 로 답합니다. 표에 없는 태그(내부 작업 · 끝난 호출)는 조용히 버립니다.
 *          - 모르는 메서드와 깨진 본문의 답은 모든 키트가 `kInvalidRequest` 로 같습니다. 멱등 키가 필수인 메서드(`RequiresIdempotencyKey`)에 키가 없어도 같습니다.
 *          - 지표는 `service_requests_total{result}` 에 키트 결과와 공통 범주를 함께 셉니다(`ServiceMetrics::initializeWithErrors`).
 *          - 응답은 한 번입니다. 호출은 표에 오른 뒤 `respond` · `fail` 로 빠지고, 모르는 표지는 경고 하나로 끝납니다(`ServiceServerCallTable.UnknownCall`).
 *          gRPC 비동기 서버의 메서드 표, Nakama `RegisterRpc` 의 자리입니다(기억에 의존).
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Service/ServiceServerCallTableBase.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStoreTracker.h"

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
     * @details 호출 번호로 색인합니다(같은 프로세스 표 `OnlineLocalCallTable` 과 같은 모양). 완료는 `isOpen` + `take` 로 두 번 찾으므로 선형 표면 대기 수 P 에 O(P) 둘이다.
     *          측정: `OnlineServerCallTableTest.CompletionCostByPending` · 지표 `service_store_pending_peak`.
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
            _mapCall.emplace( callID, Call{ token, &host, callID, nowNanoseconds(), methodIndex } );
            return callID;
        }

        /** @brief @p message 로 응답하고 @p resultIndex 로 셉니다. 끝난 결과이므로 요청 층이 기억합니다. 모르는 id 면 경고를 남기고 false 입니다. */
        template <typename TMessages, typename TMessage>
        bool respond( uint64 callID, const TMessage& message, int32 resultIndex )
        {
            Call call;
            if ( take( callID, call ) == false )
                return false;
            if constexpr ( OnlineReplyHasRetryCheck<TMessage>::value )
                (void)call._pHost->template respondReply<TMessages>( call._token, message );
            else
                (void)call._pHost->template respondMessage<TMessages>( call._token, message );
            _metrics.observeRequest( call._methodIndex, resultIndex, call._receivedNanoseconds );
            return true;
        }

        /**
         * @brief 이미 쓴 몸 @p body 로 응답하고 @p resultIndex 로 셉니다. 응답 전에 몸을 써 봐야 하는 조립(계정 — 못 쓰면 만든 세션을 지운다)만 씁니다. 모르는 id 면 경고를 남기고 false 입니다.
         * @param retention 키트의 재시도 판정입니다(`NetResponseRetention`).
         */
        bool respondBody( uint64 callID, const BitWriter& body, NetResponseRetention retention, int32 resultIndex )
        {
            Call call;
            if ( take( callID, call ) == false )
                return false;
            (void)call._pHost->respondOk( call._token, body, retention );
            _metrics.observeRequest( call._methodIndex, resultIndex, call._receivedNanoseconds );
            return true;
        }

        /**
         * @brief 공통 오류로 응답하고 그 범주로 셉니다. 다시 시도할 범주는 기억하지 않습니다(`OnlineServiceHost::respondError`). 모르는 id 면 경고를 남기고 false 입니다.
         * @param pDetail 공통 오류의 자세한 본문입니다(기다릴 시간, 상점 주소). 없으면 null 입니다.
         */
        bool fail( uint64 callID, uint16 errorCode, const BitWriter* pDetail = nullptr )
        {
            Call call;
            if ( take( callID, call ) == false )
                return false;
            (void)call._pHost->respondError( call._token, errorCode, pDetail );
            _metrics.observeError( call._methodIndex, errorCode, call._receivedNanoseconds );
            return true;
        }

        /** @brief 종료 때 남은 호출을 응답 없이 버립니다. 남은 것이 있으면 오류(`ServiceServerCallTable.AbandonedCalls`)를 남깁니다. */
        void abandonAll()
        {
            if ( _mapCall.empty() )
                return;
            reportAbandoned( _mapCall.size() );
            _mapCall.clear();
        }

        /** @brief 종료 때 남은 호출을 응답 없이 조용히 버립니다. 닫히는 연결의 답은 호스트도 버리는 조립이 씁니다. */
        void clear() { _mapCall.clear(); }

        /** @brief @p callID 가 아직 응답을 기다리면 true 입니다. 로그를 남기지 않습니다. */
        [[nodiscard]] bool isOpen( uint64 callID ) const { return _mapCall.find( callID ) != _mapCall.end(); }

        /** @brief 아직 응답을 기다리는 @p callID 의 메서드 번호(0부터, 열거자 순서)입니다. 없으면 비어 있고 로그를 남기지 않습니다. */
        [[nodiscard]] optional<int32> findMethodIndex( uint64 callID ) const
        {
            const auto found = _mapCall.find( callID );
            if ( found == _mapCall.end() )
                return nullopt;
            return found->second._methodIndex;
        }

        /** @brief 기다리는 호출 수를 지표 `service_store_pending` 에 씁니다. 서비스가 틱마다 부릅니다. */
        void publishPendingCount() { _metrics.setPendingCallCount( static_cast<int32>( _mapCall.size() ) ); }

        int32 getCount() const { return static_cast<int32>( _mapCall.size() ); }
        bool  isEmpty() const { return _mapCall.empty(); }

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
            const auto callIt = _mapCall.find( callID );
            if ( callIt == _mapCall.end() )
            {
                warnUnknownCall( callID );
                return false;
            }
            outCall = callIt->second;
            _mapCall.erase( callIt );
            return true;
        }

    private:
        unordered_map<uint64, Call> _mapCall{};
    };
} // namespace sw

namespace sw
{
    /** @brief 서비스 @p TService 에 `fillReply( TReply&, TCompletion& )` 가 있는지입니다. 클래스 밖에 두어 파생이 완성된 뒤(호출 자리)에 평가된다. */
    struct OnlineFillReplyTraits
    {
        template <typename TService, typename TReply, typename TCompletion, typename = void>
        struct HasFillReply : std::false_type
        {
        };

        template <typename TService, typename TReply, typename TCompletion>
        struct HasFillReply<TService, TReply, TCompletion, std::void_t<decltype( std::declval<TService&>().fillReply( std::declval<TReply&>(), std::declval<TCompletion&>() ) )>>
            : std::true_type
        {
        };
    };
} // namespace sw

namespace sw
{
    /**
     * @class OnlineLocalCallTable
     * @brief 호스트 없이 같은 프로세스에서 온 호출(서버 코드 · 테스트)의 표입니다. 응답은 델리게이트로 한 번 갑니다. 저장소 작업을 공유하는 키트(경제 · GM)가 씁니다.
     * @details 번호는 호스트 호출과 겹치지 않게 맨 위 비트를 켭니다. 그래서 완료는 번호만 보고 길을 가릅니다. 지표에 들지 않습니다. 응답 안에서 다시 올려도 됩니다.
     */
    template <typename TReply>
    class OnlineLocalCallTable
    {
    public:
        using ReplyDelegate = Delegate<void( const TReply& )>;

        static constexpr uint64 kLocalBit = 1ull << 63;

        [[nodiscard]] static bool isLocal( uint64 callID ) { return ( callID & kLocalBit ) != 0; }

        uint64 open( const ReplyDelegate& onReply )
        {
            const uint64 callID = kLocalBit | ++_lastID;
            _mapCall[callID]    = onReply;
            return callID;
        }

        /** @brief 호출을 표에서 빼고 @p reply 로 응답합니다. 모르는 번호면 false 입니다. */
        bool finish( uint64 callID, const TReply& reply )
        {
            const auto callIt = _mapCall.find( callID );
            if ( callIt == _mapCall.end() )
                return false;
            const ReplyDelegate onReply = callIt->second;
            _mapCall.erase( callIt );
            if ( onReply.isBound() )
                onReply( reply );
            return true;
        }

        void  clear() { _mapCall.clear(); }
        int32 getCount() const { return static_cast<int32>( _mapCall.size() ); }
        bool  isEmpty() const { return _mapCall.empty(); }

    private:
        unordered_map<uint64, ReplyDelegate> _mapCall{};
        uint64                               _lastID{ 0 };
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
            _pRequestHost = &host;
            RequestVisitor visitor{ *this, host, context, body };
            if ( TProtocol::Table::visitMethod( context._method, visitor ) == false )
                (void)host.respondError( context._token, OnlineError::kInvalidRequest ); // 메서드 라벨이 없어 지표에 들지 않는다
            _pRequestHost = nullptr;
        }

        /** @brief 키트 응답 @p reply 로 끝냅니다. 저장소 완료가 부릅니다. */
        template <typename TMethod>
        bool respond( OnlineServerCall<TMethod> call, const typename TMethod::Reply& reply )
        {
            return completeCall( call._callID, reply );
        }

        /**
         * @brief 호출 번호 @p callID 를 키트 응답 @p reply 로 끝냅니다. 로직의 완료 큐처럼 메서드를 모르는 자리가 씁니다(메서드마다 응답 형식이 같은 키트).
         * @details 표에 없는 번호(내부 작업의 태그, 이미 끝난 호출)는 조용히 false 입니다.
         */
        template <typename TReply>
        bool completeCall( uint64 callID, const TReply& reply )
        {
            if ( _calls.isOpen( callID ) == false )
                return false;
            return _calls.template respond<typename TProtocol::MessageTypes>( callID, reply, getResultIndex( reply ) );
        }

        /** @brief 호출 번호 @p callID 를 공통 범주 @p errorCode 로 끝냅니다. 완료 큐처럼 메서드를 모르는 자리가 씁니다. 표에 없는 번호는 조용히 false 입니다. */
        bool failCall( uint64 callID, uint16 errorCode )
        {
            if ( _calls.isOpen( callID ) == false )
                return false;
            return _calls.fail( callID, errorCode );
        }

        /**
         * @brief 호출 번호 @p callID 를 저장 함수의 결과 @p outcome 으로 끝냅니다. 공통 범주면 `failCall`, 아니면 @p reply 입니다. 완료 큐로 답하는 키트가 씁니다.
         * @details 키트 결과 열거형에 공통 범주를 두지 않는다 — 실패는 이 길로만 나가므로 보존 · 지표가 형식에서 나온다.
         */
        template <typename TResult, typename TReply>
        bool finishCall( uint64 callID, const OnlineServiceResult<TResult>& result, const TReply& reply )
        {
            return result.isFailed() ? failCall( callID, result._errorCode ) : completeCall( callID, reply );
        }

        /**
         * @brief 로직의 완료 @p completion 으로 호출 @p callID 를 끝냅니다. 응답 타입은 그 호출의 메서드(표지)에서 나옵니다.
         * @details 완료가 메서드를 모르는 로직(친구 · 길드처럼 완료 형식 하나로 답하는 키트)이 씁니다. 파생은 응답 타입마다 공개 `fillReply( XxxReply&, TCompletion& )` 를 두고,
         *          완료의 `_result` 는 `OnlineServiceResult` 이어야 합니다(공통 범주면 `failCall`). 그 메서드의 응답 타입에 맞는 `fillReply` 가 없으면 조립 실수이므로
         *          어서션을 내고 `kInternal` 로 끝냅니다. 표에 없는 번호(내부 작업 · 끝난 호출)는 조용히 false 입니다.
         */
        template <typename TCompletion>
        bool completeByMethod( uint64 callID, TCompletion& completion )
        {
            const optional<int32> foundMethod = _calls.findMethodIndex( callID );
            if ( foundMethod.has_value() == false )
                return false;
            const int32 methodIndex = *foundMethod;
            if ( completion._result.isFailed() )
                return failCall( callID, completion._result._errorCode );
            CompletionVisitor<TCompletion> visitor{ *this, completion, callID, SW_FALSE };
            (void)TProtocol::Table::visitMethod( static_cast<uint16>( TProtocol::Table::MethodTable::kFirstMethod + methodIndex ), visitor );
            return visitor._bDone == SW_TRUE;
        }

        /** @brief 열린 호출이 없고 저장소에 맡긴 일도 없으면 true 입니다. 종료 대기(`flushUntilIdle`)가 이것을 본다. */
        bool isIdle() const { return _calls.isEmpty() && _store.isIdle(); }
        /**
         * @brief 종료 드레인이 기다리는 일 수입니다 — 열린 호출 + 저장소에 맡긴 일(`IOnlineServerKit::getOpenJobCount`).
         * @details 저장소 왕복 중인 요청은 둘로 센다. 드레인은 0 이 되는지만 보고, 수는 로그에만 나온다.
         */
        int32 getOpenJobCount() const { return _calls.getCount() + _store.getOpenCount(); }

        /** @brief 공통 범주 @p errorCode 로 끝냅니다. 저장소 완료가 부릅니다. */
        template <typename TMethod>
        bool fail( OnlineServerCall<TMethod> call, uint16 errorCode )
        {
            return _calls.fail( call._callID, errorCode );
        }

        /** @brief 저장 함수의 결과로 끝냅니다. 공통 범주면 `fail`, 아니면 `respond` 입니다. */
        template <typename TMethod, typename TResult>
        bool finish( OnlineServerCall<TMethod> call, const OnlineServiceResult<TResult>& result, const typename TMethod::Reply& reply )
        {
            return result.isFailed() ? fail( call, result._errorCode ) : respond( call, reply );
        }

    protected:
        /**
         * @brief 요청마다 본문을 읽기 전에 불립니다. `kOk` 가 아닌 값을 돌려주면 그 공통 오류로 끝납니다(원격 기능 플래그 등). 기본은 모두 받습니다.
         */
        virtual uint16 checkRequest( OnlineServiceHost& host, const OnlineCallContext& context )
        {
            (void)host;
            (void)context;
            return OnlineError::kOk;
        }

        /** @brief 요청을 받을지 정합니다. 받지 않는 동안(로직을 잇기 전 · 내린 뒤) 모든 요청이 `kUnavailable` 로 끝납니다. 처음에는 받습니다. */
        void setReady( bool bReady ) { _bReady = bReady ? SW_TRUE : SW_FALSE; }
        /** @brief 지금 처리하는 요청의 호스트입니다. `handleCall` 안에서만 있습니다. */
        OnlineServiceHost* getRequestHost() const { return _pRequestHost; }

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

        template <typename TCompletion>
        struct CompletionVisitor
        {
            OnlineProtocolService& _service;
            TCompletion&           _completion;
            uint64                 _callID;
            uint8                  _bDone : 1; ///< 응답했다

            template <typename TMethod>
            void visit()
            {
                using Reply = typename TMethod::Reply;
                // 파생에 이 응답의 `fillReply` 가 있는지는 여기(파생이 완성된 뒤)에서 본다 — 클래스 본문 안의 특성은 미완성 파생을 들여다봤다
                if constexpr ( OnlineFillReplyTraits::HasFillReply<TService, Reply, TCompletion>::value )
                {
                    Reply reply{};
                    static_cast<TService&>( _service ).fillReply( reply, _completion );
                    _bDone = _service.completeCall( _callID, reply ) ? SW_TRUE : SW_FALSE;
                }
                else
                {
                    SW_ASSERT( false ); // 이 완료 형식으로 답할 수 없는 메서드이다(조립 실수)
                    _bDone = _service.failCall( _callID, OnlineError::kInternal ) ? SW_TRUE : SW_FALSE;
                }
            }
        };

        template <typename TMethod>
        void receive( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
        {
            const OnlineServerCall<TMethod> call{ _calls.open( host, context._token, TMethod::kIndex ) };
            if ( _bReady == SW_FALSE )
            {
                (void)fail( call, OnlineError::kUnavailable );
                return;
            }
            const uint16 checkError = checkRequest( host, context );
            if ( checkError != OnlineError::kOk )
            {
                (void)fail( call, checkError );
                return;
            }
            typename TMethod::Request request{};
            const bool                bBodyRead = TProtocol::MessageTypes::read( body, request ) == FixedReadResult::Ok;
            const bool                bKeyOk    = TMethod::kRequiresIdempotencyKey == false || context._idempotencyKey.isValid();
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
        ServiceStoreTracker   _store{}; ///< 저장소에 맡긴 일의 앞이다. 파생의 `initialize` 가 `bindStore` 로 잇고, 종료가 `flushUntilIdle` 로 기다린다

    private:
        OnlineServiceHost* _pRequestHost{ nullptr };
        uint8              _bReady{ SW_TRUE };
    };
} // namespace sw
