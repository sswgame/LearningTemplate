/**
 * @file RHITrackedState.h
 * @brief 텍스처 · 버퍼 레코드의 추적 상태(DX12 리소스 상태 · Vulkan 이미지 레이아웃)를 락 없이 바꾸는 원자값입니다.
 * @details 예전에는 상태를 해시 맵이나 레코드에 두고 배리어마다 디바이스 전역 뮤텍스(DX12 `_resourceStateMutex` · Vulkan `_imageLayoutMutex`)를 잡았다.
 *          상태가 핸들 테이블 칸에 있으면 찾기가 칸 번호이고, 같은 레벨의 두 패스가 같은 자원을 같은 상태로 옮기려 하면 `transitionTo` 의 비교 교환이
 *          하나에게만 배리어를 맡긴다(락과 같은 보장 — 리스트의 제출 순서는 락도 정하지 않았다). 언리얼은 RDG 가 전이를 모아 내므로 자원마다 락이 없다(기억에 의존한 비교).
 *          핸들 테이블이 칸을 옮기므로(넣기 · 꺼내기, 그동안 다른 스레드는 그 칸을 보지 않는다) 복사는 값을 읽어 옮긴다.
 */
#pragma once
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
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

    /**
     * @struct RHISubresourceRange
     * @brief 텍스처의 밉 · 면(배열 칸) 구간입니다. 개수 `kAll` 은 "처음부터 끝까지" 다(기본값 — 자원 전체).
     * @details 언리얼 `FRHITransitionInfo` 의 MipIndex · ArraySlice(전부는 `AllSubresources`), Godot RD 의 슬라이스 범위와 같은 자리다.
     */
    struct RHISubresourceRange
    {
        static constexpr uint16 kAll = 0xFFFFu;

        uint16 _firstMip{ 0 };
        uint16 _mipCount{ kAll };
        uint16 _firstSlice{ 0 };
        uint16 _sliceCount{ kAll };

        /** @brief 밉 하나 · 면 하나입니다. */
        static constexpr RHISubresourceRange makeSingle( uint16 mip, uint16 slice ) { return RHISubresourceRange{ mip, 1u, slice, 1u }; }
    };

    /**
     * @class RHITrackedSubresourceState
     * @brief 텍스처의 추적 상태를 서브리소스(밉 × 면) 단위로 듭니다. 모두 같으면 값 하나, 갈라지면 서브리소스마다 하나입니다(R-10).
     * @details 예전 `RHITrackedState` 는 자원 하나에 값 하나라 밉 하나만 렌더 타깃으로 쓰는 다운샘플 체인 · 면 하나에 그리는 큐브는 전체를 옮겨야 했다.
     *          언리얼 `FD3D12ResourceState`(전체 + 필요하면 서브리소스 배열)와 같은 모양이다. 서브리소스 번호는 D3D12 `D3D12CalcSubresource` 와 같은 `mip + slice * mipCount` 다.
     *          배리어를 낼 쪽 하나만 정하는 보장은 `RHITrackedState` 와 같다 — 비교 교환 대신 칸마다 스핀락이다(같은 자원을 두 워커가 옮기는 일은 드물다).
     *          핸들 테이블이 칸을 옮기므로 복사는 값을 읽어 옮긴다.
     */
    template <typename TState>
    class RHITrackedSubresourceState
    {
        static_assert( sizeof( TState ) <= sizeof( uint32 ), "RHITrackedSubresourceState holds 32-bit states" );

    public:
        /** @brief 서브리소스 번호 자리에 이 값이 오면 "자원 전체" 다(D3D12 `ALL_SUBRESOURCES` 와 같은 뜻). */
        static constexpr uint32 kWholeResource = 0xFFFFFFFFu;

        RHITrackedSubresourceState() = default;
        RHITrackedSubresourceState( const RHITrackedSubresourceState& other ) { copyFrom( other ); }
        RHITrackedSubresourceState& operator=( const RHITrackedSubresourceState& other )
        {
            if ( this != &other )
                copyFrom( other );
            return *this;
        }

        /** @brief 밉 수 · 면 수를 정하고 전체를 @p state 로 둡니다. 자원을 만들 때 한 번 부른다. */
        void initialize( uint16 mipCount, uint16 sliceCount, TState state )
        {
            ScopedLock<SpinLock> lock{ _lock };
            _mipCount   = mipCount > 0 ? mipCount : 1u;
            _sliceCount = sliceCount > 0 ? sliceCount : 1u;
            _whole      = state;
            _listState.clear();
        }

        /** @brief 모든 서브리소스가 같은 상태면 그 값과 true, 갈라졌으면 false 입니다. */
        [[nodiscard]] bool tryLoadWhole( TState& outState ) const
        {
            ScopedLock<SpinLock> lock{ _lock };
            outState = _whole;
            return _listState.empty();
        }

        /** @brief 전체를 @p state 로 덮어씁니다(배리어를 이미 낸 쪽). */
        void storeWhole( TState state )
        {
            ScopedLock<SpinLock> lock{ _lock };
            _whole = state;
            _listState.clear();
        }

        /**
         * @brief @p range 를 @p desired 로 옮기고, 낼 배리어마다 @p emit( subresourceIndex, before ) 를 부릅니다. 낸 수를 돌려줍니다.
         * @details 모두 같은 상태에서 전체를 옮기면 배리어 하나(`kWholeResource`)다. 일부만 옮기면 서브리소스마다 하나이고, 옮긴 뒤 모두 같아지면 값 하나로 접는다.
         *          이미 @p desired 인 서브리소스는 배리어를 내지 않는다(다른 워커가 먼저 옮겼다).
         */
        template <typename FnEmit>
        uint32 transitionTo( const RHISubresourceRange& range, TState desired, FnEmit&& emit )
        {
            ScopedLock<SpinLock> lock{ _lock };
            const uint32         mipEnd   = clampEnd( range._firstMip, range._mipCount, _mipCount );
            const uint32         sliceEnd = clampEnd( range._firstSlice, range._sliceCount, _sliceCount );
            const bool           bWhole   = range._firstMip == 0 && mipEnd == _mipCount && range._firstSlice == 0 && sliceEnd == _sliceCount;
            if ( _listState.empty() )
            {
                if ( _whole == desired )
                    return 0;
                if ( bWhole )
                {
                    emit( kWholeResource, _whole );
                    _whole = desired;
                    return 1;
                }
                _listState.assign( static_cast<size_t>( _mipCount ) * _sliceCount, _whole );
            }
            uint32 emitCount{ 0 };
            for ( uint32 slice = range._firstSlice; slice < sliceEnd; ++slice )
            {
                for ( uint32 mip = range._firstMip; mip < mipEnd; ++mip )
                {
                    const uint32 subresource = mip + slice * _mipCount;
                    TState&      state       = _listState[subresource];
                    if ( state == desired )
                        continue;
                    emit( subresource, state );
                    state = desired;
                    ++emitCount;
                }
            }
            collapseIfUniform();
            return emitCount;
        }

        /** @brief 서브리소스 하나의 상태입니다(진단 · 테스트). */
        TState loadSubresource( uint32 subresource ) const
        {
            ScopedLock<SpinLock> lock{ _lock };
            return _listState.empty() || subresource >= _listState.size() ? _whole : _listState[subresource];
        }

    private:
        static uint32 clampEnd( uint16 first, uint16 count, uint16 total )
        {
            const uint32 end = count == RHISubresourceRange::kAll ? total : static_cast<uint32>( first ) + count;
            return end < total ? end : total;
        }

        void collapseIfUniform()
        {
            for ( const TState state : _listState )
            {
                if ( state != _listState[0] )
                    return;
            }
            _whole = _listState[0];
            _listState.clear();
        }

        void copyFrom( const RHITrackedSubresourceState& other )
        {
            ScopedLock<SpinLock> otherLock{ other._lock };
            _mipCount   = other._mipCount;
            _sliceCount = other._sliceCount;
            _whole      = other._whole;
            _listState  = other._listState;
        }

    private:
        mutable SpinLock _lock;
        uint16           _mipCount{ 1 };
        uint16           _sliceCount{ 1 };
        TState           _whole{};
        vector<TState>   _listState; ///< 갈라졌을 때만 서브리소스마다 하나(`mip + slice * _mipCount`). 모두 같으면 비운다
    };
} // namespace sw
