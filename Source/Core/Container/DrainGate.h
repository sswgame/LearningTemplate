/**
 * @file DrainGate.h
 * @brief 비우며 꺼내는 큐(알림 버퍼)의 꺼내는 쪽을 하나로 정하는 소유권 표(`DrainGate`)와 그 표(`DrainLease`)입니다.
 * @details 알림 버퍼는 꺼내면 비므로 두 곳이 꺼내면 서로의 알림을 가져간다. 그래서 버퍼를 가진 쪽(온라인 클라이언트 · `EventBuffer` 를 쓰는 규칙 모델)이 게이트를 두고, 꺼내는 쪽은 `claim` 으로 얻은 표를 들고 있어야만 꺼낼 수 있다.
 *          이미 다른 표가 있으면 `claim` 이 false 이므로 두 번째 붙이기는 호출한 쪽이 오류로 알게 된다. 표는 복사 · 이동하지 않고, 클라이언트와 표 중 어느 쪽이 먼저 소멸해도 서로를 안전하게 놓는다.
 *          한 스레드(게임 스레드)용이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    class DrainGate;

    /**
     * @class DrainLease
     * @brief 게이트의 꺼내기 권한을 쥔 표입니다. 꺼내는 쪽(모델)이 멤버로 둡니다.
     */
    class DrainLease
    {
    public:
        DrainLease() = default;
        ~DrainLease() { release(); }
        DrainLease( const DrainLease& )            = delete;
        DrainLease& operator=( const DrainLease& ) = delete;

        /** @brief 쥔 권한을 놓습니다. 쥔 것이 없으면 아무것도 하지 않습니다. */
        void release();
        bool isHeld() const { return _pGate != nullptr; }

    private:
        friend class DrainGate;
        DrainGate* _pGate{ nullptr };
    };
} // namespace sw

namespace sw
{
    /**
     * @class DrainGate
     * @brief 꺼내기 권한을 한 표에만 줍니다.
     */
    class DrainGate
    {
    public:
        DrainGate() = default;
        ~DrainGate()
        {
            if ( _pHolder != nullptr )
                _pHolder->_pGate = nullptr;
        }
        DrainGate( const DrainGate& )            = delete;
        DrainGate& operator=( const DrainGate& ) = delete;

        /**
         * @brief @p lease 에 꺼내기 권한을 줍니다. 이미 @p lease 가 쥐고 있으면 true 이고, 다른 표가 쥐고 있으면 false 입니다(어서션도 남깁니다).
         * @details @p lease 가 다른 게이트의 권한을 쥐고 있으면 그것은 먼저 놓습니다.
         */
        [[nodiscard]] bool claim( DrainLease& lease )
        {
            if ( _pHolder == &lease )
                return true;
            if ( _pHolder != nullptr )
            {
                SW_ASSERT( false && "A drain queue can be drained by one holder only" );
                return false;
            }
            lease.release();
            _pHolder     = &lease;
            lease._pGate = this;
            return true;
        }
        /** @brief @p lease 가 이 게이트의 권한을 쥐고 있는지입니다. */
        bool isHeldBy( const DrainLease& lease ) const { return _pHolder == &lease; }
        /** @brief 누군가 권한을 쥐고 있는지입니다. */
        bool hasHolder() const { return _pHolder != nullptr; }

    private:
        friend class DrainLease;
        DrainLease* _pHolder{ nullptr };
    };

    inline void DrainLease::release()
    {
        if ( _pGate == nullptr )
            return;
        _pGate->_pHolder = nullptr;
        _pGate           = nullptr;
    }
} // namespace sw
