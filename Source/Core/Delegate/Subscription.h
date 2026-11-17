/**
 * @file Subscription.h
 * @brief 등록 하나의 수명을 소유하는 이동 전용 구독과, 구독을 내주는 출처의 공통 기반입니다.
 * @details 등록 API(`MulticastDelegate::add` · `EventDispatcher::subscribe` · `NetMessageRouter::registerHandler` …)는 핸들 대신 `Subscription` 을
 *          돌려준다. 구독이 사라지면 등록이 풀리므로 해제를 잊을 수 없다(언리얼 `FDelegateHandle` 은 해제를 호출부가 기억해야 하는 형식이라 쓰지 않는다).
 *          출처가 먼저 사라지면 출처의 침입형 목록이 살아 있는 구독을 모두 무효화한다 — 그 뒤 구독의 소멸은 아무것도 하지 않는다.
 *          구독은 **[[nodiscard]]** 다. 반환값을 버리면 그 자리에서 풀리므로, 받지 않는 호출은 컴파일러가 막는다.
 *          목록 잠금 · 목록 머리 · 출처 포인터는 출처 밖 **제어 블록**(`SubscriptionControl`, 출처당 하나)에 있고 구독이 그 블록을 참조로 살려 둔다 —
 *          구독이 잡는 잠금은 출처가 사라져도 남는다(언리얼 `TSharedPtr` 의 `FReferenceControllerBase` 가 객체보다 오래 사는 것과 같은 자리).
 * @note 스레드: 목록(만들기 · 옮기기 · 풀기)은 제어 블록의 스핀 잠금이 지킨다 — 서로 다른 스레드가 같은 출처의 구독을 따로 만들고 풀어도 된다(로그 리스너).
 *       등록 내용 자체(`releaseSubscription`)는 출처가 자기 잠금으로 지킨다. 출처의 소멸과 다른 스레드의 구독 해제는 겹쳐도 된다 — 잠금은 블록에 있어
 *       출처보다 오래 살고, 출처의 닫기(`closeSubscriptions`)는 이미 시작한 해제가 끝날 때까지 기다리며 그 뒤의 해제는 아무것도 부르지 않는다.
 *       전체 비우기(`detachAllSubscriptions`)는 기다리지 않는다(출처는 산다 — 진행 중인 해제는 출처의 잠금이 지킨다).
 *       출처 이동은 한 스레드 형식(`MulticastDelegate` · `DelegateSlot`)만 한다 — 여러 스레드 출처는 이동이 지워져 있다.
 */
#pragma once
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class Subscription;
    class SubscriptionSource;

    /**
     * @class SubscriptionControl
     * @brief 출처 하나의 구독 목록과 그 잠금을 든 제어 블록입니다. 출처가 첫 구독을 낼 때 한 번 할당합니다(구독 없는 출처는 할당 0).
     * @details 참조 수 = 출처 1 + 블록을 가리키는 구독 수. 마지막 참조가 블록을 지운다. 칸은 `Subscription` · `SubscriptionSource` 만 만진다.
     */
    class SubscriptionControl final
    {
    public:
        SubscriptionControl() noexcept = default;

        SubscriptionControl( const SubscriptionControl& )            = delete;
        SubscriptionControl& operator=( const SubscriptionControl& ) = delete;
        SubscriptionControl( SubscriptionControl&& )                 = delete;
        SubscriptionControl& operator=( SubscriptionControl&& )      = delete;

    private:
        friend class Subscription;
        friend class SubscriptionSource;

        /** @brief 참조 하나를 놓습니다. 마지막 참조면 블록을 지웁니다. */
        void releaseReference() noexcept;
        /** @brief @p subscription 이 목록에 올라 있으면 true 입니다. 잠금을 쥐고 부릅니다. */
        bool isLinkedLocked( const Subscription& subscription ) const noexcept;
        /** @brief @p subscription 을 목록에서 빼고 id 를 지웁니다. 잠금을 쥐고 부릅니다. */
        void unlinkLocked( Subscription& subscription ) noexcept;
        /** @brief 목록의 구독을 모두 무효화합니다(해제는 부르지 않는다). 잠금을 쥐고 부릅니다. */
        void unlinkAllLocked() noexcept;
        /** @brief 이 블록의 해제가 다른 스레드에서 끝날 때까지 기다립니다. 이 스레드가 안에 든 해제 @p ownReleaseCount 개는 기다리지 않습니다. */
        void waitForReleases( uint32 ownReleaseCount ) noexcept;

    private:
        SpinLock            _lock;                ///< 아래 칸과 구독의 `_pPrev` · `_pNext` · `_id` 를 지킨다
        SubscriptionSource* _pSource{ nullptr };  ///< 구독을 낸 출처. 닫히면 nullptr
        Subscription*       _pHead{ nullptr };    ///< 살아 있는 구독의 침입형 목록 머리
        atomic<uint32>      _refCount{ 1 };       ///< 출처 1 + 블록을 가리키는 구독 수. 0 이 되면 블록을 지운다
        atomic<uint32>      _releasingCount{ 0 }; ///< 잠금 밖 `releaseSubscription` 안에 있는 구독 수. 맨 위 비트는 닫는 쪽이 기다린다는 표시(서 있을 때만 해제가 깨운다)
    };

    /**
     * @class Subscription
     * @brief 등록 하나를 소유합니다. 소멸 · `reset` · 이동 대입이 등록을 풉니다. 복사할 수 없습니다.
     * @details 출처의 제어 블록을 참조합니다(출처가 먼저 사라지면 `isActive` 가 false 가 되고 푸는 일은 없다). 빈 구독(기본 생성)은 아무것도 소유하지 않습니다.
     *          여러 개를 들 때는 `vector<Subscription>` 에 담습니다.
     */
    class [[nodiscard]] Subscription
    {
    public:
        /** @brief 아무것도 소유하지 않는 빈 구독입니다. */
        Subscription() noexcept = default;
        /** @brief 등록을 풉니다. */
        ~Subscription()
        {
            if ( _pControl != nullptr )
                reset();
        }

        Subscription( const Subscription& )            = delete;
        Subscription& operator=( const Subscription& ) = delete;

        /** @brief 등록을 넘겨받습니다. @p other 는 빈 구독이 됩니다. */
        Subscription( Subscription&& other ) noexcept { takeFrom( other ); }
        /** @brief 지금 등록을 풀고 @p other 의 등록을 넘겨받습니다. */
        Subscription& operator=( Subscription&& other ) noexcept
        {
            if ( this != &other )
            {
                reset();
                takeFrom( other );
            }
            return *this;
        }

        /**
         * @brief 등록을 지금 풉니다. 빈 구독이거나 출처가 이미 무효화했으면 아무것도 하지 않습니다.
         * @details 출처의 `releaseSubscription` 은 목록 잠금 밖에서 부르고, 그동안 출처의 닫기(`closeSubscriptions`)는 이 호출이 끝나기를 기다린다.
         */
        SW_API void reset() noexcept;

        /** @brief 출처가 살아 있고 등록을 소유하고 있으면 true 입니다. 출처가 등록을 거절했거나(겹치는 처리기) 먼저 사라졌으면 false 입니다. */
        SW_API bool isActive() const noexcept;

    private:
        friend class SubscriptionControl;
        friend class SubscriptionSource;

        /** @brief @p other 의 블록 참조와 목록 자리를 그대로 넘겨받습니다(이웃과 머리를 이 객체로 고친다). 이 구독은 비어 있어야 합니다. */
        SW_API void takeFrom( Subscription& other ) noexcept;

    private:
        SubscriptionControl* _pControl{ nullptr }; ///< 참조하는 제어 블록. 이 구독만 쓴다(출처가 남의 구독 칸을 잠금 밖에서 읽고 쓰는 길이 없다)
        Subscription*        _pPrev{ nullptr };    ///< 블록의 침입형 목록에서 앞 구독
        Subscription*        _pNext{ nullptr };    ///< 블록의 침입형 목록에서 뒤 구독
        uint64               _id{ 0 };             ///< 출처가 등록을 찾는 값(델리게이트 핸들 · 처리기 번호). 뜻은 출처가 정한다. 무효화되면 0
    };

    /**
     * @class SubscriptionSource
     * @brief 구독을 내주는 쪽의 기반입니다. 살아 있는 구독을 제어 블록의 침입형 목록으로 들고, 닫히면 모두 무효화합니다.
     * @details 파생 클래스는 등록할 때 `issueSubscription( id )` 로 구독을 만들어 돌려주고, 구독이 풀릴 때 불리는 `releaseSubscription( id )` 에서
     *          그 등록을 지웁니다. 복사본은 구독을 넘겨받지 않습니다(구독은 원본의 등록이다). 이동은 구독을 데려갑니다(구독이 새 자리를 가리킨다).
     *          `releaseSubscription` 은 목록 잠금을 놓은 뒤 부른다 — 파생 클래스가 자기 잠금을 잡아도 순서가 뒤집히지 않는다.
     *          **파생 소멸자는 첫 줄에서 `closeSubscriptions()` 를 부릅니다** — 파생 멤버가 사라지기 전에 진행 중인 해제를 기다려야 하기 때문이다.
     *          빠뜨리면 기반 소멸자가 멈춘다(그 형식을 지우는 첫 테스트에서 드러난다).
     */
    class SubscriptionSource
    {
    public:
        SubscriptionSource() noexcept = default;
        /** @brief 파생 소멸자가 닫았는지 확인합니다. 닫지 않았으면 멈춥니다. */
        virtual ~SubscriptionSource() { checkClosed(); }

        /** @brief 복사본은 구독을 넘겨받지 않습니다. */
        SubscriptionSource( const SubscriptionSource& ) noexcept
            : _pControl{ nullptr }
        {
        }
        /** @brief 대입은 구독 목록을 바꾸지 않습니다. 등록 내용을 바꾸는 파생 클래스가 필요하면 `detachAllSubscriptions` 를 부릅니다. */
        SubscriptionSource& operator=( const SubscriptionSource& ) noexcept { return *this; }

        /** @brief 구독을 데려옵니다. 데려온 구독은 이 객체를 가리킵니다. */
        SubscriptionSource( SubscriptionSource&& other ) noexcept
            : _pControl{ nullptr }
        {
            takeSubscriptionsFrom( other );
        }
        /** @brief 이 객체의 구독을 무효화하고 @p other 의 구독을 데려옵니다. 진행 중인 해제는 기다리지 않습니다(옮기는 출처는 한 스레드 형식뿐이다). */
        SubscriptionSource& operator=( SubscriptionSource&& other ) noexcept
        {
            if ( this != &other )
            {
                dropControl();
                takeSubscriptionsFrom( other );
            }
            return *this;
        }

    private:
        friend class Subscription;

    protected:
        /** @brief 등록 @p id 를 소유하는 구독을 만들어 목록에 올립니다. 닫힌 출처에서 부르면 결함입니다. */
        [[nodiscard]] SW_API Subscription issueSubscription( uint64 id ) noexcept;

        /**
         * @brief @p subscription 이 이 출처가 내준 살아 있는 구독이면 그 id, 아니면 0 입니다. 등록에 주인 구독을 받는 출처가 씁니다.
         * @details 구독을 푸는 스레드와 같은 스레드에서 부릅니다(구독의 블록 칸을 잠금 없이 읽는다).
         */
        [[nodiscard]] SW_API uint64 findIssuedID( const Subscription& subscription ) const noexcept;

        /** @brief 구독 @p id 가 풀렸습니다. 그 등록을 지웁니다. 구독은 이미 목록에서 빠졌고 목록 잠금은 풀려 있습니다. */
        virtual void releaseSubscription( uint64 id ) = 0;

        /**
         * @brief 등록 @p id 를 소유한 살아 있는 구독 하나를 무효화합니다(해제는 부르지 않는다). 없으면 아무것도 하지 않습니다.
         * @details 한 번 쓰고 끝나는 등록(요청 하나의 완료 콜백)이 끝났을 때 출처가 부른다 - 구독은 `isActive` 가 false 가 되어 들고 있는 쪽이 끝난 요청을 가려낸다.
         */
        SW_API void detachSubscription( uint64 id ) noexcept;

        /** @brief 살아 있는 구독을 모두 무효화합니다. 등록은 파생 클래스가 따로 지웁니다(전체 비우기). 출처는 산다 — 진행 중인 해제는 기다리지 않습니다. */
        SW_API void detachAllSubscriptions() noexcept;

        /**
         * @brief 출처를 닫습니다 — 구독을 모두 무효화하고, 다른 스레드에서 진행 중인 `releaseSubscription` 이 끝날 때까지 기다립니다. 파생 소멸자 첫 줄에서 부릅니다.
         * @details 기다림은 끝까지 한다(포기하면 해제가 소멸된 파생을 만진다). 이 스레드가 이 출처의 해제 안에서 닫으면 그 해제는 기다리지 않는다(자기를 기다리게 된다).
         *          두 번 불러도 된다. 닫힌 뒤 `issueSubscription` 은 결함이다.
         */
        SW_API void closeSubscriptions() noexcept;

    private:
        /** @brief @p other 의 제어 블록을 데려옵니다. 이 객체는 블록이 없어야 합니다. */
        SW_API void takeSubscriptionsFrom( SubscriptionSource& other ) noexcept;
        /** @brief 구독을 무효화하고 블록 참조를 놓습니다(이동 대입 — 출처는 산다). 진행 중인 해제는 기다리지 않습니다. */
        SW_API void dropControl() noexcept;
        /** @brief 닫히지 않았으면 멈춥니다(소멸자). */
        SW_API void checkClosed() const noexcept;

    private:
        atomic<SubscriptionControl*> _pControl{ nullptr }; ///< 제어 블록. 첫 구독 전에는 nullptr, 닫히면 닫힘 표시(`Subscription.cpp`)
    };
} // namespace sw
