/**
 * @file AdminStoreLogic.h
 * @brief GM 명령의 저장 부분입니다. 권한 확인, 효과, 감사 기록, 멱등 기록을 한 트랜잭션에 처리하는 저장소 스레드의 동기 함수입니다.
 * @details 처리 순서는 다음과 같다. 먼저 등급을 읽는다(`admin_role`). 상태를 바꾸는 명령이면 멱등 기록을 확인한다(`ServiceIdempotency`, 범위 `gm.<16진>`).
 *          그다음 효과를 붙이고 감사 기록과 멱등 기록을 같은 트랜잭션에 넣어 커밋한다. 버전 충돌이 나면 다시 시도하며, 멱등 기록이 생겼으면 지난 결과를 돌려준다.
 *          감사 기록의 고유 값이 멱등 키이므로 같은 명령은 감사 기록도 하나뿐이다. 효과만 있고 기록이 없는 일은 생기지 않는다.
 *          회수는 음수가 되지 않게 막는다. 다만 환불 회수(`_bRefund`)는 빚을 허용한다. 사용자가 정한 정책이며 빚이 있는 동안은 그 재화를 쓸 수 없다.
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

    /** @brief 명령 하나의 입력입니다. 값으로 담으며 작업이 들고 저장소 스레드로 갑니다. */
    struct AdminCommand
    {
        AdminRequest         _request{};
        const ILedgerPolicy* _pPolicy{ nullptr };
        AccountID            _adminID{ kInvalidAccountID };
        uint64               _keyHigh{ 0 }; ///< 멱등 키이다. 상태를 바꾸는 명령에는 필수이다
        uint64               _keyLow{ 0 };
        int64                _nowMs{ 0 };
        uint16               _method{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief GM 명령 저장 함수 모음입니다. 저장소 스레드에서 부릅니다. */
    struct SW_KIT_SERVER_API AdminStoreLogic
    {
        static const hashed_string& getRoleTable(); ///< "admin_role" 이다. 키는 계정 16진 문자열이고 값은 등급 1바이트이다

        static AdminResult execute( IServiceStoreConnection& connection, const AdminCommand& command, AdminReply& outReply );
        /** @brief 등급을 읽습니다. 레코드가 없으면 None 과 버전 0을 반환합니다. */
        [[nodiscard]] static ServiceStoreResult readRole( IServiceStoreConnection& connection, AccountID accountID, AdminRole& outRole, uint64& outVersion );
        /** @brief 서버 설정의 첫 관리자를 넣습니다. 레코드가 없을 때만 넣으며 감사 기록의 actor 는 "system.bootstrap" 입니다. 운영 중에 바뀐 등급은 덮어쓰지 않습니다. */
        static AdminResult seedRole( IServiceStoreConnection& connection, AccountID accountID, AdminRole role, int64 nowMs );
    };
} // namespace sw
