/**
 * @file LiveEventRules.h
 * @brief 이벤트가 지금 열려 있는지 판정합니다. 기간, 반복 회차, 빌드 버전, 지역, 출시 비율을 봅니다. 저장과 전송은 모르며 테스트가 시각을 넣습니다.
 * @details 반복 회차는 기반 계층의 `ServiceScheduler::computeLatestOccurrence`(UTC 기준 매일과 매주)로 구하고, 출시 비율은 `RemoteConfig::computeRolloutBucket` 으로 구한다.
 *          원격 설정 플래그와 같은 해시를 쓰므로 같은 계정은 항상 같은 쪽에 속한다. 해시 이름이 이벤트 id 이므로 이벤트마다 다른 계정 집합이 선택된다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsTypes.h"

namespace sw
{
    /**
     * @struct LiveEventRules
     * @brief 열림 판정을 담은 순수 함수 모음입니다.
     */
    struct SW_KIT_SERVER_API LiveEventRules
    {
        /** @brief 계정과 무관한 열림(기간과 회차)을 판정합니다. 열렸으면 이번 창의 끝을 @p outWindowEndMs 에 채웁니다. */
        static bool isWindowOpen( const LiveEventDefinition& definition, int64 nowMs, int64& outWindowEndMs );
        /** @brief `isWindowOpen` 과 같되 이번 창의 시작도 @p outWindowStartMs 에 채웁니다(정의의 시작보다 앞서지 않는다). 시작 푸시가 "방금 열렸나" 를 가릴 때 쓴다. */
        [[nodiscard]] static bool findOpenWindow( const LiveEventDefinition& definition, int64 nowMs, int64& outWindowStartMs, int64& outWindowEndMs );
        /** @brief 이 계정, 지역, 빌드에 보이는지 판정합니다. 창은 따로 확인합니다. */
        static bool isAudienceMatch( const LiveEventDefinition& definition, AccountID accountID, string_view region, uint32 buildVersion );
        static bool isValid( const LiveEventDefinition& definition );
    };
} // namespace sw
