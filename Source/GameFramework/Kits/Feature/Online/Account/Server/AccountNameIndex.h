/**
 * @file AccountNameIndex.h
 * @brief 계정 이름 인덱스와 외부 로그인 연동 조회입니다. 기반 계층의 `IAccountNameIndex` 를 계정 테이블(`login_account` 의 소문자 이름에서 계정으로, `login_account_id` 의 계정에서 프로필로)로,
 *        `IAccountExternalLinks` 를 연동 테이블(`login_account_external`)로 구현합니다.
 * @details 상태가 없고 연결만 쓴다. 계정 키트 조립(`AccountOnlineServerKit`)이 하나를 만들어 서버 조립 문맥으로 친구나 우편 같은 서버 키트에 빌려준다. 테이블 형식의 원본은 `LoginStoreLogic` 이다.
 */
#pragma once
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

namespace sw
{
    class ILoginCrypto;

    /**
     * @class AccountExternalLinks
     * @brief 외부 로그인 연동 조회입니다. @p pCrypto 는 빌려 쓰며 주체를 저장할 때와 같은 다이제스트를 계산합니다.
     */
    class SW_KIT_SERVER_API AccountExternalLinks final : public IAccountExternalLinks
    {
    public:
        explicit AccountExternalLinks( ILoginCrypto* pCrypto );

        ServiceStoreResult readIsLinked( IServiceStoreConnection& connection, AccountID accountID, string_view provider, string_view subject, bool& outbLinked ) const override;

    private:
        ILoginCrypto* _pCrypto;
    };

    /**
     * @class AccountNameIndex
     * @brief 계정 이름 인덱스입니다.
     */
    class SW_KIT_SERVER_API AccountNameIndex final : public IAccountNameIndex
    {
    public:
        ServiceStoreResult readIdentityByDisplayName( IServiceStoreConnection& connection, string_view displayName, AccountIdentity& outIdentity ) const override;
        ServiceStoreResult readIdentity( IServiceStoreConnection& connection, AccountID accountID, AccountIdentity& outIdentity ) const override;
    };
} // namespace sw
