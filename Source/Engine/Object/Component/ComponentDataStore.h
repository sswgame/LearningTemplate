/**
 * @file ComponentDataStore.h
 * @brief 컴포넌트 핫 데이터를 종류마다 연속 배열(SoA 페이지)로 두는 칸 저장소 틀입니다. 컴포넌트는 칸 번호와 페이지 포인터만 가집니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/IndexFreeList.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @class ComponentDataStore
     * @brief 페이지 @p TPage 를 칸 번호로 나눠 주는 저장소입니다(트랜스폼 · 물리 바디 자세 · 명령이 같은 틀을 쓴다).
     * @details **페이지 계약.** `TPage` 는 다음을 가진다:
     *          - `kSlotShift` · `kSlotCount`(= 1 << kSlotShift) · `kSlotMask` — 페이지 안 칸 수.
     *          - `using Owner = ...;` — 칸을 받는 컴포넌트 형식.
     *          - `void initializeSlot( uint32 pageIndex, Owner* pOwner )` — 받은 칸을 기본값으로(칸을 받은 호출자 스레드에서, 잠금 밖).
     *          - `void releaseSlot( uint32 pageIndex )` — 놓은 칸이 누구의 것으로도 읽히지 않게(놓는 호출자 스레드에서, 잠금 밖).
     *
     *          페이지는 한 번 만들면 옮기지도 해제하지도 않는다(저장소가 소멸할 때까지) — 컴포넌트는 자기 페이지 포인터를 들고 표 없이 찾는다.
     *          칸 받기 · 놓기는 뮤텍스 안이고(씬 로드 워커와 게임 스레드가 함께 만든다), 페이지 표는 고정 크기 + release 발행이라 `findPage` 는 잠금이 없다.
     *          칸 **내용**의 동기화는 쓰는 쪽(씬의 틱 · 물리 단계)이 맡는다.
     *
     *          유니티 `TransformHierarchy` · Entities 청크와 언리얼 Mass 프래그먼트 배열 자리다 — 오브젝트 모델(GameObject · 컴포넌트)은 그대로 두고
     *          프레임마다 도는 값만 종류별 배열로 옮긴다(5-3 하이브리드). 칸 번호가 컴포넌트 수명 동안 바뀌지 않아야 대기 목록 · 레지스트리가 번호만 들 수 있어
     *          씬마다가 아니라 서비스 하나에 둔다.
     */
    template <typename TPage, uint32 TMaxPageCount>
    class ComponentDataStore
    {
    public:
        using Page  = TPage;
        using Owner = typename TPage::Owner;

        /** @brief 칸이 없음을 나타냅니다. */
        static constexpr uint32 kInvalidSlot = constant::invalid_index::kUint32;
        /** @brief 페이지 표의 칸 수입니다. 칸 상한은 이 수 × `TPage::kSlotCount` 입니다. */
        static constexpr uint32 kMaxPageCount = TMaxPageCount;

        ComponentDataStore()
            : _arrPage{}
            , _freeSlot{}
            , _nextSlot{ 0 }
            , _liveSlotCount{ 0 }
            , _mutex{}
        {
        }
        /** @brief 페이지를 모두 해제합니다. 남은 칸 경고는 쓰는 쪽(서비스)이 자기 이름으로 낸다(`getLiveSlotCount`). */
        ~ComponentDataStore()
        {
            for ( atomic<TPage*>& page : _arrPage )
            {
                sw_delete( page.load( std::memory_order_relaxed ) );
                page.store( nullptr, std::memory_order_relaxed );
            }
        }

        ComponentDataStore( const ComponentDataStore& )            = delete;
        ComponentDataStore& operator=( const ComponentDataStore& ) = delete;

        /**
         * @brief 빈 칸 하나를 받아 페이지의 기본값으로 채웁니다. 어느 스레드에서 불러도 됩니다.
         * @param pOwner   칸을 쓸 컴포넌트
         * @param pOutPage 칸이 들어 있는 페이지. 컴포넌트가 들고 있다가 표 없이 찾습니다
         * @return 칸 번호
         */
        uint32 allocateSlot( Owner* pOwner, TPage*& pOutPage )
        {
            uint32 slot  = kInvalidSlot;
            TPage* pPage = nullptr;
            {
                ScopedLock<mutex> lock{ _mutex };
                if ( _freeSlot.tryPop( slot ) == false )
                {
                    // 닿지 않는 한계다(`kMaxPageCount`). 닿았다면 칸 없이 컴포넌트를 만들 방법이 없다 — 알리고 멈춘다.
                    if ( _nextSlot >= kMaxPageCount * TPage::kSlotCount )
                        SW_FATAL( "Component data store is full (%# slots). Too many components are alive.", _nextSlot );
                    slot = _nextSlot++;
                }

                // 페이지는 처음 닿을 때 만들어 release 로 발행한다. 칸 번호는 이 락을 나간 뒤에야 바깥에 알려지므로, 번호를 받은 쪽은 페이지를 본다.
                atomic<TPage*>& pageEntry = _arrPage[slot >> TPage::kSlotShift];
                pPage                     = pageEntry.load( std::memory_order_relaxed );
                if ( pPage == nullptr )
                {
                    pPage = make_unique<TPage>().release(); // 락 없이 읽는 표에 올리는 자리에서만 소유를 놓는다
                    pageEntry.store( pPage, std::memory_order_release );
                }
                ++_liveSlotCount;
            }

            // 칸은 이제 이 호출자 것이다. 채우는 데 락이 필요 없다.
            pPage->initializeSlot( slot & TPage::kSlotMask, pOwner );
            pOutPage = pPage;
            return slot;
        }

        /** @brief 칸을 반환합니다. 어느 스레드에서 불러도 됩니다. `kInvalidSlot` 이면 아무것도 하지 않습니다. */
        void freeSlot( uint32 slot )
        {
            if ( slot == kInvalidSlot )
                return;
            TPage* pPage = findPage( slot );
            if ( pPage == nullptr )
                return;
            pPage->releaseSlot( slot & TPage::kSlotMask );

            ScopedLock<mutex> lock{ _mutex };
            _freeSlot.push( slot );
            --_liveSlotCount;
        }

        /** @brief 칸이 들어 있는 페이지를 반환합니다. 잠금이 없습니다. 받은 적 없는 번호면 nullptr 일 수 있습니다. */
        TPage* findPage( uint32 slot ) const
        {
            const uint32 pageNumber = slot >> TPage::kSlotShift;
            return pageNumber < kMaxPageCount ? _arrPage[pageNumber].load( std::memory_order_acquire ) : nullptr;
        }

        /** @brief 지금 쓰이는 칸 수입니다(테스트와 진단, 소멸 때 샌 칸 경고). */
        uint32 getLiveSlotCount() const
        {
            ScopedLock<mutex> lock{ _mutex };
            return _liveSlotCount;
        }

    private:
        /** @brief 페이지 표입니다. 한 번 발행한 페이지는 저장소가 소멸할 때까지 그 위치에 있습니다. */
        atomic<TPage*> _arrPage[kMaxPageCount];
        /** @brief 반환된 칸입니다. 나중에 반환된 것부터 다시 씁니다. */
        IndexFreeList _freeSlot SW_GUARDED_BY( _mutex );
        /** @brief 한 번도 쓰지 않은 칸의 첫 번호입니다. */
        uint32 _nextSlot SW_GUARDED_BY( _mutex );
        /** @brief 지금 쓰이는 칸 수입니다. */
        uint32 _liveSlotCount SW_GUARDED_BY( _mutex );
        /** @brief 칸 받기, 반환, 페이지 만들기를 보호합니다. */
        mutable mutex _mutex;
    };
} // namespace sw
