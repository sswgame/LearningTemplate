/**
 * @file EconomyDevOnline.cpp
 * @brief DevOnline 단위 — 경제 키트(서버)의 가짜 영수증 검증 제공자를 확장점 `EconomyDevOptions` 에 올립니다.
 * @details 배포 구성은 이 파일을 짓지 않는다(`SW_DEV_ONLY_SOURCE_PATTERNS`) — 그 구성에서 `fakeReceipts` 를 켜면 키트 기동이 실패하고,
 *          경제 서비스는 개발 전용 제공자를 가진 레지스트리를 거절한다.
 */
#include "pch.h"

#include "GameFramework/Base/Online/Config/DevOnlineExtension.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyDevOptions.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Fake/FakeReceiptValidator.h"

namespace sw
{
    namespace
    {
        struct EconomyDevOnlineInternal
        {
            static unique_ptr<IReceiptValidator> createFakeReceiptValidator()
            {
                return make_unique<FakeReceiptValidator>();
            }

            static constexpr EconomyDevOptions kDevOptions{ &createFakeReceiptValidator };
        };
    } // namespace

    SW_REGISTER_DEV_ONLINE_EXTENSION( EconomyDevOptions, EconomyDevOnlineInternal::kDevOptions );
} // namespace sw
