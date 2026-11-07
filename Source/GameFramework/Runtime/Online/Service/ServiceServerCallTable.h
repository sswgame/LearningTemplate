/**
 * @file ServiceServerCallTable.h
 * @brief 서버 서비스가 응답을 기다리는 요청을 두는 표입니다. 응답 경로(호스트 토큰 또는 델리게이트), 호출 id, 받은 시각, 메서드 번호와 표준 지표를 한 벌로 맡습니다.
 * @details 클라이언트 쪽 `ServiceClientCallTable` 의 짝이다. 저장소 작업으로 요청을 처리하는 서비스(경제 · GM · 우편함)는 요청을 `open` 으로 올리고, 작업의 `complete` 에서
 *          `finish` 로 응답한다. `finish` 는 표에서 빼고 응답한 뒤 `service_requests_total` · `service_request_seconds` 를 센다(`ServiceMetrics`).
 *
 *          - 요청 경로는 둘이다. 서비스 프레임워크의 요청은 `Origin::fromHost`(키트 메시지 목록 @p TMessageTypes 로 응답), 같은 프로세스와 테스트의 요청은 `Origin::fromDelegate` 이다.
 *          - 키트마다 다른 칸(영수증 표, GM 명령의 대상)은 @p TExtra 에 둔다. 표는 그 칸을 읽지 않는다.
 *          - 모르는 호출 id 의 완료는 경고 하나(`ServiceServerCallTable.UnknownCall`)로 같다. 종료 때 남은 호출은 `abandonAll` 이 오류 하나로 남기고 버린다.
 *          - 한 스레드(서비스 스레드)에서만 쓴다. 응답 델리게이트 안에서 다시 `open` 해도 된다 — `finish` 는 표에서 뺀 뒤 응답한다.
 *          gRPC 비동기 서버의 호출 태그 표, 언리얼 HTTP 서버의 요청 핸들과 같은 자리이다.
 *          대기 요청 하나를 받아 처리기에 넘기는 거래 · 계정 서버(`TradeServer`, `AccountServer`)는 응답 몸이 메서드마다 다르고 응답 전에 연결을 묶으므로 이 표를 쓰지 않는다.
 */
#pragma once
#include "Core/Container/IDAllocator.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Observability/ServiceMetrics.h"
#include "GameFramework/Runtime/Online/Service/OnlineServiceHost.h"

namespace sw
{
    class MetricRegistry;

    /** @brief 키트 칸이 없는 서비스의 대기 요청 칸입니다. */
    struct ServiceCallNoExtra
    {
    };

    /** @class ServiceServerCallTableBase @brief 표의 템플릿이 아닌 몫(지표, 서비스 이름, 호출 id, 로그)입니다. */
    class SW_GF_API ServiceServerCallTableBase
    {
    public:
        /** @brief 지표를 등록하고 로그에 쓸 서비스 이름을 받습니다. 인자는 `ServiceMetrics::initialize` 와 같습니다. 레지스트리가 null 이면 지표를 세지 않습니다. */
        template <typename TMethodName, typename TResult>
        void initialize( MetricRegistry* pRegistry, const utf8* pServiceName )
        {
            _serviceName = pServiceName;
            _metrics.initialize<TMethodName, TResult>( pRegistry, pServiceName );
        }

    protected:
        ServiceServerCallTableBase();

        uint64       allocateCallID() { return _callIDAllocator.allocate(); }
        void         warnUnknownCall( uint64 callID ) const;
        void         reportAbandoned( size_t callCount ) const;
        static int64 nowNanoseconds();

    protected:
        ServiceMetrics      _metrics;
        string              _serviceName;
        IDAllocator<uint64> _callIDAllocator;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ServiceServerCallTable
     * @brief 서비스 하나의 대기 요청 표입니다.
     * @tparam TReply        응답 메시지입니다. 결과 열거형 칸 `_result`(지표의 결과 번호)와 재시도 판정 `isRetryable()` 이 있어야 합니다.
     * @tparam TMessageTypes 호스트 경로의 응답을 쓰는 키트 메시지 목록입니다(`OnlineServiceHost::respondReply`).
     * @tparam TExtra        키트가 요청마다 더 드는 칸입니다. 기본 생성할 수 있어야 합니다.
     */
    template <typename TReply, typename TMessageTypes, typename TExtra = ServiceCallNoExtra>
    class ServiceServerCallTable final : public ServiceServerCallTableBase
    {
    public:
        using ReplyDelegate = Delegate<void( const TReply& )>;

        /** @brief 응답이 갈 곳입니다. 호스트가 있으면 토큰으로, 없으면 델리게이트로 응답합니다. */
        struct Origin
        {
            ReplyDelegate      _onReply{};
            NetRequestToken    _token{};
            OnlineServiceHost* _pHost{ nullptr };

            static Origin fromHost( OnlineServiceHost& host, const NetRequestToken& token )
            {
                Origin origin;
                origin._token = token;
                origin._pHost = &host;
                return origin;
            }

            static Origin fromDelegate( const ReplyDelegate& onReply )
            {
                Origin origin;
                origin._onReply = onReply;
                return origin;
            }

            void respond( const TReply& reply ) const
            {
                if ( _pHost != nullptr )
                    (void)_pHost->respondReply<TMessageTypes>( _token, reply ); // 다시 시도할 결과는 기억하지 않는다. 연결이 그사이 닫혔으면 호스트가 버린다
                else if ( _onReply.isBound() )
                    _onReply( reply );
            }
        };

        /** @brief 응답을 기다리는 요청 하나입니다. */
        struct Call
        {
            Origin _origin{};
            TExtra _extra{};
            uint64 _callID{ 0 };
            int64  _receivedNanoseconds{ 0 }; ///< 받은 시각(`MonotonicClock`)이다. 지연 지표가 쓴다
            int32  _methodIndex{ 0 };         ///< 메서드 번호(`XMethod::toIndex`)이다. 모르는 메서드는 음수이고 지표에 들지 않는다
        };

        /** @brief 요청 하나를 올리고 호출 id 를 반환합니다. 받은 시각은 지금입니다. 거절하는 요청도 올린 뒤 `finish` 로 끝내 지표에 들게 합니다. */
        uint64 open( const Origin& origin, int32 methodIndex, const TExtra& extra = TExtra{} )
        {
            Call call;
            call._origin              = origin;
            call._extra               = extra;
            call._callID              = allocateCallID();
            call._receivedNanoseconds = nowNanoseconds();
            call._methodIndex         = methodIndex;
            _listCall.push_back( call );
            return call._callID;
        }

        /** @brief 기다리는 호출을 찾습니다. 없으면 경고(`ServiceServerCallTable.UnknownCall`)를 남기고 null 입니다. 다음 `open` · `finish` 까지만 유효합니다. */
        Call* find( uint64 callID )
        {
            for ( Call& call : _listCall )
            {
                if ( call._callID == callID )
                    return &call;
            }
            warnUnknownCall( callID );
            return nullptr;
        }

        /** @brief @p predicate 가 참인 첫 호출을 찾습니다(키트 칸으로 찾기). 없으면 null 이고 로그를 남기지 않습니다. */
        template <typename TPredicate>
        Call* findIf( const TPredicate& predicate )
        {
            for ( Call& call : _listCall )
            {
                if ( predicate( call ) )
                    return &call;
            }
            return nullptr;
        }

        /**
         * @brief 호출을 표에서 빼고 @p reply 로 응답한 뒤 지표를 셉니다. 모르는 id 면 경고(`ServiceServerCallTable.UnknownCall`)를 남기고 false 입니다.
         * @details `reply.isRetryable()` 이면 요청 층이 응답을 기억하지 않아 같은 멱등 키의 재시도가 서비스에 다시 닿는다(`OnlineServiceHost::respondReply`).
         */
        bool finish( uint64 callID, const TReply& reply )
        {
            for ( size_t callIndex = 0; callIndex < _listCall.size(); ++callIndex )
            {
                if ( _listCall[callIndex]._callID != callID )
                    continue;
                const Call call = _listCall[callIndex];
                _listCall.erase( _listCall.begin() + static_cast<ptrdiff_t>( callIndex ) );
                call._origin.respond( reply );
                _metrics.observeRequest( call._methodIndex, static_cast<int32>( reply._result ), call._receivedNanoseconds );
                return true;
            }
            warnUnknownCall( callID );
            return false;
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
        vector<Call> _listCall{};
    };
} // namespace sw
