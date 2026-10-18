/**
 * @file SequenceBuffer.h
 * @brief 16 비트 시퀀스로 찾는 고리 버퍼 — 보낸 패킷 · 받은 패킷 · 보낸 메시지를 시퀀스 번호로 기억합니다.
 * @details 감기는 시퀀스라 낡은 것을 거절하고 건너뛴 칸을 비웁니다. 감기지 않는 32 비트 틱 · 프레임은 `TickRingBuffer`(Replication 층 — 낡음은 쓰는 쪽이 본다).
 *          크기는 형식의 일부(템플릿 상수)이고 2 의 거듭제곱이어야 합니다. 크기가 65536 의 약수가 아니면 시퀀스가 65535 → 0 으로 감길 때
 *          자리(시퀀스 % 크기)가 이어지지 않아 감김 직전과 직후의 시퀀스가 같은 칸을 덮습니다. 실행 중 크기(0 이면 1 로 바꾸던 폴백 포함)는 받지 않습니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    /**
     * @brief `SequenceBuffer` 가 칸을 다시 쓸 때(`insert`) 원소를 되돌리는 방법입니다. 원소 형식이 `static constexpr SequenceSlotReset kSlotReset` 로 고릅니다.
     * @details 이름으로 찾아(SFINAE) 고르면 함수 이름을 틀렸을 때 조용히 `T{}` 대입으로 돌아가 버퍼를 놓습니다. 그래서 원소가 방법을 밝히고,
     *          `ReuseBuffer` 를 밝힌 원소에 `resetForReuse` 가 없으면 컴파일 오류로 멈춥니다(`IsSequenceBufferElement`).
     */
    enum class SequenceSlotReset : uint8
    {
        Assign,      ///< `T{}` 를 대입한다. 버퍼가 없는 원소.
        ReuseBuffer, ///< `void resetForReuse()` 를 부른다. 모든 필드를 기본값으로 되돌리되 버퍼 용량은 남긴다(칸이 돌 때마다 다시 할당하지 않게).
    };

    namespace internal
    {
        template <typename T, typename = void>
        struct HasResetForReuse : std::false_type
        {
        };
    } // namespace internal

    namespace internal
    {
        template <typename T>
        struct HasResetForReuse<T, std::void_t<decltype( std::declval<T&>().resetForReuse() )>> : std::true_type
        {
        };

        template <typename T, typename = void>
        struct DeclaresSlotReset : std::false_type
        {
        };
        template <typename T>
        struct DeclaresSlotReset<T, std::void_t<decltype( T::kSlotReset )>>
            : std::is_same<std::remove_cv_t<decltype( T::kSlotReset )>, SequenceSlotReset>
        {
        };

        template <typename T>
        constexpr bool isSequenceBufferElement()
        {
            // 스칼라(정수 · 포인터)는 버퍼도 함수도 가질 수 없어 대입뿐이다 — 이름을 틀릴 자리가 없다.
            if constexpr ( std::is_scalar_v<T> )
                return true;
            else if constexpr ( DeclaresSlotReset<T>::value )
                return T::kSlotReset == SequenceSlotReset::Assign || HasResetForReuse<T>::value;
            else
                return false;
        }

        /** @brief 원소가 밝힌 칸 재사용 방법입니다. 스칼라는 대입입니다. */
        template <typename T>
        constexpr SequenceSlotReset getSlotReset()
        {
            if constexpr ( std::is_scalar_v<T> )
                return SequenceSlotReset::Assign;
            else
                return T::kSlotReset;
        }
    } // namespace internal

    /**
     * @brief 원소 형식이 `kSlotReset` 을 밝혔고, `ReuseBuffer` 면 `resetForReuse()` 를 가졌는지입니다. 둘 중 하나라도 어기면 false 이고
     *        `SequenceBuffer` 가 컴파일 오류로 멈춥니다. 감지(SFINAE)는 이 검사에만 쓰고, 고르는 것은 원소가 밝힌 `kSlotReset` 입니다.
     */
    template <typename T>
    struct IsSequenceBufferElement : std::bool_constant<internal::isSequenceBufferElement<T>()>
    {
    };
} // namespace sw

namespace sw
{
    /**
     * @class SequenceBuffer
     * @brief 자리 = 시퀀스 & (크기 - 1) 입니다. 자리마다 들어 있는 시퀀스를 같이 적어 두어, 오래된(감겨 덮인) 것을 찾으면 없다고 합니다.
     * @tparam T 원소입니다. 클래스면 `static constexpr SequenceSlotReset kSlotReset` 로 칸 재사용 방법을 밝혀야 합니다(`SequenceSlotReset`). 스칼라는 대입입니다.
     * @tparam kSize 칸 수입니다. 2 의 거듭제곱이고 시퀀스 공간의 절반(32768) 이하입니다 — 넘으면 "가장 새 것보다 크기만큼 뒤"가 앞뒤를 가를 수 없습니다.
     */
    template <typename T, uint32 kSize>
    class SequenceBuffer
    {
        static_assert( kSize > 0 && ( kSize & ( kSize - 1 ) ) == 0, "SequenceBuffer size must be a power of two (a divisor of 65536)" );
        static_assert( kSize <= 32768, "SequenceBuffer size must not exceed half the 16-bit sequence space" );
        static_assert( IsSequenceBufferElement<T>::value,
                       "SequenceBuffer element must declare static constexpr SequenceSlotReset kSlotReset (ReuseBuffer requires void resetForReuse())" );

    public:
        static constexpr uint32 kEmpty = invalid_index::kUint32;

        SequenceBuffer()
        {
            _listEntry.resize( kSize );
            _listSequence.assign( kSize, kEmpty );
        }

        /** @brief 넣고 그 자리를 돌려줍니다. 지금 가장 새 것보다 크게 낡았으면 nullptr 입니다. */
        T* insert( uint16 sequence )
        {
            if ( isStale( sequence ) )
                return nullptr;
            if ( _bHasNewest == false || NetSequence::isGreater( sequence, _newest ) )
            {
                // 새로 앞선 만큼 사이의 자리를 비운다(건너뛴 시퀀스가 옛 값으로 남지 않게).
                if ( _bHasNewest )
                {
                    const int32 gap = NetSequence::computeDifference( sequence, _newest );
                    for ( int32 offset = 1; offset < gap && offset <= static_cast<int32>( kSize ); ++offset )
                    {
                        _listSequence[computeIndex( static_cast<uint16>( _newest + offset ) )] = kEmpty;
                    }
                }
                _newest     = sequence;
                _bHasNewest = true;
            }
            const size_t index   = computeIndex( sequence );
            _listSequence[index] = sequence;
            if constexpr ( internal::getSlotReset<T>() == SequenceSlotReset::ReuseBuffer )
                _listEntry[index].resetForReuse();
            else
                _listEntry[index] = T{};
            return &_listEntry[index];
        }

        T* find( uint16 sequence )
        {
            const size_t index = computeIndex( sequence );
            return _listSequence[index] == sequence ? &_listEntry[index] : nullptr;
        }
        const T* find( uint16 sequence ) const { return const_cast<SequenceBuffer*>( this )->find( sequence ); }
        bool     exists( uint16 sequence ) const { return find( sequence ) != nullptr; }
        /** @brief 넣으려 해도 너무 낡아(가장 새 것보다 크기만큼 넘게 뒤) `insert` 가 거절할 시퀀스면 true 입니다. */
        bool isStale( uint16 sequence ) const
        {
            return _bHasNewest && NetSequence::isLess( sequence, static_cast<uint16>( _newest - static_cast<uint16>( kSize ) ) );
        }
        void remove( uint16 sequence )
        {
            const size_t index = computeIndex( sequence );
            if ( _listSequence[index] == sequence )
                _listSequence[index] = kEmpty;
        }
        void reset()
        {
            _listSequence.assign( kSize, kEmpty );
            _bHasNewest = false;
            _newest     = 0;
        }

        uint16                 getNewest() const { return _newest; }
        bool                   hasNewest() const { return _bHasNewest; }
        static constexpr int32 getSize() { return static_cast<int32>( kSize ); }

    private:
        static size_t computeIndex( uint16 sequence ) { return static_cast<size_t>( sequence & ( kSize - 1 ) ); }

        vector<T>      _listEntry{};
        vector<uint32> _listSequence{};
        uint16         _newest{ 0 };
        bool           _bHasNewest{ false };
    };
} // namespace sw
