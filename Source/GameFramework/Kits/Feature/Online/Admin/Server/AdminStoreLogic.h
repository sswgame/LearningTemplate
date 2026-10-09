/**
 * @file AdminStoreLogic.h
 * @brief GM 명령의 저장 부분 — 권한 확인 · 효과 · 감사 줄 · 멱등 기록을 한 트랜잭션에. 저장소 스레드의 동기 함수입니다.
 * @details 순서: 등급 읽기(`admin_role`) → 바꾸는 명령이면 멱등 기록 확인(`ServiceIdempotency`, 범위 `gm.<16 진>`) → 효과를 붙이고 감사 줄 + 멱등 기록을 같은 트랜잭션에
 *          → 커밋 → 판 충돌이면 다시(멱등 기록이 생겼으면 지난 결과). 감사 줄의 고유 값 = 멱등 키라 같은 명령은 감사 줄도 하나다 — 효과만 있고 기록이 없는 일이 없다.
 *          회수는 음수 금지, 단 환불 회수(`_bRefund`)는 빚을 허용한다(사용자 결정 8 — 빚이 있는 동안 그 재화는 쓰지 못한다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Admin/Shared/AdminProtocol.h"

namespace sw
{
    class ILedgerPolicy;

    /** @brief 명령 하나의 입력입니다(값으로 — 일이 들고 저장소 스레드로 간다). */
    struct AdminCommand
    {
        AdminRequest         _request{};
        const ILedgerPolicy* _pPolicy{ nullptr };
        AccountId            _adminId{ kInvalidAccountId };
        uint64               _keyHigh{ 0 }; ///< 멱등 키(바꾸는 명령은 필수)
        uint64               _keyLow{ 0 };
        int64                _nowMs{ 0 };
        uint16               _method{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief GM 명령 저장 함수입니다(저장소 스레드). */
    struct SW_GF_API AdminStoreLogic
    {
        static const hashed_string& getRoleTable(); ///< "admin_role" — 키 = 계정 16 진, 값 = 등급 1 바이트

        static AdminResult execute( IServiceStoreConnection& connection, const AdminCommand& command, AdminReply& outReply );
        /** @brief 등급을 읽습니다(레코드가 없으면 None 과 판 0). */
        [[nodiscard]] static ServiceStoreResult readRole( IServiceStoreConnection& connection, AccountId accountId, AdminRole& outRole, uint64& outVersion );
        /** @brief 서버 설정의 첫 관리자 — 레코드가 없을 때만 넣는다(감사 줄 actor "system.bootstrap"). 운영 중 바뀐 등급은 덮지 않는다. */
        static AdminResult seedRole( IServiceStoreConnection& connection, AccountId accountId, AdminRole role, int64 nowMs );
    };
} // namespace sw
