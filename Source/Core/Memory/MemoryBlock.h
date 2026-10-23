/**
 * @file MemoryBlock.h
 * @brief sw 할당자로 잡은 바이트 블록 하나를 소유하는 형식입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/MemoryTag.h"

namespace sw
{
    /**
     * @class MemoryBlock
     * @brief `Memory::allocate` · `Memory::allocateAligned` 로 잡은 블록의 소유자입니다. 소멸하면 짝이 맞는 해제로 풉니다.
     * @details 원소 타입이 없는 거친 버퍼(문자 아레나, 슬랩, 외부 API 에 넘기는 임시 상태)의 `void*` + 수동 `Memory::free` 를 대신합니다.
     *          이동만 되고 복사는 안 됩니다. 일찍 돌아가거나 예외가 나도 새지 않고, 블록 목록은 `vector<MemoryBlock>` 이면 비우는 것만으로 풀립니다.
     *          정렬 할당인지 기억하므로 호출하는 쪽이 해제 짝(`free` / `freeAligned`)을 고르지 않습니다.
     *
     *          **소유자가 블록에서 `T` 를 만들었다면 블록을 풀기 전에 `T` 를 먼저 소멸시킵니다**(이 형식은 바이트만 풉니다).
     *          한 객체는 `make_unique<T>` , 원소 배열은 `vector<T>` · `make_unique<T[]>` 를 씁니다. 이 형식이 맞는 곳은 바이트 블록 그 자체이거나
     *          수명이 소유자와 따로 풀리는 거친 저장소입니다. 외부 API 가 소유권을 가져가는 자리는 `release()` 로 넘깁니다.
     */
    class MemoryBlock
    {
    public:
        /** @brief 빈 블록(`isValid()` 가 거짓)입니다. */
        MemoryBlock() noexcept
            : _pData{ nullptr }
            , _bAligned{ false }
        {
        }

        ~MemoryBlock() { reset(); }

        /** @brief 복사를 금지합니다. 소유자는 하나입니다. */
        MemoryBlock( const MemoryBlock& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        MemoryBlock& operator=( const MemoryBlock& ) = delete;

        /** @brief 블록을 넘겨받습니다. 원본은 빈 블록이 됩니다. */
        MemoryBlock( MemoryBlock&& other ) noexcept
            : _pData{ other._pData }
            , _bAligned{ other._bAligned }
        {
            other._pData    = nullptr;
            other._bAligned = false;
        }

        /** @brief 가진 블록을 풀고 @p other 의 블록을 넘겨받습니다. */
        MemoryBlock& operator=( MemoryBlock&& other ) noexcept
        {
            if ( this != &other )
            {
                reset();
                _pData          = other._pData;
                _bAligned       = other._bAligned;
                other._pData    = nullptr;
                other._bAligned = false;
            }
            return *this;
        }

        /** @brief 현재 태그로 @p size 바이트를 잡습니다. 실패하면 빈 블록입니다(`isValid()` 로 확인합니다). */
        [[nodiscard]] static MemoryBlock allocate( size_t size ) { return MemoryBlock{ Memory::allocate( size ), false }; }

        /** @brief 명시한 태그로 @p size 바이트를 잡습니다. 실패하면 빈 블록입니다. */
        [[nodiscard]] static MemoryBlock allocate( size_t size, MemoryTag tag ) { return MemoryBlock{ Memory::allocate( size, tag ), false }; }

        /** @brief @p alignment 로 정렬해 @p size 바이트를 잡습니다. 실패하면 빈 블록입니다. */
        [[nodiscard]] static MemoryBlock allocateAligned( size_t size, size_t alignment ) { return MemoryBlock{ Memory::allocateAligned( size, alignment ), true }; }

        /** @brief 블록 시작 주소입니다. 빈 블록이면 nullptr 입니다. 소유권은 그대로입니다. */
        [[nodiscard]] void* get() const noexcept { return _pData; }

        /** @brief 블록 시작 주소를 `T*` 로 봅니다. */
        template <typename T>
        [[nodiscard]] T* getAs() const noexcept
        {
            return static_cast<T*>( _pData );
        }

        /** @brief 블록을 가졌는지입니다(할당이 성공했는지). */
        [[nodiscard]] bool isValid() const noexcept { return _pData != nullptr; }

        /**
         * @brief 소유권을 내려놓고 주소를 돌려줍니다. 받은 쪽이 `Memory::free`(정렬 블록은 `Memory::freeAligned`)로 풉니다.
         * @details 블록을 외부 API 가 가져가는 자리(해제 콜백이 있는 C API)에서만 씁니다.
         */
        [[nodiscard]] void* release() noexcept
        {
            void* pData = _pData;
            _pData      = nullptr;
            _bAligned   = false;
            return pData;
        }

        /** @brief 가진 블록을 풉니다. 빈 블록이면 아무 일도 하지 않습니다. */
        void reset() noexcept
        {
            if ( _pData == nullptr )
                return;
            if ( _bAligned )
                Memory::freeAligned( _pData );
            else
                Memory::free( _pData );
            _pData    = nullptr;
            _bAligned = false;
        }

    private:
        MemoryBlock( void* pData, bool bAligned ) noexcept
            : _pData{ pData }
            , _bAligned{ bAligned }
        {
        }

        void* _pData;
        bool  _bAligned;
    };
} // namespace sw
