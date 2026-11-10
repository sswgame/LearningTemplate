/**
 * @file RHITrackedState.h
 * @brief 텍스처 · 버퍼 레코드의 추적 상태(DX12 리소스 상태 · Vulkan 이미지 레이아웃)를 락 없이 바꾸는 원자값입니다.
 * @details 예전에는 상태를 해시 맵이나 레코드에 두고 배리어마다 디바이스 전역 뮤텍스(DX12 `_resourceStateMutex` · Vulkan `_imageLayoutMutex`)를 잡았다.
 *          상태가 핸들 테이블 칸에 있으면 찾기가 칸 번호이고, 같은 레벨의 두 패스가 같은 자원을 같은 상태로 옮기려 하면 `transitionTo` 의 비교 교환이
 *          하나에게만 배리어를 맡긴다(락과 같은 보장 — 리스트의 제출 순서는 락도 정하지 않았다). 언리얼은 RDG 가 전이를 모아 내므로 자원마다 락이 없다(기억에 의존한 비교).
 *          핸들 테이블이 칸을 옮기므로(넣기 · 꺼내기, 그동안 다른 스레드는 그 칸을 보지 않는다) 복사는 값을 읽어 옮긴다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @brief 32 비트로 담기는 상태 값(@p TState — 열거형 또는 정수) 하나입니다. 기본값은 `TState{}`(DX12 COMMON · Vulkan UNDEFINED 가 모두 0)이다.
     */
    template <typename TState>
    class RHITrackedState
    {
        static_assert( sizeof( TState ) <= sizeof( uint32 ), "RHITrackedState holds a 32-bit state" );

    public:
        RHITrackedState()
            : _value{ static_cast<uint32>( TState{} ) }
        {
        }
        RHITrackedState( const RHITrackedState& other )
            : _value{ other._value.load( std::memory_order_relaxed ) }
        {
        }
        RHITrackedState& operator=( const RHITrackedState& other )
        {
            _value.store( other._value.load( std::memory_order_relaxed ), std::memory_order_relaxed );
            return *this;
        }
        /** @brief 지금 상태입니다. */
        TState load() const { return static_cast<TState>( _value.load( std::memory_order_relaxed ) ); }
        /** @brief 상태를 덮어씁니다(배리어를 이미 낸 쪽 — 복사 전이 · 업로드 · 버린 프레임 되돌리기). */
        void store( TState state ) { _value.store( static_cast<uint32>( state ), std::memory_order_relaxed ); }
        /**
         * @brief 상태를 @p desired 로 바꿉니다. 바꿨으면 true 이고 @p outBefore 에 옛 상태를 담는다 — 그 배리어는 부른 쪽이 낸다.
         *        이미 @p desired 면 false 다(다른 워커가 먼저 옮겼거나 옮길 것이 없다).
         */
        bool transitionTo( TState desired, TState& outBefore )
        {
            uint32 current = _value.load( std::memory_order_relaxed );
            do
            {
                if ( current == static_cast<uint32>( desired ) )
                    return false;
            } while ( _value.compare_exchange_weak( current, static_cast<uint32>( desired ), std::memory_order_relaxed ) == false );
            outBefore = static_cast<TState>( current );
            return true;
        }

    private:
        atomic<uint32> _value;
    };
} // namespace sw
