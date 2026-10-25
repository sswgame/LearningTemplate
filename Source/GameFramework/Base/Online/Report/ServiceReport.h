/**
 * @file ServiceReport.h
 * @brief 플레이어 신고 레코드입니다. 신고한 순간의 문맥(채팅이면 그 메시지와 앞 메시지들)을 스냅숏으로 남기고, GM 도구가 열린 신고를 읽어 처리합니다.
 * @details - 신고를 쓰는 키트(채팅)와 처리하는 키트(GM)가 서로 include 하지 않도록 형식을 기반 `Online/` 에 둡니다. 제재(`ServiceSanction`)와 같은 구조입니다.
 *          - 테이블 `service_report` 의 키는 `<상태>/<만든 시각 16진>/<신고자 16진>.<고유 16진>` 이며 상태는 `open` 과 `closed` 입니다.
 *            키 순서가 시간 순서라 GM 도구는 `open/` 을 최근 것부터 읽습니다. 처리하면 같은 트랜잭션에서 `open/` 키를 지우고 `closed/` 키로 옮깁니다.
 *          - 값은 버전 헤더가 붙은 자기 기술형(A, `ServiceRecordUtil`)입니다. 문맥 줄은 신고할 때의 사본이라 원본 기록이 보존 기간으로 지워져도 남습니다.
 *          - 닫힌 신고의 보존 기간 정리는 서버 런타임의 예약 작업(`ServiceRetentionJob`)만 합니다. 게임 코드에는 신고를 지우는 함수가 없습니다.
 *          Nakama 의 신고 기능은 없고, PlayFab 의 `ReportPlayer` 와 국내 게임사 GM 툴의 신고 큐에 해당합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Guard/RequestLimits.h"
#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Store/ServiceRecordUnknownFields.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 신고 처리 상태입니다. 저장 값이고 와이어 값이므로 새 값은 끝에만 추가합니다. */
    ENUM()
    enum class ServiceReportStatus : uint8
    {
        Open = 0,
        Resolved, ///< 조치했다(제재 등은 GM 이 따로 건다)
        Dismissed ///< 조치할 것이 없다
    };
} // namespace sw

namespace sw
{
    /** @brief 신고 문맥 한 줄입니다. 채팅이면 메시지 하나이며 상한은 채팅 메시지(`ChatMessage`)와 같습니다. */
    REFLECT()
    struct SW_GF_API ServiceReportLine
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = RequestLimits::kMaxDisplayNameSize )
        string _senderName{};
        PROPERTY( MaxCount = ServiceReportConstant::kMaxLineTextSize )
        string _text{};
        PROPERTY()
        AccountID _senderID{ kInvalidAccountID };
        PROPERTY()
        int64 _sentMs{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 신고 하나입니다. 저장소 레코드(A)이고 GM 목록 응답에는 고정 배치형(B)으로 실립니다.
     * @details `_listLine` 의 마지막 줄이 신고한 대상입니다. 상한 값은 `ServiceReportConstant` 와 같습니다.
     */
    REFLECT()
    struct SW_GF_API ServiceReport
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = ServiceReportConstant::kMaxLineCount )
        vector<ServiceReportLine> _listLine{};
        PROPERTY( MaxCount = ServiceReportConstant::kMaxReportKeySize )
        string _reportKey{}; ///< 저장소 키이다. GM 이 처리할 때 이 값으로 고른다
        PROPERTY( MaxCount = ServiceReportConstant::kMaxCategorySize )
        string _category{}; ///< 신고 종류 `[0-9a-z_.]` 이다. 채팅은 "chat" 이다
        PROPERTY( MaxCount = ServiceReportConstant::kMaxContextSize )
        string _context{}; ///< 종류마다 뜻이 다르다. 채팅이면 채널 id 또는 귓속말 기록 키이다
        PROPERTY( MaxCount = ServiceReportConstant::kMaxReasonSize )
        string _reason{}; ///< 신고자가 고른 사유 코드 `[0-9a-z_.]` 이며 로컬라이제이션 키이다
        PROPERTY( MaxCount = ServiceReportConstant::kMaxMemoSize )
        string _resolutionMemo{}; ///< 처리한 GM 의 메모이다
        PROPERTY()
        AccountID _reporterID{ kInvalidAccountID };
        PROPERTY()
        AccountID _targetID{ kInvalidAccountID };
        PROPERTY()
        AccountID _resolverID{ kInvalidAccountID };
        PROPERTY()
        int64 _createdMs{ 0 };
        PROPERTY()
        int64 _resolvedMs{ 0 };
        PROPERTY()
        ServiceReportStatus _status{ ServiceReportStatus::Open };
        /// 이 빌드가 모르는 칸(배포 중 새 빌드가 더한 칸)이다. PROPERTY 가 아니다 — 영속 읽기가 담고 쓰기가 그대로 싣는다(`ServiceRecordUtil`).
        ServiceRecordUnknownFields _unknownFields{};
    };
} // namespace sw

namespace sw
{
    /** @brief 신고의 상한 값들입니다. `PROPERTY( MaxCount )` 가 이 이름을 씁니다. */
    struct ServiceReportConstant
    {
        static constexpr int32  kMaxLineCount     = 20;
        static constexpr int32  kMaxLineTextSize  = 512; ///< 문맥 줄의 글. 채팅 메시지(`ChatLimit::kMaxTextSize`)의 상한과 같다
        static constexpr int32  kMaxCategorySize  = 32;
        static constexpr int32  kMaxContextSize   = 48;
        static constexpr int32  kMaxReasonSize    = 64;
        static constexpr int32  kMaxMemoSize      = 1024;
        static constexpr int32  kMaxReportKeySize = 96;
        static constexpr uint32 kRecordVersion    = 1;
    };
} // namespace sw

namespace sw
{
    /** @brief 신고 쓰기와 읽기입니다. 쓰기는 트랜잭션에 넣기만 하고, 읽기와 처리는 저장소 스레드(`IServiceStoreWork::run`)에서 부릅니다. */
    struct SW_GF_API ServiceReportStore
    {
        static const hashed_string& getTable(); ///< "service_report"
        /** @brief 닫힌 신고(`closed/<만든 시각 16 진>/…`)의 만든 시각을 읽습니다(보존 기간 쓸기 `ServiceRetention` 용). 열린 신고와 틀린 키는 false 입니다. */
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
