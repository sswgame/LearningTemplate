/**
 * @file ServiceSanction.h
 * @brief 계정 제재 — 종류(채팅 금지 · 정지 · 영구 정지)마다 끝 시각과 사유 코드를 계정마다 레코드 하나(`service_sanction`, 키 = 계정 16 진)에 둡니다.
 * @details GM 키트(GF_Admin)가 판 조건으로 쓰고, 계정 키트는 로그인 · 재접속에서, 채팅 키트는 말하기에서 읽는다(저장소 스레드). 지난 제재 이력은 감사 로그가 갖는다.
 *          쓰는 쪽 · 읽는 쪽이 다른 키트라 기반에 둔다. 넥슨 · 엔씨 GM 툴의 "계정 제재(종류 · 기간 · 사유)" 칸과 같다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 제재 종류입니다. 저장 값이라 순서를 바꾸지 않는다. */
    enum class ServiceSanctionKind : uint8
    {
        ChatMute = 0, ///< 채팅 금지
        Suspend,      ///< 기간 정지 — 로그인 거절 + 세션 끊기
        Ban,          ///< 영구 정지
        Count
    };

    SW_GF_API const utf8* toString( ServiceSanctionKind kind );
} // namespace sw

namespace sw
{
    /** @brief 계정 하나의 제재 상태입니다. */
    struct ServiceSanctionState
    {
        static constexpr int64 kPermanentMs = 0x7FFFFFFFFFFFFFFFll;

        string _reasonCode{};                                                   ///< 표시용 사유 코드(로컬라이제이션 키) — 가장 최근 것
        int64  _arrUntilMs[static_cast<int32>( ServiceSanctionKind::Count )]{}; ///< 0 = 없음
        uint64 _version{ 0 };                                                   ///< 레코드 판(0 = 레코드 없음)
    };
} // namespace sw

namespace sw
{
    /** @brief 제재 레코드 읽기 · 쓰기입니다(저장소 스레드). */
    struct SW_GF_API ServiceSanction
    {
        static const hashed_string& getTable();
        /** @brief 읽습니다. 레코드가 없으면 `Ok` 와 빈 상태(판 0)입니다. */
        [[nodiscard]] static ServiceStoreResult readState( IServiceStoreConnection& connection, uint64 accountId, ServiceSanctionState& outState );
        /** @brief 지금(@p nowMs) @p kind 가 걸려 있는가입니다. */
        static bool isActive( const ServiceSanctionState& state, ServiceSanctionKind kind, int64 nowMs );
        /** @brief 로그인을 막는 제재(정지 · 영구 정지)가 걸려 있으면 그 끝 시각(둘 중 늦은 것), 아니면 0 입니다. */
        static int64 getLoginBlockedUntilMs( const ServiceSanctionState& state, int64 nowMs );
        /** @brief 새 상태를 판 조건(@p state 의 `_version` — 0 이면 "없어야 한다")으로 붙입니다. 모든 종류가 0 이면 레코드를 지운다. */
        static void stageWrite( ServiceTransaction& inoutTransaction, uint64 accountId, const ServiceSanctionState& state );
    };
} // namespace sw
