/**
 * @file LiveEventRules.h
 * @brief 이벤트가 지금 열렸나 — 기간 · 반복 회차 · 빌드 판 · 지역 · 출시 비율. 저장 · 전송을 모릅니다(시험이 시각을 넣는다).
 * @details 반복 회차는 기반 `ServiceScheduler::computeLatestOccurrence`(UTC 매일 · 매주), 출시 비율은 `RemoteConfig::computeRolloutBucket` — 원격 설정 플래그와 같은 해시라
 *          같은 계정은 늘 같은 쪽이고, 해시 이름이 이벤트 id 라 이벤트마다 다른 계정 집합이 고른다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Shared/LiveOpsTypes.h"

namespace sw
{
    /**
     * @struct LiveEventRules
     * @brief 열림 판정(순수)입니다.
     */
    struct SW_GF_API LiveEventRules
    {
        /** @brief 계정과 무관한 열림(기간 · 회차)입니다. 열렸으면 이번 창의 끝을 @p outWindowEndMs 에. */
        static bool isWindowOpen( const LiveEventDefinition& definition, int64 nowMs, int64& outWindowEndMs );
        /** @brief 이 계정 · 지역 · 빌드에 보이나입니다(창은 따로). */
        static bool isAudienceMatch( const LiveEventDefinition& definition, AccountId accountId, string_view region, uint32 buildVersion );
        static bool isValid( const LiveEventDefinition& definition );
    };
} // namespace sw
