/**
 * @file IAfterDeleteAccount.h
 * @brief 탈퇴 뒤 키트 정리 훅(`IAfterDeleteAccount`)과 그 감싸개(`AfterDeleteAccountFunction` · `AfterDeleteAccountMember`)입니다.
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

    // 탈퇴 정리가 계정 레코드를 지운 뒤 키트마다 부르는 훅 — 키트가 `IOnlineServerKit::getAfterDeleteAccount` 로 내놓고, 계정 키트의 탈퇴 정리가 재시도 대기열
    // (`account_after_delete`)로 모든 키트를 부른다. Nakama 런타임의 `RegisterAfterDeleteAccount` 훅, PlayFab DeletePlayer 가 타이틀 데이터 · 통계 · 친구를 함께 지우는 자리다.
    /**
     * @class IAfterDeleteAccount
     * @brief 저장소 스레드(`ServiceStoreJob::run`)에서 부릅니다. 키트가 소유하며 바뀌지 않는 상태만 읽어야 합니다(여러 저장소 일이 동시에 부를 수 있다).
     */
    class IAfterDeleteAccount
    {
    public:
        IAfterDeleteAccount()          = default;
        virtual ~IAfterDeleteAccount() = default;

        IAfterDeleteAccount( const IAfterDeleteAccount& )            = delete;
        IAfterDeleteAccount& operator=( const IAfterDeleteAccount& ) = delete;

        /**
         * @brief 지운 계정 @p accountID 를 키나 값으로 둔 이 키트의 레코드를 지우거나 익명화합니다.
         * @details 멱등이어야 한다 — 다 끝낼 때까지 다음 회차가 다시 부른다. 트랜잭션 상한 안에서 스스로 나눠 커밋한다.
         * @return 남은 것이 없으면 Ok, 충돌로 다 못 했으면 Conflict, 저장소 문제면 Unavailable 입니다. Ok 가 아니면 대기열에 남습니다.
         */
        [[nodiscard]] virtual ServiceStoreResult afterDeleteAccount( IServiceStoreConnection& connection, AccountID accountID, int64 nowMs ) const = 0;
    };

    /** @brief 상태 없는 저장 함수 하나 @p TErase 를 훅으로 감쌉니다. 키트가 멤버로 두고 `getAfterDeleteAccount` 가 그 주소를 반환합니다. */
    template <ServiceStoreResult ( *TErase )( IServiceStoreConnection& connection, AccountID accountID, int64 nowMs )>
    class AfterDeleteAccountFunction final : public IAfterDeleteAccount
    {
    public:
        AfterDeleteAccountFunction() = default;

        [[nodiscard]] ServiceStoreResult afterDeleteAccount( IServiceStoreConnection& connection, AccountID accountID, int64 nowMs ) const override
        {
            return TErase( connection, accountID, nowMs );
        }
    };

    /** @brief 주인 @p TOwner 의 상수 멤버 함수 @p TErase 를 훅으로 감쌉니다(바뀌지 않는 정의를 읽는 서비스). 주인보다 짧게 삽니다. */
    template <class TOwner, ServiceStoreResult ( TOwner::*TErase )( IServiceStoreConnection& connection, AccountID accountID, int64 nowMs ) const>
    class AfterDeleteAccountMember final : public IAfterDeleteAccount
    {
    public:
        explicit AfterDeleteAccountMember( const TOwner* pOwner )
            : _pOwner{ pOwner }
        {
        }

        [[nodiscard]] ServiceStoreResult afterDeleteAccount( IServiceStoreConnection& connection, AccountID accountID, int64 nowMs ) const override
        {
            return ( _pOwner->*TErase )( connection, accountID, nowMs );
        }

    private:
        const TOwner* _pOwner;
    };
} // namespace sw
