/**
 * @file LinearAllocator.h
 * @brief 빠른 프레임 · 블록 단위 메모리 할당자입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) LinearAllocator — 블록 안에서 선형으로 할당하고 reset 으로 재사용한다
    //    할당은 lock-free 오프셋, 새 블록 추가는 mutex
    // ------------------------------------------------------------------------------
    /**
     * @class LinearAllocator
     * @brief O(1) 선형 할당자입니다. reset 은 오프셋만 되돌리고, clear 는 블록을 해제합니다.
     *
     * @note 스레드 계약: allocate() 는 여러 스레드에서 동시에 불러도 안전합니다(블록 오프셋 CAS). 하지만 reset() / clear() 가
     *       진행 중인 allocate() 와 겹치면 안 됩니다. 겹치면 이미 내준 메모리를 다시 내줄 수 있습니다. 호출하는 쪽이 프레임
     *       경계 같은 외부 동기화로 allocate() 가 없을 때만 reset() / clear() 를 불러야 합니다.
     */
    class SW_API LinearAllocator
    {
    public:
        /** @brief 기본 용량(64KB)으로 할당기를 준비합니다. */
        LinearAllocator();
        /**
         * @brief 지정한 초기 용량으로 할당자를 준비합니다.
         * @param initialCapacity 초기 용량(바이트)
         */
        explicit LinearAllocator( size_t initialCapacity );
        /** @brief 소유한 블록을 해제합니다. */
        ~LinearAllocator();

        /** @brief 복사를 금지합니다. */
        LinearAllocator( const LinearAllocator& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        LinearAllocator& operator=( const LinearAllocator& ) = delete;

        /**
         * @brief 현재 블록에서 정렬된 메모리를 잘라 냅니다. 모자라면 새 블록을 붙입니다.
         * @param size 할당할 바이트 수
         * @param alignment 정렬 기준
         * @return 할당된 메모리 포인터(블록 슬롯이 바닥나거나 메모리가 부족하면 nullptr)
         */
        void* allocate( size_t size, size_t alignment = alignof( std::max_align_t ) );

        /**
         * @brief 모든 블록의 오프셋을 0 으로 되돌려 재사용합니다. 메모리는 그대로 둡니다.
         * @warning 동시에 실행 중인 allocate() 가 없어야 합니다(클래스의 스레드 계약 참고).
         */
        void reset();

        /**
         * @brief 확보해 둔 블록을 모두 해제합니다.
         * @warning 동시에 실행 중인 allocate() 가 없어야 합니다(클래스의 스레드 계약 참고).
         */
        void clear();

    private:
        /**
         * @brief 이미 확보해 둔 블록 중 requiredCapacity 를 담을 수 있는 빈 블록으로 바꿉니다.
         * @details reset() 은 오프셋만 되돌리므로, 그 뒤에는 앞서 잡아 둔 블록들이 비어 있습니다. 새 블록을 잡기 전에 그것부터
         *          씁니다. `_mutex` 를 잡은 채로 불러야 합니다.
         * @param fromBlockIndex 방금 가득 찬 블록의 인덱스. 그 **다음** 블록부터 찾습니다.
         * @param requiredCapacity 정렬 여유를 포함해 담아야 하는 바이트 수
         * @return 바꿨으면 true, 쓸 수 있는 블록이 없으면 false
         */
        bool advanceToHeldBlock( size_t fromBlockIndex, size_t requiredCapacity );

        /** @brief minCapacity 이상인 새 블록을 할당하고 현재 블록으로 바꿉니다. _mutex 를 잡은 채로 불러야 합니다. */
        bool allocateNewBlock( size_t minCapacity );

        /** @brief 블록 하나의 버퍼 · 용량 · 원자 오프셋입니다. */
        struct Block
        {
            uint8*         _pData{ nullptr };
            size_t         _capacity{ 0 };
            atomic<size_t> _offset{ 0 };
        };

        /**
         * @brief 블록 테이블의 슬롯 수입니다.
         * @details allocate() 가 락 없이 테이블을 읽으므로 저장소가 절대 재배치되면 안 됩니다. 블록 용량이 기하급수적으로 커지므로
         *          이 슬롯 수로 충분합니다.
         */
        static constexpr size_t kMaxBlockCount = 64;

    private:
        size_t         _defaultCapacity;
        atomic<size_t> _blockCount;
        atomic<size_t> _currentBlockIndex;
        atomic<Block*> _arrBlock[kMaxBlockCount];
        mutex          _mutex;
    };
} // namespace sw
