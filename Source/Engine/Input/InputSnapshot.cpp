#include "pch.h"

#include "Engine/Input/InputSnapshot.h"

#include "Core/Memory/Memory.h"

namespace sw
{
    namespace
    {
        /** @brief 값 하나를 버퍼에 적고 커서를 옮깁니다. */
        template <typename T>
        void writeField( uint8*& pCursor, const T& value )
        {
            Memory::copy( pCursor, &value, sizeof( T ) );
            pCursor += sizeof( T );
        }

        /** @brief 값 하나를 버퍼에서 읽고 커서를 옮깁니다. */
        template <typename T>
        void readField( const uint8*& pCursor, T& outValue )
        {
            Memory::copy( &outValue, pCursor, sizeof( T ) );
            pCursor += sizeof( T );
        }
    } // namespace
} // namespace sw

namespace sw
{
    uint32 InputSnapshot::serialize( uint8* pOutBuffer, uint32 bufferSize ) const
    {
        if ( pOutBuffer == nullptr || bufferSize < kSerializedSize )
            return 0;

        // **필드를 순서대로 적는다 — 구조체를 통째로 복사하지 않는다.** `_tickNumber` 뒤에는 정렬
        // 패딩 4바이트가 있고, 그 값은 아무도 정하지 않는다. 통째로 복사하면 그 패딩까지 파일과
        // 네트워크로 나가서, 같은 입력을 두 번 저장해도 바이트가 달라진다.
        uint8* pCursor = pOutBuffer;
        writeField( pCursor, _tickNumber );
        writeField( pCursor, _buttonMask );
        writeField( pCursor, _moveVector._x );
        writeField( pCursor, _moveVector._y );
        writeField( pCursor, _lookVector._x );
        writeField( pCursor, _lookVector._y );
        writeField( pCursor, _leftTrigger );
        writeField( pCursor, _rightTrigger );
        return kSerializedSize;
    }

    bool InputSnapshot::deserialize( const uint8* pBuffer, uint32 bufferSize )
    {
        if ( pBuffer == nullptr || bufferSize < kSerializedSize )
            return false;

        const uint8* pCursor = pBuffer;
        readField( pCursor, _tickNumber );
        readField( pCursor, _buttonMask );
        readField( pCursor, _moveVector._x );
        readField( pCursor, _moveVector._y );
        readField( pCursor, _lookVector._x );
        readField( pCursor, _lookVector._y );
        readField( pCursor, _leftTrigger );
        readField( pCursor, _rightTrigger );
        return true;
    }

    InputHistoryBuffer::InputHistoryBuffer()
        : _arrHistory{}
        , _writeIndex{ 0 }
        , _count{ 0 }
        , _latestTick{ 0 }
    {
    }

    void InputHistoryBuffer::recordSnapshot( const InputSnapshot& snapshot )
    {
        _arrHistory[_writeIndex] = snapshot;
        _latestTick              = snapshot._tickNumber;
        _writeIndex              = ( _writeIndex + 1 ) & ( kDefaultCapacity - 1 );
        if ( _count < kDefaultCapacity )
            ++_count;
    }

    const InputSnapshot* InputHistoryBuffer::getSnapshot( uint32 tickNumber ) const
    {
        if ( _count == 0 || tickNumber > _latestTick )
            return nullptr;

        const uint32 diff = _latestTick - tickNumber;
        if ( diff < _count )
        {
            const size_t index = ( _writeIndex + kDefaultCapacity - 1 - diff ) & ( kDefaultCapacity - 1 );
            if ( _arrHistory[index]._tickNumber == tickNumber )
                return &_arrHistory[index];
        }

        return nullptr;
    }

    const InputSnapshot* InputHistoryBuffer::getLatestSnapshot() const
    {
        if ( _count == 0 )
            return nullptr;

        const size_t lastIndex = ( _writeIndex + kDefaultCapacity - 1 ) & ( kDefaultCapacity - 1 );
        return &_arrHistory[lastIndex];
    }

    void InputHistoryBuffer::clear()
    {
        _writeIndex = 0;
        _count      = 0;
        _latestTick = 0;
    }
} // namespace sw
