/**
 * @file OnlineDrainGate.h
 * @brief 클라이언트의 알림 버퍼를 꺼내는 쪽을 하나로 정하는 소유권 표(`OnlineDrainGate`)와 그 표(`OnlineDrainLease`)입니다.
 * @details 알림 버퍼는 꺼내면 비므로 두 곳이 꺼내면 서로의 알림을 가져간다. 그래서 버퍼를 가진 클라이언트가 게이트를 두고, 꺼내는 쪽은 `claim` 으로 얻은 표를 들고 있어야만 꺼낼 수 있다.
 *          이미 다른 표가 있으면 `claim` 이 false 이므로 두 번째 붙이기는 호출한 쪽이 오류로 알게 된다. 표는 복사 · 이동하지 않고, 클라이언트와 표 중 어느 쪽이 먼저 소멸해도 서로를 안전하게 놓는다.
 *          한 스레드(게임 스레드)용이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    class OnlineDrainGate;

    /**
     * @class OnlineDrainLease
     * @brief 게이트의 꺼내기 권한을 쥔 표입니다. 꺼내는 쪽(모델)이 멤버로 둡니다.
     */
    class OnlineDrainLease
    {
    public:
        OnlineDrainLease() = default;
        ~OnlineDrainLease() { release(); }
        OnlineDrainLease( const OnlineDrainLease& )            = delete;
        OnlineDrainLease& operator=( const OnlineDrainLease& ) = delete;

        /** @brief 쥔 권한을 놓습니다. 쥔 것이 없으면 아무것도 하지 않습니다. */
        void release();
        bool isHeld() const { return _pGate != nullptr; }

    private:
        friend class OnlineDrainGate;
        OnlineDrainGate* _pGate{ nullptr };
    };
} // namespace sw

namespace sw
{
    /**
     * @class OnlineDrainGate
     * @brief 꺼내기 권한을 한 표에만 줍니다.
     */
    class OnlineDrainGate
    {
    public:
        OnlineDrainGate() = default;
        ~OnlineDrainGate()
        {
            if ( _pHolder != nullptr )
                _pHolder->_pGate = nullptr;
        }
        OnlineDrainGate( const OnlineDrainGate& )            = delete;
        OnlineDrainGate& operator=( const OnlineDrainGate& ) = delete;

        /**
         * @brief @p lease 에 꺼내기 권한을 줍니다. 이미 @p lease 가 쥐고 있으면 true 이고, 다른 표가 쥐고 있으면 false 입니다(어서션도 남깁니다).
         * @details @p lease 가 다른 게이트의 권한을 쥐고 있으면 그것은 먼저 놓습니다.
         */
        [[nodiscard]] bool claim( OnlineDrainLease& lease )
        {
            if ( _pHolder == &lease )
                return true;
            if ( _pHolder != nullptr )
            {
                SW_ASSERT( false && "A client's notification buffer can be drained by one holder only" );
                return false;
            }
            lease.release();
            _pHolder     = &lease;
            lease._pGate = this;
            return true;
        }
        /** @brief @p lease 가 이 게이트의 권한을 쥐고 있는지입니다. */
        bool isHeldBy( const OnlineDrainLease& lease ) const { return _pHolder == &lease; }

    private:
        friend class OnlineDrainLease;
        OnlineDrainLease* _pHolder{ nullptr };
    };

    inline void OnlineDrainLease::release()
    {
        if ( _pGate == nullptr )
            return;
        _pGate->_pHolder = nullptr;
        _pGate           = nullptr;
    }
} // namespace sw
