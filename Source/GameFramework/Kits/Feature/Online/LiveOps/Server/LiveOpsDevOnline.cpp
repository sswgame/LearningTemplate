/**
 * @file LiveOpsDevOnline.cpp
 * @brief DevOnline 단위 — 라이브 운영 키트(서버)의 가짜 푸시 제공자를 확장점 `LiveOpsDevOptions` 에 올립니다.
 * @details 배포 구성은 이 파일을 짓지 않는다(`SW_DEV_ONLY_SOURCE_PATTERNS`) — 그 구성에서 `fakePush` 를 켜면 키트 기동이 실패한다.
 */
#include "pch.h"

#include "GameFramework/Base/Online/Config/DevOnlineExtension.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsDevOptions.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Fake/FakePushProvider.h"

namespace sw
{
    namespace
    {
        struct LiveOpsDevOnlineInternal
        {
            static unique_ptr<IPushNotificationProvider> createFakePushProvider()
            {
                return make_unique<FakePushProvider>();
            }

            static constexpr LiveOpsDevOptions kDevOptions{ &createFakePushProvider };
        };
    } // namespace

    SW_REGISTER_DEV_ONLINE_EXTENSION( LiveOpsDevOptions, LiveOpsDevOnlineInternal::kDevOptions );
} // namespace sw
