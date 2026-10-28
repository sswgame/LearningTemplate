/**
 * @file Subscription.h
 * @brief 등록 하나의 수명을 소유하는 이동 전용 구독과, 구독을 내주는 출처의 공통 기반입니다.
 * @details 등록 API(`MulticastDelegate::add` · `EventDispatcher::subscribe` · `NetMessageRouter::registerHandler` …)는 핸들 대신 `Subscription` 을
 *          돌려준다. 구독이 사라지면 등록이 풀리므로 해제를 잊을 수 없다(언리얼 `FDelegateHandle` 은 해제를 호출부가 기억해야 하는 형식이라 쓰지 않는다).
 *          출처가 먼저 사라지면 출처의 침입형 목록이 살아 있는 구독을 모두 무효화한다 — 그 뒤 구독의 소멸은 아무것도 하지 않는다.
 *          구독은 **[[nodiscard]]** 다. 반환값을 버리면 그 자리에서 풀리므로, 받지 않는 호출은 컴파일러가 막는다.
 * @note 스레드: 목록(만들기 · 옮기기 · 풀기)은 출처의 스핀 잠금이 지킨다 — 서로 다른 스레드가 같은 출처의 구독을 따로 만들고 풀어도 된다(로그 리스너).
 *       등록 내용 자체(`releaseSubscription`)는 출처가 자기 잠금으로 지킨다. 출처의 소멸 · 전체 비우기(`detachAllSubscriptions`)와 다른 스레드의
 *       구독 해제가 겹치는 것은 계약 밖이다(출처가 사라지는 중이면 어느 잠금도 그 메모리를 지키지 못한다).
 */
#pragma once
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class SubscriptionSource;

    /**
     * @class Subscription
     * @brief 등록 하나를 소유합니다. 소멸 · `reset` · 이동 대입이 등록을 풉니다. 복사할 수 없습니다.
     * @details 출처를 약하게 가리킵니다(출처가 먼저 사라지면 `isActive` 가 false 가 되고 푸는 일은 없다). 빈 구독(기본 생성)은 아무것도 소유하지 않습니다.
     *          여러 개를 들 때는 `vector<Subscription>` 에 담습니다.
     */
    class [[nodiscard]] Subscription
    {
    public:
        /** @brief 아무것도 소유하지 않는 빈 구독입니다. */
        Subscription() noexcept = default;
        /** @brief 등록을 풉니다. */
        ~Subscription() { reset(); }

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

        /** @brief 등록을 지금 풉니다. 빈 구독이거나 출처가 이미 사라졌으면 아무것도 하지 않습니다. */
        inline void reset() noexcept;

        /** @brief 출처가 살아 있고 등록을 소유하고 있으면 true 입니다. 출처가 등록을 거절했거나(겹치는 처리기) 먼저 사라졌으면 false 입니다. */
        bool isActive() const noexcept { return _pSource != nullptr; }

    private:
        friend class SubscriptionSource;

        /** @brief @p other 의 목록 자리를 그대로 넘겨받습니다(이웃과 머리를 이 객체로 고친다). */
        inline void takeFrom( Subscription& other ) noexcept;

        SubscriptionSource* _pSource{ nullptr }; ///< 등록을 내준 출처. 출처가 먼저 사라지면 출처가 nullptr 로 바꾼다
        Subscription*       _pPrev{ nullptr };   ///< 출처의 침입형 목록에서 앞 구독
        Subscription*       _pNext{ nullptr };   ///< 출처의 침입형 목록에서 뒤 구독
        uint64              _id{ 0 };            ///< 출처가 등록을 찾는 값(델리게이트 핸들 · 처리기 번호). 뜻은 출처가 정한다
    };

    /**
     * @class SubscriptionSource
     * @brief 구독을 내주는 쪽의 기반입니다. 살아 있는 구독을 침입형 목록으로 들고, 먼저 사라지면 모두 무효화합니다.
     * @details 파생 클래스는 등록할 때 `issueSubscription( id )` 로 구독을 만들어 돌려주고, 구독이 풀릴 때 불리는 `releaseSubscription( id )` 에서
     *          그 등록을 지웁니다. 복사본은 구독을 넘겨받지 않습니다(구독은 원본의 등록이다). 이동은 구독을 데려갑니다(구독이 새 자리를 가리킨다).
     *          `releaseSubscription` 은 목록 잠금을 놓은 뒤 부른다 — 파생 클래스가 자기 잠금을 잡아도 순서가 뒤집히지 않는다.
     */
    class SubscriptionSource
    {
    public:
        SubscriptionSource() noexcept = default;
        /** @brief 살아 있는 구독을 모두 무효화합니다. 등록을 푸는 호출은 하지 않습니다(출처가 사라지면 등록도 사라진다). */
        virtual ~SubscriptionSource() { detachAllSubscriptions(); }

        /** @brief 복사본은 구독을 넘겨받지 않습니다. */
        SubscriptionSource( const SubscriptionSource& ) noexcept
            : _listLock{}
            , _pHead{ nullptr }
        {
        }
        /** @brief 대입은 구독 목록을 바꾸지 않습니다. 등록 내용을 바꾸는 파생 클래스가 필요하면 `detachAllSubscriptions` 를 부릅니다. */
        SubscriptionSource& operator=( const SubscriptionSource& ) noexcept { return *this; }

        /** @brief 구독을 데려옵니다. 데려온 구독은 이 객체를 가리킵니다. */
        SubscriptionSource( SubscriptionSource&& other ) noexcept
            : _listLock{}
            , _pHead{ nullptr }
        {
            takeSubscriptionsFrom( other );
        }
        /** @brief 이 객체의 구독을 무효화하고 @p other 의 구독을 데려옵니다. */
        SubscriptionSource& operator=( SubscriptionSource&& other ) noexcept
        {
            if ( this != &other )
            {
                detachAllSubscriptions();
                takeSubscriptionsFrom( other );
            }
            return *this;
        }

    protected:
        /** @brief 등록 @p id 를 소유하는 구독을 만들어 목록에 올립니다. */
        [[nodiscard]] Subscription issueSubscription( uint64 id ) noexcept
        {
            Subscription subscription;
            subscription._pSource = this;
            subscription._id      = id;
            {
                ScopedLock<SpinLock> lock{ _listLock };
                subscription._pNext = _pHead;
                if ( _pHead != nullptr )
                    _pHead->_pPrev = &subscription;
                _pHead = &subscription;
            }
            // 반환이 옮기면(NRVO 가 아니면) 이동 생성자가 목록 자리를 새 객체로 고친다.
            return subscription;
        }

        /**
         * @brief @p subscription 이 이 출처가 내준 살아 있는 구독이면 그 id, 아니면 0 입니다. 등록에 주인 구독을 받는 출처가 씁니다.
         * @details 구독을 푸는 스레드와 같은 스레드에서 부릅니다(목록 잠금 없이 읽는다).
         */
        [[nodiscard]] uint64 findIssuedID( const Subscription& subscription ) const noexcept { return subscription._pSource == this ? subscription._id : 0; }

        /** @brief 구독 @p id 가 풀렸습니다. 그 등록을 지웁니다. 구독은 이미 목록에서 빠졌고 목록 잠금은 풀려 있습니다. */
        virtual void releaseSubscription( uint64 id ) = 0;

        /** @brief 살아 있는 구독을 모두 무효화합니다. 등록은 파생 클래스가 따로 지웁니다(전체 비우기 · 출처 소멸). */
        void detachAllSubscriptions() noexcept
        {
            ScopedLock<SpinLock> lock{ _listLock };
            Subscription*        pCurrent = _pHead;
            while ( pCurrent != nullptr )
            {
                Subscription* const pNext = pCurrent->_pNext;
                pCurrent->_pSource        = nullptr;
                pCurrent->_pPrev          = nullptr;
                pCurrent->_pNext          = nullptr;
                pCurrent                  = pNext;
            }
            _pHead = nullptr;
        }

    private:
        friend class Subscription;

        /** @brief @p subscription 을 목록에서 뺍니다. 목록 잠금을 쥐고 부릅니다. */
        void unlinkLocked( Subscription& subscription ) noexcept
        {
            if ( subscription._pPrev != nullptr )
                subscription._pPrev->_pNext = subscription._pNext;
            else
                _pHead = subscription._pNext;
            if ( subscription._pNext != nullptr )
                subscription._pNext->_pPrev = subscription._pPrev;
            subscription._pPrev = nullptr;
            subscription._pNext = nullptr;
        }

        /** @brief @p other 의 구독을 모두 이 객체로 옮깁니다. 이 객체의 목록은 비어 있어야 합니다. */
        void takeSubscriptionsFrom( SubscriptionSource& other ) noexcept
        {
            ScopedLockPair<SpinLock> lock{ _listLock, other._listLock };
            _pHead       = other._pHead;
            other._pHead = nullptr;
            for ( Subscription* pCurrent = _pHead; pCurrent != nullptr; pCurrent = pCurrent->_pNext )
            {
                pCurrent->_pSource = this;
            }
        }

        SpinLock      _listLock;         ///< 목록과 구독의 이웃 · 출처 칸을 지킨다
        Subscription* _pHead{ nullptr }; ///< 살아 있는 구독의 침입형 목록 머리
    };

    inline void Subscription::reset() noexcept
    {
        SubscriptionSource* const pSource = _pSource;
        if ( pSource == nullptr )
            return;
        {
            ScopedLock<SpinLock> lock{ pSource->_listLock };
            if ( _pSource == nullptr )
                return; // 잠금을 기다리는 사이 출처가 전체를 비웠다
            pSource->unlinkLocked( *this );
            _pSource = nullptr;
        }
        const uint64 id = _id;
        _id             = 0;
        pSource->releaseSubscription( id );
    }

    inline void Subscription::takeFrom( Subscription& other ) noexcept
    {
        SubscriptionSource* const pSource = other._pSource;
        if ( pSource == nullptr )
            return;
        ScopedLock<SpinLock> lock{ pSource->_listLock };
        _pSource = other._pSource;
        _pPrev   = other._pPrev;
        _pNext   = other._pNext;
        _id      = other._id;
        if ( _pSource != nullptr )
        {
            if ( _pPrev != nullptr )
                _pPrev->_pNext = this;
            else
                _pSource->_pHead = this;
            if ( _pNext != nullptr )
                _pNext->_pPrev = this;
        }
        other._pSource = nullptr;
        other._pPrev   = nullptr;
        other._pNext   = nullptr;
        other._id      = 0;
    }
} // namespace sw
