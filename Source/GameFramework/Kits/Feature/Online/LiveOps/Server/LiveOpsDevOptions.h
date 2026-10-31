/**
 * @file LiveOpsDevOptions.h
 * @brief 라이브 운영 키트(서버)의 개발 대체물 확장점입니다 — 가짜 푸시 제공자(`fakePush`).
 * @details DevOnline 단위(`LiveOpsOnlineServerKit.cpp`)가 올리고(`DevOnlineExtension.h`), 확장점은 `LiveOpsOnlineServerKit.cpp` 가 정의한다. 단위가 없는 구성(배포)에서
 *          `fakePush` 를 켜면 기동 오류다. 같은 모듈(GF_Server_LiveOps) 안에서만 쓴다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class IPushNotificationProvider;

    template <typename TValue>
    class DevOnlineExtension;

    /**
     * @struct LiveOpsDevOptions
     * @brief 라이브 운영 키트의 개발 대체물 값입니다. 파일 머리말 참고.
     */
    struct LiveOpsDevOptions
    {
        using CreateFakePushProviderFunction = unique_ptr<IPushNotificationProvider> ( * )();

        CreateFakePushProviderFunction _pCreateFakePushProvider{ nullptr }; ///< `fakePush` 가 발송기에 올리는 가짜 제공자("fake")를 만든다

        static DevOnlineExtension<LiveOpsDevOptions> s_devOnline;
    };
} // namespace sw
