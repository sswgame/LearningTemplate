#include "pch.h"

#include "Engine/Graphics/RHI/Transfer/RHIStagingBudget.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    RHIStagingBudget::RHIStagingBudget()
        : _inFlightBytes{ 0 }
        , _reservedUntilMicroseconds{ 0 }
        , _budgetBytes{ 0 }
        , _wholeReservationMicroseconds{ 0 }
    {
    }

    void RHIStagingBudget::initialize( uint64 budgetBytes, int64 wholeReservationMicroseconds )
    {
        SW_ASSERT( budgetBytes > 0 );
        SW_ASSERT( wholeReservationMicroseconds >= 0 );
        _inFlightBytes.store( 0, std::memory_order_release );
        _reservedUntilMicroseconds.store( 0, std::memory_order_release );
        _budgetBytes                  = budgetBytes;
        _wholeReservationMicroseconds = wholeReservationMicroseconds;
    }

    bool RHIStagingBudget::tryReserve( uint64 requestBytes, int64 nowMicroseconds, uint64& outReservedBytes )
    {
        outReservedBytes           = 0;
        const uint64 reservedBytes = MathUtil::min( requestBytes, _budgetBytes );
        const bool   bWhole        = reservedBytes == _budgetBytes;

        if ( bWhole == false )
        {
            int64 reservedUntil = _reservedUntilMicroseconds.load( std::memory_order_acquire );
            if ( reservedUntil != 0 )
            {
                if ( nowMicroseconds < reservedUntil )
                    return false; // 예산을 통째로 잡아야 하는 요청이 기다린다 — 자리가 있어도 작은 요청은 받지 않아 복사 중인 것이 비게 한다
                // 큰 요청이 다시 오지 않았다(포기) — 작은 요청을 다시 받는다. 그새 연장된 예약은 지우지 않는다.
                _reservedUntilMicroseconds.compare_exchange_strong( reservedUntil, 0, std::memory_order_acq_rel, std::memory_order_acquire );
            }
        }

        uint64 inFlightBytes = _inFlightBytes.load( std::memory_order_acquire );
        while ( reservedBytes <= _budgetBytes - inFlightBytes )
        {
            if ( _inFlightBytes.compare_exchange_weak( inFlightBytes, inFlightBytes + reservedBytes, std::memory_order_acq_rel, std::memory_order_acquire ) )
            {
                if ( bWhole )
                    _reservedUntilMicroseconds.store( 0, std::memory_order_release ); // 기다리던 큰 요청이 들어왔다
                outReservedBytes = reservedBytes;
                return true;
            }
        }
        if ( bWhole && _wholeReservationMicroseconds > 0 )
            _reservedUntilMicroseconds.store( nowMicroseconds + _wholeReservationMicroseconds, std::memory_order_release );
        return false;
    }

    void RHIStagingBudget::release( uint64 reservedBytes )
    {
        [[maybe_unused]] const uint64 inFlightBefore = _inFlightBytes.fetch_sub( reservedBytes, std::memory_order_acq_rel );
        SW_ASSERT( reservedBytes <= inFlightBefore );
    }
} // namespace sw
