/**
 * @file SequenceBuffer.h
 * @brief 16 비트 시퀀스로 찾는 고리 버퍼 — 보낸 패킷 · 받은 패킷 · 보낸 메시지를 시퀀스 번호로 기억합니다.
 * @details 감기는 시퀀스라 낡은 것을 거절하고 건너뛴 칸을 비웁니다. 감기지 않는 32 비트 틱 · 프레임은 `TickRingBuffer`(Replication 층 — 낡음은 쓰는 쪽이 본다).
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
     * @class SequenceBuffer
     * @brief 자리 = 시퀀스 % 크기 입니다. 자리마다 들어 있는 시퀀스를 같이 적어 두어, 오래된(감겨 덮인) 것을 찾으면 없다고 합니다.
     * @details 칸을 다시 쓸 때(`insert`) 원소가 `void resetForReuse()` 를 가지면 그것을 부르고, 아니면 `T{}` 를 대입합니다. 버퍼를 가진 원소(신뢰 메시지의
     *          `vector<uint8>`)는 `resetForReuse` 로 모든 필드를 기본값으로 되돌리되 버퍼 용량은 남깁니다 — `T{}` 대입은 버퍼를 놓아, 칸이 돌 때마다
     *          다시 할당하게 합니다.
     */
    template <typename T>
    class SequenceBuffer
    {
    public:
        static constexpr uint32 kEmpty = invalid_index::kUint32;

        explicit SequenceBuffer( int32 size = 256 )
        {
            _listEntry.resize( static_cast<size_t>( size > 0 ? size : 1 ) );
            _listSequence.assign( _listEntry.size(), kEmpty );
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
                    for ( int32 offset = 1; offset < gap && offset <= static_cast<int32>( _listEntry.size() ); ++offset )
                    {
                        _listSequence[computeIndex( static_cast<uint16>( _newest + offset ) )] = kEmpty;
                    }
                }
                _newest     = sequence;
                _bHasNewest = true;
            }
            const size_t index   = computeIndex( sequence );
            _listSequence[index] = sequence;
            if constexpr ( HasResetForReuse<T>::value )
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
            return _bHasNewest && NetSequence::isLess( sequence, static_cast<uint16>( _newest - static_cast<uint16>( _listEntry.size() ) ) );
        }
        void remove( uint16 sequence )
        {
            const size_t index = computeIndex( sequence );
            if ( _listSequence[index] == sequence )
                _listSequence[index] = kEmpty;
        }
        void reset()
        {
            _listSequence.assign( _listEntry.size(), kEmpty );
            _bHasNewest = false;
            _newest     = 0;
        }

        uint16 getNewest() const { return _newest; }
        bool   hasNewest() const { return _bHasNewest; }
        int32  getSize() const { return static_cast<int32>( _listEntry.size() ); }

    private:
        /** @brief 원소가 칸 재사용 함수(`resetForReuse`)를 가졌는지입니다. */
        template <typename U, typename = void>
        struct HasResetForReuse : std::false_type
        {
        };
        template <typename U>
        struct HasResetForReuse<U, std::void_t<decltype( std::declval<U&>().resetForReuse() )>> : std::true_type
        {
        };

        size_t computeIndex( uint16 sequence ) const { return static_cast<size_t>( sequence ) % _listEntry.size(); }

        vector<T>      _listEntry{};
        vector<uint32> _listSequence{};
        uint16         _newest{ 0 };
        bool           _bHasNewest{ false };
    };
} // namespace sw
