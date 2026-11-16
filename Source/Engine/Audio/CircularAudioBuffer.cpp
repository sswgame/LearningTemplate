#include "pch.h"

#include "Engine/Audio/CircularAudioBuffer.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct CircularAudioBufferInternal
        {
            /** @brief @p capacity 이상인 가장 작은 2의 거듭제곱입니다. 0 이거나 2^31 을 넘으면 프로그램 결함이다. */
            static uint32 roundUpCapacity( uint32 capacity )
            {
                constexpr uint32 kMaxCapacity = 1u << 31;
                if ( capacity == 0 || capacity > kMaxCapacity )
                    SW_FATAL( "CircularAudioBuffer capacity %# is outside [1, 2^31]", capacity );
                uint32 rounded = 1;
                while ( rounded < capacity )
                {
                    rounded <<= 1;
                }
                SW_ASSERT( MathUtil::isPowerOfTwo( rounded ) );
                return rounded;
            }
        };
    } // namespace

    CircularAudioBuffer::CircularAudioBuffer( uint32 capacity )
        : _arrSample{}
        , _capacity{ CircularAudioBufferInternal::roundUpCapacity( capacity ) }
        , _mask{ _capacity - 1 }
        , _readIndex{ 0 }
        , _writeIndex{ 0 }
    {
        _arrSample = make_unique<float32[]>( _capacity );
    }

    uint32 CircularAudioBuffer::push( vector_reference<const float32> listSample )
    {
        const uint32 writeIndex = _writeIndex.load( std::memory_order_relaxed ); // 이 위치는 생산자만 바꾼다
        const uint32 readIndex  = _readIndex.load( std::memory_order_acquire );  // 소비자가 다 읽은 칸만 덮는다
        const uint32 freeCount  = _capacity - ( writeIndex - readIndex );
        const uint32 pushCount  = static_cast<uint32>( listSample.size() ) < freeCount ? static_cast<uint32>( listSample.size() ) : freeCount;
        for ( uint32 offset = 0; offset < pushCount; ++offset )
        {
            _arrSample[( writeIndex + offset ) & _mask] = listSample[offset];
        }
        _writeIndex.store( writeIndex + pushCount, std::memory_order_release ); // 샘플을 다 쓴 뒤 보인다
        return pushCount;
    }

    uint32 CircularAudioBuffer::pop( vector_reference<float32> outListSample )
    {
        const uint32 readIndex  = _readIndex.load( std::memory_order_relaxed );  // 이 위치는 소비자만 바꾼다
        const uint32 writeIndex = _writeIndex.load( std::memory_order_acquire ); // 생산자가 다 쓴 칸만 읽는다
        const uint32 available  = writeIndex - readIndex;
        const uint32 popCount   = static_cast<uint32>( outListSample.size() ) < available ? static_cast<uint32>( outListSample.size() ) : available;
        for ( uint32 offset = 0; offset < popCount; ++offset )
        {
            outListSample[offset] = _arrSample[( readIndex + offset ) & _mask];
        }
        _readIndex.store( readIndex + popCount, std::memory_order_release ); // 읽은 칸을 생산자에게 돌려준다
        return popCount;
    }

    uint32 CircularAudioBuffer::getNum() const
    {
        const uint32 readIndex  = _readIndex.load( std::memory_order_acquire );
        const uint32 writeIndex = _writeIndex.load( std::memory_order_acquire );
        return writeIndex - readIndex;
    }
} // namespace sw
