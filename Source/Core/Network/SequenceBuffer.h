/**
 * @file SequenceBuffer.h
 * @brief 16 비트 시퀀스로 찾는 고리 버퍼 — 보낸 패킷 · 받은 패킷 · 보낸 메시지를 시퀀스 번호로 기억합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    /**
     * @class SequenceBuffer
     * @brief 자리 = 시퀀스 % 크기 입니다. 자리마다 들어 있는 시퀀스를 같이 적어 두어, 오래된(감겨 덮인) 것을 찾으면 없다고 합니다.
     */
    template <typename T>
    class SequenceBuffer
    {
    public:
        static constexpr uint32 kEmpty = 0xFFFFFFFFu;

        explicit SequenceBuffer( int32 size = 256 )
        {
            _listEntry.resize( static_cast<size_t>( size > 0 ? size : 1 ) );
            _listSequence.assign( _listEntry.size(), kEmpty );
        }

        /** @brief 넣고 그 자리를 돌려줍니다. 지금 가장 새 것보다 크게 낡았으면 nullptr 입니다. */
        T* insert( uint16 sequence )
        {
            if ( _bHasNewest && NetSequence::isLess( sequence, static_cast<uint16>( _newest - static_cast<uint16>( _listEntry.size() ) ) ) )
                return nullptr;
            if ( _bHasNewest == false || NetSequence::isGreater( sequence, _newest ) )
            {
                // 새로 앞선 만큼 사이의 자리를 비운다(건너뛴 시퀀스가 옛 값으로 남지 않게).
                if ( _bHasNewest )
                {
                    const int32 gap = NetSequence::computeDifference( sequence, _newest );
                    for ( int32 offset = 1; offset < gap && offset <= static_cast<int32>( _listEntry.size() ); ++offset )
                        _listSequence[computeIndex( static_cast<uint16>( _newest + offset ) )] = kEmpty;
                }
                _newest     = sequence;
                _bHasNewest = true;
            }
            const size_t index   = computeIndex( sequence );
            _listSequence[index] = sequence;
            _listEntry[index]    = T{};
            return &_listEntry[index];
        }

        T* find( uint16 sequence )
        {
            const size_t index = computeIndex( sequence );
            return _listSequence[index] == sequence ? &_listEntry[index] : nullptr;
        }
        const T* find( uint16 sequence ) const { return const_cast<SequenceBuffer*>( this )->find( sequence ); }
        bool     exists( uint16 sequence ) const { return find( sequence ) != nullptr; }
        void     remove( uint16 sequence )
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
        size_t computeIndex( uint16 sequence ) const { return static_cast<size_t>( sequence ) % _listEntry.size(); }

        vector<T>      _listEntry{};
        vector<uint32> _listSequence{};
        uint16         _newest{ 0 };
        bool           _bHasNewest{ false };
    };
} // namespace sw
