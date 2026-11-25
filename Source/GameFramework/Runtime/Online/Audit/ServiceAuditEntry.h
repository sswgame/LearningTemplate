/**
 * @file ServiceAuditEntry.h
 * @brief 감사 줄 하나의 값 형식입니다. 관리자 감사 목록 응답(키트 프로토콜)에 실려 클라이언트 · 도구도 읽습니다.
 * @details 쓰기 · 읽기(`ServiceAuditLog`)와 감사 붙은 쓰기(`ServiceAuditedWrite`)는 서버만 돌아 `Audit/Server/ServiceAuditLog.h` 에 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Online/Store/ServiceRecordUnknownFields.h"

namespace sw
{
    /**
     * @brief 감사 줄 하나입니다.
     * @details 관리자 감사 목록 응답에 고정 배치형으로 실립니다. 앞의 셋은 관리자 프로토콜의 문구 상한(256)이고, 뒤의 셋은 아래 상수와 같은 값입니다.
     *          감사 표(`ServiceAuditLog::getTable`)의 레코드 형식이기도 하다(판은 이 형식의 `REFLECT( Version )`).
     */
    REFLECT()
    struct ServiceAuditEntry
    {
        REFLECT_BODY();
        static constexpr int32 kMaxTextSize  = 256;  ///< `_actor` · `_action` · `_subject`. 관리자 프로토콜의 문구 상한(`AdminProtocol::kMaxTextSize`)과 같다
        static constexpr int32 kMaxStateSize = 2048; ///< `_before` · `_after`
        static constexpr int32 kMaxMemoSize  = 1024;

        PROPERTY( MaxCount = kMaxTextSize )
        string _actor{}; ///< `acct.<16 진>`, `gm.<16 진>`, `system` — `ServiceAuditLog::makeAccountActor` · `makeOperatorActor` · `kSystemActor` 로 만든다
        PROPERTY( MaxCount = kMaxTextSize )
        string _action{}; ///< 동작 코드 `[0-9a-z_.]`. 예: "trade.settle", "admin.sanction", "account.link"
        PROPERTY( MaxCount = kMaxTextSize )
        string _subject{}; ///< 대상. 키의 앞부분이며 키 규칙을 따른다
        PROPERTY( MaxCount = kMaxStateSize )
        string _before{}; ///< 전 상태(짧은 JSON 또는 요약)
        PROPERTY( MaxCount = kMaxStateSize )
        string _after{};
        PROPERTY( MaxCount = kMaxMemoSize )
        string _memo{};
        PROPERTY()
        int64 _timeMs{ 0 }; ///< UTC 벽시계 밀리초(0 이상)

        ServiceRecordUnknownFields _unknownProperties{}; ///< 감사 표(`service_audit`) 레코드가 이 빌드가 모르는 칸을 든다
    };
} // namespace sw
