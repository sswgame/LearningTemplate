/**
 * @file ServiceStoreEntryTestUtil.h
 * @brief 저장 함수를 저장소 입구(`ServiceStoreJob::execute`)로 돌리는 테스트 도구와, 커밋을 정한 만큼 실패시키는 연결입니다. 서비스가 쓰는 길과 같은 읽기 집합 포장을 거친다.
 * @details 저장 함수가 손으로 `requireVersion` 을 걸지 않으므로(입구가 읽은 레코드를 커밋 조건으로 붙인다), "읽기와 커밋 사이" 경쟁 테스트는 이 길로 돌아야 실제 서비스와 같다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceCommitRunner.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceReadSetConnection.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordReader.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

#include <utility>

namespace sw
{
    /** @brief 앞 @p _conflictCount 번의 커밋은 쓰지 않고 `Conflict`, 그 뒤 @p _lostReplyCount 번은 쓰고도 `Unavailable`(응답을 잃었다)을 돌려주는 연결입니다. */
    class UnreliableStoreConnection final : public IServiceStoreConnection
    {
    public:
        int32 _conflictCount{ 0 };
        int32 _lostReplyCount{ 0 };
        int32 _commitCount{ 0 }; ///< 지금까지 받은 커밋 수

        explicit UnreliableStoreConnection( IServiceStoreConnection& inner )
            : _inner{ inner }
        {
        }

        [[nodiscard]] ServiceStoreResult readRecord( const hashed_string& table, string_view key, ServiceRecord& outRecord ) override { return _inner.readRecord( table, key, outRecord ); }

    private:
        [[nodiscard]] ServiceStoreResult commit( const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo ) override
        {
            ++_commitCount;
            if ( _conflictCount > 0 )
            {
                --_conflictCount;
                return ServiceStoreResult::Conflict; // SQL 직렬화 실패처럼 어긋난 쓰기 번호를 모른다(-1)
            }
            const ServiceStoreResult committed = ServiceCommitRunner::commitOnce( _inner, transaction, pOutInfo );
            if ( _lostReplyCount == 0 )
                return committed;
            --_lostReplyCount;
            return ServiceStoreResult::Unavailable;
        }
        [[nodiscard]] ServiceStoreResult listRecords( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending,
                                                      vector<ServiceRecord>& outListRecord ) override
        {
            return ServiceRecordReader::readPage( _inner, table, keyPrefix, cursorKey, maxCount, bDescending, outListRecord )._result;
        }

    private:
        IServiceStoreConnection& _inner;
    };

    /** @brief 함수 하나를 저장 일로 감쌉니다. 완료는 쓰지 않습니다. */
    template <typename TFunction>
    class ServiceStoreEntryTestJob final : public ServiceStoreJob
    {
    public:
        explicit ServiceStoreEntryTestJob( TFunction function )
            : _function{ std::move( function ) }
        {
        }

        void complete() override {}

    private:
        void run( ServiceReadSetConnection& connection ) override { _function( connection ); }

    private:
        TFunction _function;
    };

    /** @brief @p function( 연결 ) 을 @p connection 위의 저장소 입구로 한 번 돌립니다. */
    struct ServiceStoreEntryTestUtil
    {
        template <typename TFunction>
        static void run( IServiceStoreConnection& connection, TFunction function )
        {
            ServiceStoreEntryTestJob<TFunction> job( std::move( function ) );
            ServiceStoreCommitStats             stats; // 이 도구는 커밋 수를 보지 않는다 — 입구 계약만 따른다
            job.execute( connection, stats );
        }
    };
} // namespace sw
