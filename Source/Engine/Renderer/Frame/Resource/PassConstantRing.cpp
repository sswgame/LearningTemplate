#include "pch.h"

#include "Engine/Renderer/Frame/Resource/PassConstantRing.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

namespace sw
{
    SW_LOG_CALLER( "PassConstantRing" );

    PassConstantRing::PassConstantRing()
        : _listSlot{}
        , _cursor{ 0 }
        , _highWater{ 0 }
        , _bExhaustedLogged{ SW_FALSE }
    {
    }

    bool PassConstantRing::appendSlot( IRHIDevice* pDevice )
    {
        RHIConstantBufferSlot slot{};
        if ( slot.create( pDevice, kSlotBytes ) == false )
            return false;
        _listSlot.push_back( slot );
        return true;
    }

    bool PassConstantRing::initialize( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr || pDevice->getResourceFactory() == nullptr )
            return false;
        if ( _listSlot.empty() == false )
            return true;

        _listSlot.reserve( kInitialSlotCount );
        for ( uint32 slotIndex = 0; slotIndex < kInitialSlotCount; ++slotIndex )
        {
            if ( appendSlot( pDevice ) == false )
                break;
        }
        _cursor.store( 0, std::memory_order_relaxed );
        return _listSlot.empty() == false;
    }

    void PassConstantRing::release( IRHIDevice* pDevice )
    {
        for ( RHIConstantBufferSlot& slot : _listSlot )
        {
            slot.release( pDevice );
        }
        forget();
    }

    void PassConstantRing::forget()
    {
        _listSlot.clear();
        _cursor.store( 0, std::memory_order_relaxed );
    }

    void PassConstantRing::beginFrame()
    {
        // 0번은 프레임 시드 전용이라 패스에는 1번부터 나눠 준다.
        _cursor.store( 1, std::memory_order_relaxed );
        _bExhaustedLogged.store( SW_FALSE );
    }

    bool PassConstantRing::acquire( RHIBufferHandle& outBuffer, RHIDescriptorIndex& outIndex )
    {
        if ( _listSlot.empty() )
            return false;

        uint32 ticket = _cursor.fetch_add( 1, std::memory_order_relaxed );

        // 다음 프레임 용량 산정용 최댓값. 단조 증가라 한 번 커진 용량은 줄지 않는다.
        uint32 previousHigh = _highWater.load( std::memory_order_relaxed );
        while ( previousHigh < ticket + 1 &&
                _highWater.compare_exchange_weak( previousHigh, ticket + 1, std::memory_order_relaxed, std::memory_order_relaxed ) == false )
        {
        }

        if ( ticket >= static_cast<uint32>( _listSlot.size() ) )
        {
            // 슬롯이 모자라면 빌려주지 않는다 — 앞 슬롯을 공유하면 그 프레임의 배치 상수가 조용히 섞인다. 부르는 쪽은 그 패스 · 타일을 그리지 않는다.
            // 병렬 기록 중이라 여기서 늘릴 수 없다. 다음 프레임은 _highWater 로 키운다(ensurePassCbCapacityForFrame).
            // 오류는 프레임당 한 번만 남긴다. 드로우마다 찍으면 로그가 잠긴다.
            if ( _bExhaustedLogged.exchange( SW_TRUE ) == SW_FALSE )
            {
                SW_LOG_ERROR( "Out of constant buffer slots (%#) - the remaining passes of this frame that need a slot are not drawn; the next frame grows the ring",
                              static_cast<uint32>( _listSlot.size() ) );
            }
            return false;
        }

        // 분배는 위의 atomic 커서가 하므로 락이 필요 없다. 다만 **const 로 읽어야** 한다. 비-const 접근은
        // "쓰기"로 취급되어, 서로 다른 슬롯을 읽기만 하는 드로우 둘도 레이스로 잡힌다.
        const vector<RHIConstantBufferSlot>& listSlot = _listSlot;
        outBuffer                                     = listSlot[ticket]._buffer;
        outIndex                                      = listSlot[ticket]._index;
        return true;
    }

    void PassConstantRing::ensureCapacity( IRHIDevice* pDevice, uint32 needed )
    {
        if ( pDevice == nullptr || pDevice->getResourceFactory() == nullptr )
            return;
        const uint32 target = MathUtil::min( needed, kMaxSlotCount );
        if ( static_cast<uint32>( _listSlot.size() ) >= target )
            return;

        _listSlot.reserve( target );
        while ( static_cast<uint32>( _listSlot.size() ) < target )
        {
            if ( appendSlot( pDevice ) == false )
                break;
        }
    }

    bool PassConstantRing::getSeedSlot( RHIBufferHandle& outBuffer, RHIDescriptorIndex& outIndex ) const
    {
        if ( _listSlot.empty() )
            return false;
        outBuffer = _listSlot[0]._buffer;
        outIndex  = _listSlot[0]._index;
        return true;
    }
} // namespace sw
