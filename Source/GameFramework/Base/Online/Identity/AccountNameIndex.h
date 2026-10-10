/**
 * @file AccountNameIndex.h
 * @brief 계정 이름 색인 창구 — 접속해 있지 않은 계정도 저장소에서 표시 이름 · 계정 id 로 찾습니다(친구 신청 · 우편 받는 이 · 귓속말 대상).
 * @details - 계정 키트(서버)가 구현한다(`AccountNameIndex` — 계정 표의 형식은 그쪽만 안다). 다른 키트는 이 창구만 보고 계정 키트를 include 하지 않는다.
 *          - **저장소 스레드에서 부른다**: 부르는 키트의 저장소 일(`IServiceStoreWork::run`) 안에서 그 연결로 읽는다 — 이름으로 찾고 같은 일에서 관계를 쓰면
 *            왕복이 하나다. 구현은 상태가 없어(연결만 쓴다) 어느 스레드에서 불러도 된다.
 *          - 이름으로 찾을 수 있는 것은 정식 계정(로그인 이름 = 표시 이름 — 고유)뿐이다. 게스트 · 외부 계정의 만든 이름(`Guest-…`)은 고유하지 않아 색인에 없다 — 계정 id 로 찾는다.
 *          - 접속해 있는 계정은 `IAccountDirectory`(이 프로세스) · `IAccountPresence`(서버 여럿)가 저장소를 읽지 않고 답한다 — 이 창구는 오프라인까지 볼 때.
 *          언리얼 `IOnlineUser::QueryUserIdMapping`(표시 이름 → 사용자 id) · PlayFab `GetAccountInfo(TitleDisplayName)` 과 같은 자리다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class IAccountNameIndex
     * @brief 계정 이름 색인 창구입니다(저장소 스레드).
     */
    class SW_GF_API IAccountNameIndex
    {
    public:
        IAccountNameIndex()          = default;
        virtual ~IAccountNameIndex() = default;

        IAccountNameIndex( const IAccountNameIndex& )            = delete;
        IAccountNameIndex& operator=( const IAccountNameIndex& ) = delete;

        /** @brief 정식 계정을 표시 이름(대소문자 무시)으로 찾습니다. 없거나 규칙 밖 이름이면 NotFound, 저장소가 아프면 Unavailable. */
        [[nodiscard]] virtual ServiceStoreResult readIdentityByDisplayName( IServiceStoreConnection& connection, string_view displayName,
                                                                            AccountIdentity& outIdentity ) const = 0;
        /** @brief 계정 id 의 공개 신원을 읽습니다(게스트 · 외부 계정 포함). 없으면 NotFound. */
        [[nodiscard]] virtual ServiceStoreResult readIdentity( IServiceStoreConnection& connection, AccountID accountID, AccountIdentity& outIdentity ) const = 0;
    };
} // namespace sw
