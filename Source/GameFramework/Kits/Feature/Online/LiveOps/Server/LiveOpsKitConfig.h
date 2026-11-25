/**
 * @file LiveOpsKitConfig.h
 * @brief 라이브 운영 키트의 서버 설정입니다(`_listKit[]._settings`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"

namespace sw
{
    /** @brief 라이브 운영 키트의 서버 설정입니다. 파일 경로는 프로젝트 루트 기준이고, 기동 때 절대 경로로 풀며 못 풀면 기동 실패입니다. */
    REFLECT()
    struct LiveOpsKitConfig
    {
        REFLECT_BODY();
        /** @brief 가짜 푸시 제공자("fake")를 등록합니다(DevOnline 단위만 — 확장점 `LiveOpsDevOptions`, 그 단위가 없는 구성은 기동 실패). */
        PROPERTY()
        bool _bFakePush{ false };
        /** @brief 실제 푸시 제공자 설정 JSON 입니다(항목 형식은 `PushProviderManager::readEntry`). 비우면 실제 제공자가 없습니다. */
        PROPERTY()
        string _pushProvidersFile{};
        /** @brief 제공자 `https://` 서버를 검증할 CA 번들 PEM 입니다. `_pushProvidersFile` 이 https 주소를 쓰면 필수입니다. */
        PROPERTY()
        string _pushTrustFile{};
        /** @brief 토픽 알림 표(`liveops_push`)의 TTL 입니다. 이벤트 창이 끝난 뒤 이 기간이 지나면 TTL 쓸기가 지웁니다. */
        PROPERTY( Required )
        ServiceTTLConfig _pushClaimTTL{};
        /** @brief 계정마다 등록할 수 있는 기기 수입니다. 넘으면 가장 오래된 기기를 밀어냅니다. */
        PROPERTY( Required, Min = 1, Max = PushLimit::kMaxDevicePerAccount )
        int32 _pushMaxDevicePerAccount{ 0 };
        /** @brief 일시 실패와 제공자 지연을 다시 시도하는 횟수입니다. */
        PROPERTY( Required, Min = 1, Max = 16 )
        int32 _pushMaxRetry{ 0 };
        /** @brief 첫 재시도까지의 밀리초입니다. 시도마다 두 배가 됩니다. */
        PROPERTY( Required, Min = 1, Max = 60000 )
        int64 _pushFirstBackoffMs{ 0 };
        /** @brief 계정마다 한꺼번에 보낼 수 있는 알림 수입니다(도배 제한). */
        PROPERTY( Required, Min = 1, Max = 1000 )
        int32 _pushAccountBurst{ 0 };
        /** @brief 계정마다 알림 하나가 다시 차는 간격(밀리초)입니다. */
        PROPERTY( Required, Min = 1000 )
        int64 _pushAccountRefillIntervalMs{ 0 };
        /** @brief 도배 제한이 보관하는 계정 수의 상한입니다. */
        PROPERTY( Required, Min = 1, Max = 10000000 )
        int32 _pushMaxTrackedAccountCount{ 0 };
    };
} // namespace sw
