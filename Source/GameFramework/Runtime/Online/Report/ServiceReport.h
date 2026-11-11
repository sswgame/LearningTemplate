/**
 * @file ServiceReport.h
 * @brief 플레이어 신고 레코드입니다. 신고한 순간의 문맥(채팅이면 그 메시지와 앞 메시지들)을 스냅숏으로 남기고, GM 도구가 열린 신고를 읽어 처리합니다.
 * @details - 신고를 쓰는 키트(채팅)와 처리하는 키트(GM)가 서로 include 하지 않도록 형식을 기반 `Online/` 에 둡니다. 제재(`ServiceSanction`)와 같은 구조입니다.
 *          - 테이블 `service_report` 의 키는 `<상태>/<만든 시각 16진>/<신고자 16진>.<고유 16진>` 이며 상태는 `open` 과 `closed` 입니다.
 *            키 순서가 시간 순서라 GM 도구는 `open/` 을 최근 것부터 읽습니다. 처리하면 같은 트랜잭션에서 `open/` 키를 지우고 `closed/` 키로 옮깁니다.
 *          - 값은 버전 헤더가 붙은 자기 기술형(A, `ServiceRecordUtil`)입니다. 문맥 줄은 신고할 때의 사본이라 원본 기록이 보존 기간으로 지워져도 남습니다.
 *          - 닫힌 신고의 보존 기간 정리는 서버 런타임의 예약 작업(`ServiceRetentionJob`)만 합니다. 게임 코드에는 신고를 지우는 함수가 없습니다.
 *          쓰기 · 읽기 · 처리(`ServiceReportStore`)는 서버만 돌아 `Report/Server/ServiceReportStore.h` 에 있다. Nakama 의 신고 기능은 없고, PlayFab 의 `ReportPlayer` 와 국내 게임사 GM 툴의 신고 큐에 해당합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Identity/AccountIdentity.h"
#include "GameFramework/Runtime/Online/Service/OnlineLimit.h"
#include "GameFramework/Runtime/Online/Store/ServiceRecordUnknownFields.h"

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
        PROPERTY( MaxCount = OnlineLimit::kMaxDisplayNameSize )
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
        ServiceRecordUnknownFields _unknownProperties{};
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
