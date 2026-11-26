/**
 * @file IAccountDirectory.h
 * @brief 서버의 계정 포트입니다. 계정 디렉터리 인터페이스(이 프로세스에 접속한 계정)를 둡니다. 값 형식(계정 id · 공개 신원)은 공유 `Identity/AccountIdentity.h` 에 있습니다.
 *        계정을 다루는 다른 포트(차단 목록 `IAccountBlockList`, 세션 끊기 `IAccountSessionControl`, 이름 색인 `IAccountNameIndex`, 외부 로그인 연동 `IAccountExternalLinks`, 오프라인 푸시 `IOfflinePushSender`,
 *        탈퇴 뒤 키트 정리 `IAfterDeleteAccount`)는 같은 폴더의 자기 헤더에 있다.
 * @details 발급은 계정 키트(GF_Account, GF_Server_Account)가 한다. 다른 키트(Trade, Economy, Mailbox, Admin)는 이 인터페이스만 보고 계정 키트를 include 하지 않는다
 *          (키트끼리는 include 하지 않는다). 서버가 여럿이면 다른 서버에 붙은 계정은 캐시 층의 접속 상태(`ICacheStore`)로 본다.
 *          세션 토큰 검증은 따로 인터페이스를 두지 않는다. 스트림 연결은 로그인할 때 한 번 확인되고 그 뒤로는 연결의 주체로 다니며, UDP 게임 서버는 계정 키트의 접속 테이블이 확인한다.
 *          언리얼에서 친구, 세션, 업적 인터페이스가 `IOnlineIdentity`(`FUniqueNetId`, 닉네임, 로그인 상태)를 같이 보는 것에 해당한다.
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

    /**
     * @class IAccountDirectory
     * @brief 계정 디렉터리 인터페이스입니다. 이 프로세스에 접속한 계정만 답합니다(메모리만 보고 저장소를 읽지 않는다). 서비스 스레드에서 부릅니다.
     */
    class IAccountDirectory
    {
    public:
        static constexpr const utf8* kProviderKit = OnlineServerKitName::kAccount; ///< 이 인터페이스를 내는 키트다. 받는 키트의 의존 이름이 여기서 나온다(`OnlineServerKit`)

        IAccountDirectory()          = default;
        virtual ~IAccountDirectory() = default;

        IAccountDirectory( const IAccountDirectory& )            = delete;
        IAccountDirectory& operator=( const IAccountDirectory& ) = delete;

        /** @brief 계정 id 의 공개 정보입니다. 이 프로세스에 접속한 계정만 알며, 모르면 false 입니다. */
        [[nodiscard]] virtual bool findIdentity( AccountID accountID, AccountIdentity& outIdentity ) const = 0;
        /** @brief 접속한 계정을 표시 이름(대소문자 무시)으로 찾습니다. 거래 신청과 귓속말은 상대가 접속해 있어야 합니다. */
        [[nodiscard]] virtual bool findIdentityByDisplayName( string_view displayName, AccountIdentity& outIdentity ) const = 0;
        /** @brief 지금 이 프로세스에 로그인해 붙어 있는가입니다(재접속 유예 중이면 false). */
        virtual bool isAccountOnline( AccountID accountID ) const = 0;
    };
} // namespace sw
