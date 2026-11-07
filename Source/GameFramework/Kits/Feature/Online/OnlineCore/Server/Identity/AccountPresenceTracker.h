/**
 * @file AccountPresenceTracker.h
 * @brief 서비스 하나가 접속 상태(`IAccountPresence`)에 맡긴 찾기의 앞입니다 — 찾기 주인 몫 · 요청 번호 표 · 결과 델리게이트를 서비스마다 다시 적지 않습니다.
 * @details 채팅 귓속말 · 거래 상대 · 잔액 알림 · 새 우편 알림 · 매칭 유예가 저마다 `_findOwner` 와 `unordered_map< 요청 번호, 기다리는 값 >` 과 결과 함수를 두고
 *          손으로 찾아 지웠다. 여기서는 완료 람다가 기다리는 값을 잡는다(`PendingRequestTable` — `ServiceStoreTracker` 와 같은 모양).
 *          - `bind` 가 주인 몫을 열고 `unbind` 가 놓은 뒤 열린 찾기를 취소로 끝낸다(접속 상태 관리자가 이 서비스보다 오래 살아도 사라진 객체를 부르지 않는다).
 *          - 접속 상태가 없으면(서버 한 대) 찾기를 맡지 않고 false 다 — 부르는 쪽이 그 자리에서 끝낸다.
 *          - 서비스 스레드에서만 쓴다. 결과는 호스트 틱 안에서 오며 요청 함수 안에서 바로 오지 않는다(`IAccountPresence` 계약).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/AccountPresence.h"
#include "GameFramework/Runtime/Online/Service/PendingRequestTable.h"

namespace sw
{
    /** @class AccountPresenceTracker @brief 접속 상태 찾기의 주인 몫과 열린 찾기 표입니다. 서비스의 멤버로 두고 옮기지 않습니다(결과 델리게이트가 주소를 든다). */
    class AccountPresenceTracker
    {
    public:
        AccountPresenceTracker()
            : _table{}
            , _pPresence{ nullptr }
            , _owner{}
        {
        }
        ~AccountPresenceTracker() { _owner.reset(); }

        AccountPresenceTracker( const AccountPresenceTracker& )            = delete;
        AccountPresenceTracker& operator=( const AccountPresenceTracker& ) = delete;

        /** @brief 접속 상태 @p pPresence 에 주인 몫을 엽니다. 서버 한 대면 nullptr 입니다. 이미 묶였으면 먼저 `unbind` 합니다. */
        void bind( IAccountPresence* pPresence )
        {
            unbind();
            _pPresence = pPresence;
            _owner     = pPresence != nullptr ? pPresence->openFindOwner() : Subscription{};
        }

        /** @brief 주인 몫을 놓고 열린 찾기를 취소로 끝냅니다. 서비스가 내려갈 때 부릅니다. */
        void unbind()
        {
            _owner.reset(); // 먼저 놓는다 — 취소 완료가 도는 동안 답이 오지 않는다
            _pPresence = nullptr;
            _table.cancelAll();
        }

        IAccountPresence* getPresence() const { return _pPresence; }
        bool              hasPresence() const { return _pPresence != nullptr; }

        /**
         * @brief 계정 @p accountID 가 접속한 서버를 찾고, 답이 오면 @p complete 를 부릅니다(`PendingRequestTable::track` 의 두 모양).
         * @param ownerTag `cancelOwnedBy` 가 고르는 주인(요청한 계정)입니다. 없으면 0 입니다.
         * @return 접속 상태가 없거나 맡지 못했으면 false 이고 @p complete 는 불리지 않습니다.
         */
        template <typename TComplete>
        bool findByAccount( AccountID accountID, uint64 ownerTag, TComplete&& complete )
        {
            if ( _pPresence == nullptr )
                return false;
            return track( _pPresence->submitFindByAccount( accountID, AccountPresenceDelegate::create<&AccountPresenceTracker::onFound>( this ), _owner ), ownerTag,
                          std::forward<TComplete>( complete ) );
        }

        /** @brief 접속한 계정을 표시 이름으로 찾습니다. 나머지는 `findByAccount` 와 같습니다. */
        template <typename TComplete>
        bool findByDisplayName( string_view displayName, uint64 ownerTag, TComplete&& complete )
        {
            if ( _pPresence == nullptr )
                return false;
            return track( _pPresence->submitFindByDisplayName( displayName, AccountPresenceDelegate::create<&AccountPresenceTracker::onFound>( this ), _owner ), ownerTag,
                          std::forward<TComplete>( complete ) );
        }

        /** @brief 주인 @p ownerTag 의 찾기를 취소로 끝냅니다(그 계정이 떠났다). */
        void cancelOwnedBy( uint64 ownerTag ) { _table.cancelOwnedBy( ownerTag ); }

        /** @brief 답을 기다리는 찾기 수입니다. */
        int32 getOpenCount() const { return _table.getOpenCount(); }

    private:
        template <typename TComplete>
        bool track( uint64 requestID, uint64 ownerTag, TComplete&& complete )
        {
            if ( requestID == 0 )
                return false; // 몫이 살아 있지 않아 맡지 않았다
            return _table.track( requestID, ownerTag, std::forward<TComplete>( complete ) );
        }

        void onFound( const AccountPresenceResult& result ) { (void)_table.answer( result._requestID, result ); } // 모르는 번호는 취소한 뒤 온 답이다

    private:
        PendingRequestTable<uint64, AccountPresenceResult> _table;
        IAccountPresence*                                  _pPresence;
        Subscription                                       _owner; ///< 찾기의 주인 몫 — 놓으면 그 몫으로 맡긴 찾기의 델리게이트가 불리지 않는다
    };
} // namespace sw
