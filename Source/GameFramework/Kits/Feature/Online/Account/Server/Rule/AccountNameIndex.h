/**
 * @file AccountNameIndex.h
 * @brief 계정 이름 색인 — 기반 `IAccountNameIndex` 를 계정 표(`login_account` 소문자 이름 → 계정, `login_account_id` 계정 → 프로필)로 구현합니다.
 * @details 상태가 없다(연결만 쓴다) — 게임 조립이 하나 만들어 친구 · 우편 같은 서버 키트에 빌려준다. 표 형식은 `LoginStoreLogic` 이 정본이다.
 */
#pragma once
#include "GameFramework/Base/Online/Identity/AccountNameIndex.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class AccountNameIndex
     * @brief 계정 이름 색인입니다.
     */
    class SW_GF_API AccountNameIndex final : public IAccountNameIndex
    {
    public:
        ServiceStoreResult readIdentityByDisplayName( IServiceStoreConnection& connection, string_view displayName, AccountIdentity& outIdentity ) const override;
        ServiceStoreResult readIdentity( IServiceStoreConnection& connection, AccountID accountID, AccountIdentity& outIdentity ) const override;
    };
} // namespace sw
