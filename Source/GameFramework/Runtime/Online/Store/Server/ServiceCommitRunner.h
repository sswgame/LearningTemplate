/**
 * @file ServiceCommitRunner.h
 * @brief 저장소 커밋의 유일한 길입니다 — 시도 함수를 받아 버전 충돌이면 다시 부르는 실행기와, 시도 하나가 쥐는 커밋할 수 있는 연결입니다.
 * @details 서비스가 "읽고, 판단하고, 커밋하고, 충돌이면 다시" 를 손 `for` 로 되풀이하면 상한 · 다 썼을 때의 답 · 로그가 자리마다 달라진다(로그인 재접속은 멀쩡한 세션을 InvalidToken 으로
 *          끊었고, 거래 복구는 닫지 못한 거래를 Ok 로 넘겼다). 그래서 연결의 `commit` 은 보호이고, 부를 수 있는 것은 이 실행기가 만든 `ServiceCommitAttempt` 와 `commitOnce` 뿐이다.
 *          Nakama `ExecuteInTx`(40xxx 오류면 5 번까지 클로저를 다시 부르고 마지막 오류를 돌려준다), Spanner `ReadWriteTransaction`(ABORTED 면 함수를 다시 부르고, 함수가 받는 트랜잭션에는 Commit 이 없다)과
 *          같은 모양이다. 시도 사이에 자지 않는다(Nakama 와 같다) — 저장소 스레드를 세우면 다른 일이 기다린다. 간격을 두는 다시 하기는 클라이언트의 `kBusy` 백오프가 맡는다.
 *
 *          규칙은 다음과 같다.
 *          - 판단에 쓰는 읽기는 시도 함수 안에서 한다. 시도마다 새 읽기 집합(`ServiceReadSetConnection`)이라 읽은 것이 모두 그 시도의 커밋 조건이다.
 *          - 시도 함수는 `optional<T>` 를 돌려준다 — 값이면 끝난 답이고, 비면 "다시"(충돌)다.
 *          - 다 썼을 때의 답은 하나다: 서비스 결과는 `OnlineError::kBusy`(커밋되지 않았으므로 멱등 키 없이도 다시 해도 된다), 저장소 · 원장 결과는 `Conflict`. 경고 로그는 실행기가 남긴다.
 *          - 적용됐는지 모르는 커밋(`Unavailable`)은 다시 하지 않는다. 멱등 기록이 있으면 `ServiceIdempotency::commit` · `Ledger::commitTransfer` 가 기록을 읽어 가린다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Service/OnlineLimit.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceReadSetConnection.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

#include <utility>

namespace sw
{
    /**
     * @class ServiceCommitAttempt
     * @brief 시도 하나의 연결입니다. 읽기는 그 시도의 커밋 조건이 되고, 커밋할 수 있다. `ServiceCommitRunner` 만 만든다.
     */
    class SW_GF_API ServiceCommitAttempt final : public IServiceStoreConnection
    {
    public:
        [[nodiscard]] ServiceStoreResult readRecord( const hashed_string& table, string_view key, ServiceRecord& outRecord ) override { return _readSet.readRecord( table, key, outRecord ); }
        /** @brief 읽은 레코드를 조건으로 붙여 커밋합니다. `Conflict` 면 시도 함수는 빈 값을 돌려주어 다시 불리거나, 어긋난 쓰기 번호로 업무 답을 정합니다. */
        [[nodiscard]] ServiceStoreResult commit( const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo = nullptr ) override;

    private:
        friend struct ServiceCommitRunner;

        explicit ServiceCommitAttempt( IServiceStoreConnection& connection )
            : _readSet{ connection }
        {
        }

        [[nodiscard]] ServiceStoreResult listRecords( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending,
                                                      vector<ServiceRecord>& outListRecord ) override;

    private:
        ServiceReadSetConnection _readSet;
    };

    /** @brief 충돌이 상한까지 이어졌을 때의 답입니다. 서비스 결과(`OnlineServiceResult`)는 `kBusy` 다. 다른 결과 형식은 특수화한다. */
    template <typename T>
    struct ServiceConflictAnswer
    {
        [[nodiscard]] static T make() { return T::fail( OnlineError::kBusy ); }
    };

    template <>
    struct ServiceConflictAnswer<ServiceStoreResult>
    {
        [[nodiscard]] static ServiceStoreResult make() { return ServiceStoreResult::Conflict; }
    };

    /** @brief 커밋 실행기입니다(전부 static, 저장소 스레드에서 부른다). */
    struct SW_GF_API ServiceCommitRunner
    {
        /**
         * @brief @p attemptFunction( `ServiceCommitAttempt&` ) 을 값을 돌려줄 때까지(상한 `OnlineLimit::kMaxConflictRetry`) 부릅니다.
         * @param pName 로그에 적는 일 이름(`"Trade.Invite"`)
         */
        template <typename T, typename TAttemptFunction>
        [[nodiscard]] static T run( IServiceStoreConnection& connection, const utf8* pName, TAttemptFunction&& attemptFunction )
        {
            for ( int32 attemptIndex = 0; attemptIndex < OnlineLimit::kMaxConflictRetry; ++attemptIndex )
            {
                ServiceCommitAttempt attempt( connection );
                optional<T>          answer = attemptFunction( attempt );
                if ( answer.has_value() )
                    return std::move( *answer );
            }
            reportExhausted( pName );
            return ServiceConflictAnswer<T>::make();
        }

        /**
         * @brief 다시 하지 않는 커밋 하나입니다 — 충돌이 곧 답인 쓰기(차지 · 기대 버전을 받은 바꾸기)와 다음 회차가 다시 보는 정리에 씁니다.
         * @details 요청에 답하는 "읽고 판단하고 쓰기" 는 `run` 을 쓴다 — 여기서 충돌을 받으면 다시 읽을 길이 없다.
         */
        [[nodiscard]] static ServiceStoreResult commitOnce( IServiceStoreConnection& connection, const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo = nullptr );

    private:
        static void reportExhausted( const utf8* pName );
    };
} // namespace sw
