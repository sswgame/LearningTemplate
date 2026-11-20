/**
 * @file Core/Delegate/WeakRef.h
 * @brief 약한 참조(`WeakRef<T>`)와 그 출처(`WeakRefSource`)입니다 — 대상이 먼저 사라지면 참조가 스스로 비어, 날 포인터로 사라진 객체를 부르지 않습니다.
 * @details 수명이 다른 두 소유자 사이(엔진 UI 시스템이 소유한 화면 ↔ 게임 인스턴스 · 씬 · 컴포넌트가 소유한 모델)의 빌림에 쓴다.
 *          언리얼 `TWeakObjectPtr` · `TWeakPtr` 자리다. 대상은 전역 객체 표 없이 구독 목록(`SubscriptionList`)을 멤버로 들고, 소멸할 때 목록이 닫히며
 *          낸 구독을 모두 무효화한다 — 참조는 그 구독이 살아 있을 때만 대상 주소를 돌려준다.
 *
 *          class ChatLog
 *          {
 *          public:
 *              WeakRefSource& getWeakRefSource() { return _weakRefSource; }
 *          private:
 *              ...
 *              WeakRefSource _weakRefSource; // 맨 끝 멤버 — 먼저 닫혀, 다른 멤버가 사라지는 동안 참조가 대상을 돌려주지 않는다
 *          };
 *          WeakRef<ChatLog> model{ log };                     // 대상의 `getWeakRefSource()` 로 구독을 받는다
 *          if ( ChatLog* pModel = model.get() ) pModel->update();
 *
 * @note 스레드: `get` 은 대상의 소멸과 다른 스레드에서 겹치면 돌려준 주소가 곧 무효가 될 수 있다 — 대상과 같은 스레드(게임 스레드)에서 쓴다.
 */
#pragma once
#include "Core/Delegate/SubscriptionList.h"

namespace sw
{
    /**
     * @class WeakRefSource
     * @brief 약한 참조를 내주는 쪽이 멤버로 드는 출처입니다. 소멸하면 낸 참조가 모두 빕니다. 복사 · 이동하지 않습니다(참조가 이 주소의 주인을 가리킨다).
     */
    class WeakRefSource final
    {
    public:
        WeakRefSource() noexcept = default;

        WeakRefSource( const WeakRefSource& )            = delete;
        WeakRefSource& operator=( const WeakRefSource& ) = delete;
        WeakRefSource( WeakRefSource&& )                 = delete;
        WeakRefSource& operator=( WeakRefSource&& )      = delete;

    private:
        /** @brief 참조 하나의 수명 구독입니다(등록 내용이 없어 번호는 하나로 충분하다). */
        [[nodiscard]] Subscription issue() noexcept { return _list.issue( kReferenceID ); }

    private:
        template <typename T>
        friend class WeakRef;

        static constexpr uint64 kReferenceID = 1;
        SubscriptionList        _list{ SubscriptionList::ReleaseFunction{} };
    };

    /**
     * @class WeakRef
     * @brief 대상 @p T 의 약한 참조입니다. 대상이 사라지면 `get` 이 nullptr 입니다. 이동만 됩니다(구독이 이동만 된다).
     * @details @p T 는 `WeakRefSource& getWeakRefSource()` 를 내놓는다 — 대상과 출처를 따로 받지 않아 다른 객체의 출처를 짝짓는 실수가 없다.
     */
    template <typename T>
    class WeakRef
    {
    public:
        /** @brief 빈 참조입니다. */
        WeakRef() noexcept = default;

        /** @brief @p target 을 가리킵니다. */
        explicit WeakRef( T& target ) noexcept
            : _pTarget{ &target }
            , _lifetime{ target.getWeakRefSource().issue() }
        {
        }

        /** @brief 대상이 살아 있으면 그 주소, 비었거나 대상이 사라졌으면 nullptr 입니다. */
        [[nodiscard]] T* get() const noexcept { return _lifetime.isActive() ? _pTarget : nullptr; }
        /** @brief 비웁니다. */
        void reset() noexcept
        {
            _lifetime.reset();
            _pTarget = nullptr;
        }

    private:
        T*           _pTarget{ nullptr };
        Subscription _lifetime{}; ///< 대상 출처가 낸 구독 — 대상이 사라지면 무효가 된다
    };
} // namespace sw
