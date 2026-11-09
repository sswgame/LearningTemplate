/**
 * @file ServiceSanction.h
 * @brief 계정 제재입니다. 종류(채팅 금지, 정지, 영구 정지)마다 끝 시각과 사유 코드를 계정마다 레코드 하나(`service_sanction`, 키는 계정 16 진)에 둡니다.
 * @details GM 키트(GF_Admin)가 버전 조건으로 쓰고, 계정 키트는 로그인 · 재접속에서, 채팅 키트는 말하기에서 읽는다(저장소 스레드). 지난 제재 이력은 감사 로그가 갖는다.
 *          쓰는 쪽과 읽는 쪽이 다른 키트라 기반에 둔다. 넥슨과 엔씨 GM 툴의 "계정 제재(종류, 기간, 사유)" 항목과 같다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Sanction/ServiceSanctionState.h"
#include "GameFramework/Runtime/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Runtime/Online/Store/ServiceStore.h"

namespace sw
{
    /** @brief 제재 레코드 읽기 · 쓰기입니다(저장소 스레드). */
    struct SW_GF_API ServiceSanction
    {
        static const hashed_string& getTable();
        /** @brief 읽습니다. 레코드가 없으면 `Ok` 와 빈 상태(버전 0)입니다. */
        [[nodiscard]] static ServiceStoreResult readState( IServiceStoreConnection& connection, uint64 accountID, ServiceSanctionState& outState );
        /** @brief 지금(@p nowMs) @p kind 가 걸려 있는가입니다. */
        static bool isActive( const ServiceSanctionState& state, ServiceSanctionKind kind, int64 nowMs );
        /** @brief 로그인을 막는 제재(정지 · 영구 정지)가 걸려 있으면 그 끝 시각(둘 중 늦은 것), 아니면 0 입니다. */
        static int64 getLoginBlockedUntilMs( const ServiceSanctionState& state, int64 nowMs );
        /** @brief 새 상태를 버전 조건(@p state 의 `_version`. 0 이면 "없어야 한다")으로 붙입니다. 모든 종류가 0 이면 레코드를 지운다. */
        static void stageWrite( ServiceTransaction& inoutTransaction, uint64 accountID, const ServiceSanctionState& state );
    };
} // namespace sw

namespace sw
{
    /** @brief 제재 버스(`ServiceSanctionBus::kChangedTopic`)의 본문입니다. 제재가 바뀐 계정입니다. */
    REFLECT()
    struct SW_GF_API ServiceSanctionChanged
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1; ///< 의미 버전
        PROPERTY()
        uint64 _accountID{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ServiceSanctionBus
     * @brief 제재가 바뀌었다는 서버 버스 주제입니다. GM 키트가 쓴 뒤 내고, 제재를 묵혀 보는 키트(채팅)는 그 계정의 묵힌 값을 버린다.
     * @details 버스는 최대 한 번 배달한다. 놓치면 읽는 쪽이 캐시 시한(채팅 60 초) 안에 다시 읽는다.
     */
    struct ServiceSanctionBus
    {
        static constexpr ServerBusTopic<ServiceSanctionChanged> kChangedTopic{ "sanction.changed" };
    };
} // namespace sw
