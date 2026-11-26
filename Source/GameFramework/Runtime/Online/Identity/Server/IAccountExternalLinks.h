/**
 * @file IAccountExternalLinks.h
 * @brief 서버의 외부 로그인 연동 포트입니다(`IAccountDirectory` 와 같은 계정 포트 묶음).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Identity/AccountIdentity.h"
#include "GameFramework/Runtime/Online/Service/OnlineServerKitName.h"

namespace sw
{
    class IServiceStoreConnection;
    enum class ServiceStoreResult : uint8;

    // 외부 로그인 연동 포트 — 계정 키트(서버)가 구현하고, 스토어 신원과 계정을 이어야 하는 키트(경제의 영수증 구매자 대조)가 서버 조립 문맥에서 받는다. 연동 테이블의 형식은 계정 키트만 안다.
    // 저장소 스레드에서 부른다: `IAccountNameIndex` 와 같이 부르는 쪽의 저장소 작업 안에서 그 연결로 읽고, 구현은 상태가 없다.
    // 언리얼 `IOnlineIdentity`(플랫폼 사용자 id)를 게임 서버가 대조하는 자리다. 주체는 다이제스트만 저장되므로 "이 주체인가" 로만 묻는다 — 저장된 값을 꺼내 줄 수 없다.
    /**
     * @class IAccountExternalLinks
     * @brief 외부 로그인 연동 인터페이스입니다(저장소 스레드).
     */
    class IAccountExternalLinks
    {
        // 순수 인터페이스라 내보내지 않는다. 인라인 생성자와 소멸자를 GameFramework.dll 이 내보내지 않으므로 dllimport 로 해석되면 키트 링크가 깨진다(C-3).
    public:
        static constexpr const utf8* kProviderKit = OnlineServerKitName::kAccount; ///< 이 인터페이스를 내는 키트다. 받는 키트의 의존 이름이 여기서 나온다(`OnlineServerKit`)

        IAccountExternalLinks()          = default;
        virtual ~IAccountExternalLinks() = default;

        IAccountExternalLinks( const IAccountExternalLinks& )            = delete;
        IAccountExternalLinks& operator=( const IAccountExternalLinks& ) = delete;

        /** @brief @p accountID 가 제공자 @p provider 의 주체 @p subject 에 연동돼 있으면 @p outbLinked 가 true 입니다. 연동이 없거나 다른 주체면 false 이고 결과는 Ok 입니다. 저장소가 아프면 Unavailable. */
        [[nodiscard]] virtual ServiceStoreResult readIsLinked( IServiceStoreConnection& connection, AccountID accountID, string_view provider, string_view subject, bool& outbLinked ) const = 0;
    };
} // namespace sw
