/**
 * @file MailboxKitConfig.h
 * @brief 우편함 키트의 서버 설정입니다(`_listKit[]._settings`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"

namespace sw
{
    /** @brief 우편 알림 · 캠페인 · 정리의 운영 값입니다. 코드 기본값이 없다. */
    REFLECT()
    struct SW_KIT_API MailboxDeliveryPolicy
    {
        REFLECT_BODY();
        PROPERTY( Required, Min = 1 )
        int64 _campaignRefreshMs{ 0 };
        PROPERTY( Required, Min = 0 )
        int64 _notificationIntervalMs{ 0 }; ///< 새 우편 알림을 가져가는 주기이다. 0 이면 알림을 보내지 않는다(다른 서버가 맡는다)
        PROPERTY( Required, Min = 1 )
        int32 _sweepBatchCount{ 0 };
        PROPERTY( Required, Min = 1, Max = 64 )
        int32 _notificationBatchCount{ 0 }; ///< 한 번에 가져가는 알림 수이다(트랜잭션 상한 64 안)
    };
} // namespace sw

namespace sw
{
    /** @brief 우편함 키트의 서버 설정입니다. */
    REFLECT()
    struct MailboxKitConfig
    {
        REFLECT_BODY();
        /** @brief 만료 정리 주기(밀리초)입니다. 이 주기의 예약 작업(`mail.sweep`)을 등록해 서버 하나만 정리합니다. */
        PROPERTY( Required, Min = 60000 )
        int32 _sweepIntervalMs{ 0 };
        /** @brief 받은 우편을 남기는 기간(일)입니다. 수령 때 만료를 "받은 뒤 이 기간" 으로 다시 걸고, 만료 정리가 그때 지웁니다(PlayFab · MMO 우편함과 같다). */
        PROPERTY( Required, Min = 1, Max = 3650 )
        int32 _claimedRetentionDays{ 0 };
        /** @brief 끝난 전체 우편(캠페인)과 그 수령 표식의 TTL 입니다. 캠페인이 끝난 뒤 이 기간이 지나면 TTL 쓸기가 지웁니다. */
        PROPERTY( Required )
        ServiceTTLConfig _endedCampaignTTL{};
        /** @brief 모두 받기 한 번에 받는 우편 수입니다(우편마다 커밋이라 저장소 일을 묶는다). */
        PROPERTY( Required, Min = 1, Max = 64 )
        int32 _maxClaimAllCount{ 0 };
        /** @brief 모두 받기 한 번에 훑는 우편 레코드 수입니다(최근 것부터). `_maxClaimAllCount` 이상이어야 합니다. */
        PROPERTY( Required, Min = 1, Max = 5000 )
        int32 _maxClaimAllScanCount{ 0 };
        /** @brief 우편 알림 · 캠페인 · 정리의 운영 값입니다. */
        PROPERTY( Required )
        MailboxDeliveryPolicy _delivery{};
    };
} // namespace sw
