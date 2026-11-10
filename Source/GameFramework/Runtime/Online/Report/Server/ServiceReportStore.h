/**
 * @file ServiceReportStore.h
 * @brief 신고 레코드의 쓰기 · 읽기 · 처리입니다(서버). 형식은 공유 `Report/ServiceReport.h` 에 있습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Report/ServiceReport.h"
#include "GameFramework/Runtime/Online/Store/ServiceStore.h"

namespace sw
{
    /** @brief 신고 쓰기와 읽기입니다. 쓰기는 트랜잭션에 넣기만 하고, 읽기와 처리는 저장소 스레드(`IServiceStoreJob::run`)에서 부릅니다. */
    struct SW_GF_API ServiceReportStore
    {
        static const hashed_string& getTable(); ///< "service_report"
        /** @brief 닫힌 신고(`closed/<만든 시각 16 진>/…`)의 만든 시각을 읽습니다(보존 기간 쓸기 `ServiceRetentionRule` 용). 열린 신고와 틀린 키는 false 입니다. */
        [[nodiscard]] static bool readClosedTimeMs( const ServiceRecord& record, int64& outTimeMs );
        /** @brief 열린 신고의 키를 만듭니다. @p uniqueID 는 같은 신고자가 같은 밀리초에 낸 신고를 가릅니다. */
        static string makeOpenKey( int64 createdMs, AccountID reporterID, uint64 uniqueID );
        /** @brief 비어 있지 않고 @p maxSize 바이트 이하이며 `[0-9a-z_.]` 만 있으면 true 입니다. 종류와 사유 코드의 규칙입니다. */
        static bool isValidCode( string_view text, int32 maxSize );
        /** @brief 종류, 사유, 문맥, 줄 수, 신고자가 규칙에 맞으면 true 입니다. */
        static bool isValidReport( const ServiceReport& report );
        /**
         * @brief 열린 신고를 @p inoutTransaction 에 넣습니다("없어야 한다" 조건). `_reportKey` 와 `_status` 는 이 함수가 채웁니다.
         * @return 쓸 수 없는 신고(규칙 위반, 레코드 쓰기 실패)면 false 이며 트랜잭션은 그대로입니다. 부르는 쪽은 요청을 Invalid 로 끝냅니다.
         */
        [[nodiscard]] static bool stageOpen( ServiceTransaction& inoutTransaction, ServiceReport& inoutReport, uint64 uniqueID );
        /**
         * @brief 열린(@p bOpen) 또는 닫힌 신고를 최근 것부터 읽습니다. 손상된 레코드가 있으면 Invalid 이며 건너뛰지 않습니다.
         * @param cursor 앞 호출의 @p outNextCursor(처음은 빈 글)
         */
        [[nodiscard]] static ServiceStoreResult listReports( IServiceStoreConnection& connection, bool bOpen, string_view cursor, int32 maxCount,
                                                             vector<ServiceReport>& outListReport, string& outNextCursor );
        /**
         * @brief 열린 신고 @p reportKey 를 처리해 닫힌 쪽으로 옮기는 쓰기를 @p inoutTransaction 에 넣습니다. 열린 키를 읽은 버전으로 지우고 닫힌 키를 씁니다.
         * @details 커밋은 부르는 쪽이 감사 기록, 멱등 기록과 함께 합니다. 그사이 다른 GM 이 처리했으면 커밋이 Conflict 입니다.
         * @return 열린 신고가 없으면 NotFound, @p status 가 Open 이거나 메모가 상한을 넘거나 레코드가 손상됐으면 Invalid 입니다.
         */
        [[nodiscard]] static ServiceStoreResult stageResolve( IServiceStoreConnection& connection, string_view reportKey, ServiceReportStatus status, AccountID resolverID,
                                                              string_view memo, int64 nowMs, ServiceTransaction& inoutTransaction, ServiceReport& outReport );
    };
} // namespace sw
