/**
 * @file LinearAllocator.h
 * @brief 고속 프레임/블록 메모리 할당기
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
    // 1) LinearAllocator — 블록 선형 할당, reset 으로 재사용
    //    할당은 lock-free 오프셋, 새 블록 추가는 mutex
    // ------------------------------------------------------------------------------
    /**
     * @class LinearAllocator
     * @brief O(1) 선형 할당. reset 시 오프셋만 되돌리고, clear 시 블록을 해제합니다.
     *
     * @note 스레드 계약: allocate() 는 여러 스레드에서 동시 호출해도 안전합니다(블록 오프셋 CAS).
     *       그러나 reset() / clear() 는 진행 중인 allocate() 와 겹치면 안 됩니다 — 겹치면 이미
     *       내준 메모리를 다시 내줄 수 있습니다. 호출자가 프레임 경계 등에서 외부 동기화로
     *       allocate() 가 없을 때만 reset()/clear() 를 호출해야 합니다.
     */
    class SW_API LinearAllocator
    {
    public:
        /** @brief 기본 용량(64KB)으로 할당기를 준비합니다. */
        LinearAllocator();
        /**
         * @brief 지정된 초기 용량으로 할당기를 준비합니다.
         * @param initialCapacity 바이트 단위의 초기 용량
         */
        explicit LinearAllocator( size_t initialCapacity );
        /** @brief 소유한 블록을 해제합니다. */
        ~LinearAllocator();

        /** @brief 복사를 금지합니다. */
        LinearAllocator( const LinearAllocator& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        LinearAllocator& operator=( const LinearAllocator& ) = delete;

        /**
         * @brief 현재 블록에서 정렬된 메모리를 잘라 냅니다. 부족하면 새 블록을 붙입니다.
         * @param size 할당할 바이트 크기
         * @param alignment 메모리 정렬 기준
         * @return 할당된 메모리 포인터 (블록 슬롯 고갈·OOM 시 nullptr)
         */
        void* allocate( size_t size, size_t alignment = alignof( std::max_align_t ) );

        /**
         * @brief 모든 블록 오프셋을 0으로 되돌려 재사용합니다. 메모리는 유지합니다.
         * @warning 동시에 실행 중인 allocate() 가 없어야 합니다(클래스 스레드 계약 참고).
         */
        void reset();

        /**
         * @brief 예약된 블록을 모두 해제합니다.
         * @warning 동시에 실행 중인 allocate() 가 없어야 합니다(클래스 스레드 계약 참고).
         */
        void clear();

    private:
        /** @brief minCapacity 이상인 새 블록을 할당하고 현재 블록으로 전환합니다. _mutex 를 잡은 채 호출해야 합니다. */
        bool allocateNewBlock( size_t minCapacity );

        /** @brief 한 블록의 버퍼·용량·원자 오프셋입니다. */
        struct Block
        {
            uint8*         _pData{ nullptr };
            size_t         _capacity{ 0 };
            atomic<size_t> _offset{ 0 };
        };

        /**
         * @brief 블록 테이블 슬롯 수.
         * @details allocate() 가 락 없이 테이블을 읽으므로 저장소가 절대 재배치되면 안 됩니다.
         *          블록 용량이 기하급수적으로 커지므로 이 슬롯 수로 충분합니다.
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
