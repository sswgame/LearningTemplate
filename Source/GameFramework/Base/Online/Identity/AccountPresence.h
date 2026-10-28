/**
 * @file AccountPresence.h
 * @brief 서버 여럿에 걸친 접속 상태 인터페이스입니다. 계정이 지금 어느 서버에 접속해 있는지(계정 id 로)와 접속한 계정을 표시 이름으로 찾고(비동기, 캐시),
 *        다른 서버의 계정에게 알림을 보냅니다(버스).
 * @details - 계정 키트(서버)가 구현한다(`OnlinePresence`: 캐시 `presence:` 키, 버스 `push.<서버>`). 거래, 채팅, 친구, 매칭 같은 다른 키트는 이 인터페이스만 본다
 *            (키트끼리 include 하지 못하며, 기록 형식은 구현 하나가 갖는다).
 *          - 찾기 결과는 등록한 델리게이트로 정확히 한 번 온다(`cancel` 한 것은 제외). 호스트 `tick` 안에서 오며, 요청 함수 안에서 바로 부르지 않는다.
 *            여러 서비스가 같이 찾아도 서로의 결과를 가져가지 않는다.
 *          - 서버 한 대면 없어도 된다(nullptr). 이 프로세스의 `IAccountDirectory` 로 충분하다. 알림은 버스로 최대 한 번이고, 원본은 저장소다.
 *          언리얼 `IOnlinePresence::QueryPresence`(사용자 id 에서 상태, 완료 델리게이트)와 Nakama status registry 에 해당한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Identity/OnlinePushRelay.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 찾은 결과 하나입니다. 접속해 있지 않으면 `_serverID` 가 0 입니다. */
    struct AccountPresenceResult
    {
        AccountIdentity _identity{}; ///< 이름으로 찾았으면 공개 신원, 계정 id 로 찾았으면 계정 id 만(못 찾았으면 계정 id 0)
        uint64          _requestID{ 0 };
        uint64          _serverID{ 0 }; ///< 그 계정이 붙어 있는 서버(이 서버일 수도 있다)

        bool isOnline() const { return _serverID != 0; }
    };

    using AccountPresenceDelegate = Delegate<void( const AccountPresenceResult& )>;
} // namespace sw

namespace sw
{
    /**
     * @class IAccountPresence
     * @brief 접속 상태 인터페이스입니다(서비스 스레드).
     * @details 찾기는 주인 몫(`openFindOwner` 의 구독)과 함께 맡긴다. 주인이 몫을 놓으면 그 몫으로 맡긴 찾기의 델리게이트는 불리지 않는다 —
     *          주인이 이 인터페이스보다 먼저 내려가도 사라진 객체를 부르지 않는다. 구현은 `releaseSubscription( 몫 번호 )` 에서 그 찾기를 지운다.
     */
    class IAccountPresence
    {
        // 순수 인터페이스라 내보내지 않는다. 인라인 생성자와 소멸자를 GameFramework.dll 이 내보내지 않으므로 dllimport 로 해석되면 키트 링크가 깨진다(C-3).
    public:
        IAccountPresence()          = default;
        virtual ~IAccountPresence() = default;

        IAccountPresence( const IAccountPresence& )            = delete;
        IAccountPresence& operator=( const IAccountPresence& ) = delete;

        /** @brief 찾기 주인의 몫을 엽니다. 주인이 멤버로 들고, 놓으면(소멸 · `reset`) 그 몫으로 맡긴 찾기의 델리게이트가 불리지 않습니다. */
        [[nodiscard]] Subscription openFindOwner() { return _subscriptions.issue( _nextFindOwnerID++ ); }
        /**
         * @brief 접속한 계정을 표시 이름(대소문자 무시)으로 찾습니다. 0 이 아닌 요청 id 를 돌려주며, 결과는 @p onFound 로 한 번 옵니다.
         * @details @p owner 가 이 인터페이스가 연 살아 있는 몫이 아니면 맡지 않고 0 입니다.
         */
        virtual uint64 submitFindByDisplayName( string_view displayName, const AccountPresenceDelegate& onFound, const Subscription& owner ) = 0;
        /**
         * @brief 계정이 지금 접속한 서버를 찾습니다(친구 접속 표시, 다른 서버 귓속말, 매칭 결과를 보낼 곳). 0 이 아닌 요청 id 를 돌려주며, 결과는 @p onFound 로 한 번 옵니다.
         * @details @p owner 는 `submitFindByDisplayName` 과 같습니다.
         */
        virtual uint64 submitFindByAccount( AccountID accountID, const AccountPresenceDelegate& onFound, const Subscription& owner ) = 0;
        /**
         * @brief 다른 서버에 접속한 계정에게 알림을 보냅니다. 그 계정의 서버를 찾아 넘기고, 접속해 있지 않으면 버립니다. 서버 여럿이 아니면 false 입니다.
         * @details 알림 번호와 B 본문이 아니라 알림 열거자와 본문 값을 받습니다. 받는 서버가 자기 빌드의 번호와 B 로 다시 씁니다(`OnlinePushRelay`).
         *          그 알림은 키트 서버 `.cpp` 에 `OnlinePushRelayEntry` 로 등록돼 있어야 합니다.
         */
        template <typename TPushName, typename TBody>
        bool sendRemotePush( const AccountID accountID, const TPushName pushName, const TBody& body )
        {
            OnlinePushRelay relay;
            if ( OnlinePushRelayRegistry::makeRelay( accountID, pushName, body, relay ) == false )
                return false;
            return relayRemotePush( relay );
        }
        /** @brief 만든 중계 봉투(`OnlinePushRelayRegistry::makeRelay`)를 그 계정의 서버로 넘깁니다. 서버 여럿이 아니면 false. */
        virtual bool relayRemotePush( const OnlinePushRelay& relay ) = 0;

    private:
        uint64 _nextFindOwnerID{ 1 };

    protected:
        /** @brief 구독 @p id 가 풀렸습니다 — 구현이 그 등록을 지운다(`_subscriptions` 가 부른다). */
        virtual void releaseSubscription( uint64 id ) = 0;

        /** @brief 이 객체가 내준 구독입니다(믹스인 상속 대신 합성). 풀리면 `releaseSubscription` 이 불린다. 맨 끝 멤버라 먼저 사라져 남은 구독을 무효화한다. */
        SubscriptionList _subscriptions{ SubscriptionList::ReleaseFunction::create<&IAccountPresence::releaseSubscription>( this ) };
    };
} // namespace sw
