/**
 * @file ServiceStoreEntryTestUtil.h
 * @brief 저장 함수를 저장소 입구(`IServiceStoreJob::execute`)로 돌리는 테스트 도구입니다. 서비스가 쓰는 길과 같은 읽기 집합 포장을 거친다.
 * @details 저장 함수가 손으로 `requireVersion` 을 걸지 않으므로(입구가 읽은 레코드를 커밋 조건으로 붙인다), "읽기와 커밋 사이" 경쟁 테스트는 이 길로 돌아야 실제 서비스와 같다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceReadSetConnection.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

#include <utility>

namespace sw
{
    /** @brief 함수 하나를 저장 일로 감쌉니다. 완료는 쓰지 않습니다. */
    template <typename TFunction>
    class ServiceStoreEntryTestJob final : public IServiceStoreJob
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
            job.execute( connection );
        }
    };
} // namespace sw
