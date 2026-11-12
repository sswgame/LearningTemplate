/**
 * @file LiveOpsKitConfig.h
 * @brief 라이브 운영 키트의 서버 설정입니다(`_listKit[]._settings`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

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
        /** @brief 실제 푸시 제공자 설정 JSON 입니다(항목 형식은 `PushProviderSet::readEntry`). 비우면 실제 제공자가 없습니다. */
        PROPERTY()
        string _pushProvidersFile{};
        /** @brief 제공자 `https://` 서버를 검증할 CA 번들 PEM 입니다. `_pushProvidersFile` 이 https 주소를 쓰면 필수입니다. */
        PROPERTY()
        string _pushTrustFile{};
        /** @brief 토픽 알림 표(`liveops_push`)를 이벤트 창이 끝난 뒤 남기는 기간(일)입니다. 지나면 보존 쓸기가 지웁니다. */
        PROPERTY( Min = 1, Max = 3650 )
        int32 _pushClaimRetentionDays{ 30 };
    };
} // namespace sw
