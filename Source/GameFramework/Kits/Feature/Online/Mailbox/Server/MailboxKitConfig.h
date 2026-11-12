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
    /** @brief 우편함 키트의 서버 설정입니다. */
    REFLECT()
    struct MailboxKitConfig
    {
        REFLECT_BODY();
        /** @brief 만료 정리 주기(밀리초)입니다. 이 주기의 예약 작업(`mail.sweep`)을 등록해 서버 하나만 정리합니다. */
        PROPERTY( Min = 60000 )
        int32 _sweepIntervalMs{ 60000 };
        /** @brief 받은 우편을 남기는 기간(일)입니다. 수령 때 만료를 "받은 뒤 이 기간" 으로 다시 걸고, 만료 정리가 그때 지웁니다(PlayFab · MMO 우편함과 같다). */
        PROPERTY( Min = 1, Max = 3650 )
        int32 _claimedRetentionDays{ 30 };
        /** @brief 끝난 전체 우편(캠페인)과 그 수령 표식의 TTL 입니다. 캠페인이 끝난 뒤 이 기간이 지나면 TTL 쓸기가 지웁니다. */
        PROPERTY( Required )
        ServiceTTLConfig _endedCampaignTTL{};
    };
} // namespace sw
