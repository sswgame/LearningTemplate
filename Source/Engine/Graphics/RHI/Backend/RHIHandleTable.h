/**
 * @file RHIHandleTable.h
 * @brief index|generation 불투명 핸들 테이블입니다. COM, Vk 포인터를 정수로 캐스팅하지 않습니다.
 * @details 놓은 칸은 프레임 경계를 두 번 지난 뒤에만 다시 쓴다(GPU 를 통째로 기다린 지점은 두 번으로 친다 — 프레임을 열지 않는 도구에서도 칸이 돌아온다). `get` 은 락이 없어 기록 워커 · 업로드 워커가 레코드 포인터를 쥔 채 쓰는데, 칸을 곧바로 다시 쓰면(LIFO)
 *          그 사이 들어온 새 자원의 레코드를 옛 핸들의 것으로 읽어 복사 · 배리어가 엉뚱한 자원에 갈 수 있다. 포인터는 한 프레임을 넘겨 쥐지 않으므로 두 번이면 넉넉하다.
 *          언리얼이 RHI 자원을 참조 계수로 쥐고 프레임 뒤에 놓는 것, Godot `RID_Owner` 가 놓은 칸의 검증 번호를 바꾸는 것과 같은 자리다(기억에 의존한 비교).
 */
#pragma once
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/SlotHandleTable.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    /**
     * @brief T 슬롯을 세대(generation)와 함께 보관합니다. 핸들 0 은 무효입니다.
     * @details 표는 자기 핸들 종류(@p Handle — `RHIBufferHandle` · `RHITextureHandle` · …)만 내고 받는다. 버퍼 표에 텍스처 핸들을 넣으면 컴파일이 멈춘다.
     *          장치의 경계 수(`IRHIDevice::getHandleReuseCount`)를 받아야 만들 수 있다 — 칸 다시 쓰기를 프레임에서 떼어 낼 수 없게.
     */
    template <typename Handle, typename T>
    class RHIHandleTable
    {
    public:
        /** @brief 놓은 칸을 다시 쓰기까지 지나야 하는 경계 수입니다 — 놓은 프레임의 기록이 끝나고 그 다음 프레임이 시작한 뒤다. */
        static constexpr uint64 kReuseDelayCount = constant::render::kHandleReuseDelayCount;

        /** @brief @p handleReuseCount 는 장치가 프레임을 열 때(1) · GPU 를 통째로 기다린 뒤(`kReuseDelayCount`) 오르는 수이고 표보다 오래 산다. */
        explicit RHIHandleTable( const atomic<uint64>& handleReuseCount )
            : _handleReuseCount{ handleReuseCount }
            , _table{}
            , _heldMutex{}
            , _listHeld{}
        {
        }

        /** @brief 값을 넣고 불투명 핸들을 반환합니다. */
        Handle insert( T value )
        {
            reuseHeldSlots();
            return Handle{ _table.insert( std::move( value ) ).packed() };
        }

        /** @brief 핸들이 유효하면 슬롯 포인터를, 아니면 nullptr 를 반환합니다. 받은 포인터는 그 프레임의 기록 안에서만 쥡니다. */
        T* get( Handle handle ) { return _table.get( SlotHandle::fromPacked( handle.getPacked() ) ); }

        /** @brief 핸들이 유효하면 슬롯 포인터를, 아니면 nullptr 를 반환합니다. 받은 포인터는 그 프레임의 기록 안에서만 쥡니다. */
        const T* get( Handle handle ) const { return _table.get( SlotHandle::fromPacked( handle.getPacked() ) ); }

        /** @brief 슬롯을 비우고 세대를 증가시킵니다. 꺼낸 값은 outValue 로 받고, 핸들이 유효했으면 true 를 반환합니다. 칸은 프레임 경계를 두 번 지난 뒤 다시 쓴다. */
        [[nodiscard]] bool take( Handle handle, T& outValue )
        {
            uint32 index = SlotHandleTable<T>::kInvalidIndex;
            if ( _table.takeAndHoldSlot( SlotHandle::fromPacked( handle.getPacked() ), outValue, index ) == false )
                return false;
            if ( index == SlotHandleTable<T>::kInvalidIndex )
                return true;
            ScopedLock<mutex> lock{ _heldMutex };
            _listHeld.push_back( HeldSlot{ _handleReuseCount.load( std::memory_order_acquire ), index } );
            return true;
        }

        /** @brief 핸들을 무효화합니다. 핸들이 유효했으면 true 입니다(장치의 같은 핸들 두 번 해제 검사가 본다). */
        bool erase( Handle handle )
        {
            T discarded{};
            return take( handle, discarded );
        }

        /** @brief 점유된 슬롯마다 fn(T&) 를 부릅니다. */
        template <typename Fn>
        void forEach( Fn&& fn ) { _table.forEach( std::forward<Fn>( fn ) ); }

        /** @brief 모든 슬롯을 비웁니다. */
        void clear() { _table.clear(); }

    private:
        /** @brief 놓은 칸 하나와 놓은 때의 경계 수입니다. */
        struct HeldSlot
        {
            uint64 _frameCount{ 0 };
            uint32 _index{ 0 };
        };

        /** @brief 프레임 경계를 다 지난 칸을 다시 내줄 수 있게 합니다(놓은 순서라 앞에서부터 본다). */
        void reuseHeldSlots()
        {
            const uint64      frameCount = _handleReuseCount.load( std::memory_order_acquire );
            ScopedLock<mutex> lock{ _heldMutex };
            size_t            readyCount = 0;
            while ( readyCount < _listHeld.size() && _listHeld[readyCount]._frameCount + kReuseDelayCount <= frameCount )
            {
                _table.reuseHeldSlot( _listHeld[readyCount]._index );
                ++readyCount;
            }
            if ( readyCount > 0 )
                _listHeld.erase( _listHeld.begin(), _listHeld.begin() + static_cast<ptrdiff_t>( readyCount ) );
        }

    private:
        const atomic<uint64>&      _handleReuseCount;
        SlotHandleTable<T>         _table;
        mutex                      _heldMutex;
        vector<HeldSlot> _listHeld SW_GUARDED_BY( _heldMutex ); ///< 놓은 순서
    };
} // namespace sw
