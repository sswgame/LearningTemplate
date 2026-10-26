/**
 * @file DelegateSlot.h
 * @brief 주인이 하나뿐인 콜백 칸입니다. 거는 쪽이 `Subscription` 을 받아 쥐고, 놓으면 칸이 빕니다.
 * @details `setXxxHook( delegate )` 처럼 칸 하나를 대입하는 setter 는 나중에 쓴 쪽이 앞의 것을 말없이 덮어쓰고, 떼는 쪽은 `{}` 를 대입하다가
 *          남이 건 것까지 지운다. 이 칸은 비어 있을 때만 걸리고(이미 차 있으면 단언 — 주인이 둘이라는 결함) 건 구독이 풀릴 때만 빈다.
 *          칸 자체가 먼저 사라지면 구독은 무효가 되어 아무것도 풀지 않는다(`SubscriptionSource`).
 *          여러 쪽이 함께 들어야 하는 알림은 이것이 아니라 `MulticastDelegate` 를 쓴다.
 * @note 스레드: 거는 쪽 · 부르는 쪽이 같은 스레드여야 합니다. 다른 스레드가 잠금 없이 읽는 훅(렌더 스레드의 Present 훅 · 태스크 프로파일 훅)은 이 칸으로 바꾸지 않습니다 —
 *       구독이 풀리는 쓰기가 읽기와 겹칠 수 있다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Delegate/Subscription.h"

namespace sw
{
    /**
     * @class DelegateSlot
     * @brief 델리게이트(@p TDelegate — `Delegate<…>` 이나 그 별칭) 하나를 담는 칸입니다. `bind` 가 구독을 돌려주고, 그 구독이 풀리면 칸이 빕니다.
     */
    template <typename TDelegate>
    class DelegateSlot final : public SubscriptionSource
    {
    public:
        using delegate_type = TDelegate;

        DelegateSlot()                                 = default;
        DelegateSlot( const DelegateSlot& )            = delete;
        DelegateSlot& operator=( const DelegateSlot& ) = delete;

        /** @brief 칸에 @p delegate 를 겁니다. 칸이 이미 차 있으면 부른 쪽의 결함이라 단언합니다(주인이 둘이다). */
        [[nodiscard]] Subscription bind( delegate_type delegate )
        {
            SW_ASSERT( _delegate.isBound() == false );
            _delegate = std::move( delegate );
            return issueSubscription( ++_bindingID );
        }

        /** @brief 칸이 차 있으면 true 입니다. */
        bool isBound() const { return _delegate.isBound(); }
        /** @brief 걸린 델리게이트입니다(비어 있으면 묶이지 않은 델리게이트). */
        const delegate_type& get() const { return _delegate; }

        /**
         * @brief 걸린 델리게이트의 호출 스텁이 [@p pBegin, @p pEnd) 안이면 칸을 비우고 true 입니다(모듈 이미지 언로드 훑기).
         * @details 그 구독은 이제 아무것도 풀지 않는다 — 주인 객체는 함께 내려가는 모듈 안에 있어 놓을 기회가 없을 수 있다.
         */
        bool releaseIfCodeWithin( const void* pBegin, const void* pEnd )
        {
            if ( _delegate.isCodeWithin( pBegin, pEnd ) == false )
                return false;
            _delegate = delegate_type{};
            ++_bindingID;
            return true;
        }

    protected:
        void releaseSubscription( uint64 id ) override
        {
            // 지금 건 것의 구독만 칸을 비운다(훑기가 비운 뒤 다시 건 것을 앞 구독이 지우지 않게).
            if ( id == _bindingID )
                _delegate = delegate_type{};
        }

    private:
        delegate_type _delegate{};
        uint64        _bindingID{ 0 }; ///< 지금 칸을 쥔 구독의 번호
    };
} // namespace sw
