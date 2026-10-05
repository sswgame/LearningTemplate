#include "pch.h"

#include "Core/Network/Replication/NetInputWindow.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Message/NetSendBudget.h"

namespace sw
{
    NetInputSendWindow::NetInputSendWindow()
        : _listEntry{}
        , _format{}
        , _oldestTick{ 0 }
        , _latestTick{ 0 }
        , _firstUnacknowledgedTick{ 0 }
        , _bHasEntry{ SW_FALSE }
    {
    }

    void NetInputSendWindow::initialize( int32 capacity, const NetInputFormat& format )
    {
        _format = format;
        _listEntry.initialize( MathUtil::max( 1, capacity ) );
        reset();
    }

    void NetInputSendWindow::reset()
    {
        _listEntry.reset();
        _oldestTick              = 0;
        _latestTick              = 0;
        _firstUnacknowledgedTick = 0;
        _bHasEntry               = SW_FALSE;
    }

    bool NetInputSendWindow::push( uint32 tick, const uint8* pData, int32 byteCount, uint32 stamp )
    {
        const bool bValidData = _format.isValidEntrySize( byteCount ) && ( byteCount == 0 || pData != nullptr );
        if ( _listEntry.getCapacity() == 0 || bValidData == false || tick == TickRingBuffer<NetInputEntry>::kEmpty )
            return false;
        if ( _bHasEntry == SW_TRUE && tick == _latestTick )
            return false; // 이미 있다 — 처음 값이 남는다
        if ( _bHasEntry == SW_TRUE && tick < _latestTick )
            reset(); // 틱이 줄었다 — 새 판이다. 옛 확인은 이 판의 것이 아니다
        if ( _bHasEntry == SW_FALSE || tick != _latestTick + 1u )
            _oldestTick = tick; // 처음이거나 틱이 건너뛰었다 — 그 앞과 이어 실을 수 없다
        NetInputEntry& entry = _listEntry.acquire( tick );
        if ( byteCount > 0 )
            entry._bytes.assign( pData, pData + byteCount );
        else
            entry._bytes.clear();
        entry._tick  = tick;
        entry._stamp = stamp;
        _latestTick  = tick;
        _bHasEntry   = SW_TRUE;
        // 고리 한 바퀴보다 오래된 것은 덮였다 — 그만큼 확인이 없으면 가장 오래된 것부터 잊는다.
        const uint32 capacity = static_cast<uint32>( _listEntry.getCapacity() );
        if ( tick - _oldestTick >= capacity )
            _oldestTick = tick - capacity + 1u;
        return true;
    }

    void NetInputSendWindow::acknowledge( uint32 firstMissingTick )
    {
        if ( _bHasEntry == SW_FALSE )
            return; // 넣은 것이 없다 — 지난 판의 확인일 수 있다
        // 아직 넣지 않은 틱은 확인할 수 없고, 확인은 늘기만 한다(비신뢰라 늦게 온 옛 확인이 있다).
        const uint32 clamped     = MathUtil::min( firstMissingTick, _latestTick + 1u );
        _firstUnacknowledgedTick = MathUtil::max( _firstUnacknowledgedTick, clamped );
    }

    uint32 NetInputSendWindow::getFirstPendingTick() const
    {
        return _bHasEntry == SW_TRUE ? MathUtil::max( _firstUnacknowledgedTick, _oldestTick ) : _firstUnacknowledgedTick;
    }

    int32 NetInputSendWindow::getPendingCount() const
    {
        const uint32 first = getFirstPendingTick();
        return _bHasEntry == SW_TRUE && first <= _latestTick ? static_cast<int32>( _latestTick - first + 1u ) : 0;
    }

    int32 NetInputSendWindow::write( BitWriter& writer, NetSendBudget& budget ) const
    {
        const uint32 first    = getFirstPendingTick();
        const uint32 maxCount = static_cast<uint32>( MathUtil::min( getPendingCount(), MathUtil::max( 0, _format._maxEntryCount ) ) );
        // 개수 칸은 항목보다 먼저 쓰므로 가장 큰 값으로 센다. 오래된 것부터 — 새 것부터 실으면 못 실은 옛 것이 영구 빈틈이 된다.
        budget.reserveBits( BitMath::computeVarUintBits( first ) + BitMath::computeVarUintBits( maxCount ) );
        uint32 count = 0;
        while ( count < maxCount && budget.tryReserveBits( computeEntryBits( getEntry( first + count ) ) ) )
            ++count;
        writer.writeVarUint( first );
        writer.writeVarUint( count );
        for ( uint32 index = 0; index < count; ++index )
            writeEntry( writer, getEntry( first + index ) );
        return static_cast<int32>( count );
    }

    const NetInputEntry& NetInputSendWindow::getEntry( uint32 tick ) const
    {
        const NetInputEntry* pEntry = _listEntry.find( tick );
        SW_ASSERT( pEntry != nullptr ); // [_oldestTick, _latestTick] 는 이어 넣었고 고리 크기를 넘지 않는다 — 늘 들어 있다
        return *pEntry;
    }

    int32 NetInputSendWindow::computeEntryBits( const NetInputEntry& entry ) const
    {
        const int32 stampBits = _format._bStamped == SW_TRUE ? BitMath::computeVarUintBits( entry._stamp ) : 0;
        const int32 byteBits  = _format._fixedEntryBytes > 0 ? _format._fixedEntryBytes * 8 : BitMath::computeBlobBits( static_cast<int32>( entry._bytes.size() ) );
        return stampBits + byteBits;
    }

    void NetInputSendWindow::writeEntry( BitWriter& writer, const NetInputEntry& entry ) const
    {
        if ( _format._bStamped == SW_TRUE )
            writer.writeVarUint( entry._stamp );
        if ( _format._fixedEntryBytes > 0 )
            writer.writeBytes( entry._bytes.data(), _format._fixedEntryBytes ); // 크기는 `push` 가 맞췄다
        else
            writer.writeBlob( entry._bytes.data(), static_cast<int32>( entry._bytes.size() ) );
    }
} // namespace sw

namespace sw
{
    NetInputReceiveBuffer::NetInputReceiveBuffer()
        : _listEntry{}
        , _format{}
        , _windowFirst{ 0 }
        , _windowEnd{ 0 }
        , _firstMissingTick{ 0 }
        , _mode{ NetInputWindowMode::Manual }
    {
    }

    void NetInputReceiveBuffer::initialize( int32 capacity, const NetInputFormat& format, NetInputWindowMode mode )
    {
        _format = format;
        _mode   = mode;
        _listEntry.initialize( MathUtil::max( 1, capacity ) );
        reset();
    }

    void NetInputReceiveBuffer::reset()
    {
        _listEntry.reset();
        _windowFirst      = 0;
        _windowEnd        = static_cast<uint32>( _listEntry.getCapacity() );
        _firstMissingTick = 0;
    }

    void NetInputReceiveBuffer::setWindow( uint32 first, uint32 end )
    {
        _windowFirst       = MathUtil::max( _windowFirst, first );
        const uint64 limit = MathUtil::min( static_cast<uint64>( _windowFirst ) + static_cast<uint64>( _listEntry.getCapacity() ), static_cast<uint64>( kNoWindowEnd ) );
        _windowEnd         = static_cast<uint32>( MathUtil::clamp( static_cast<uint64>( end ), static_cast<uint64>( _windowFirst ), limit ) );
        advanceFirstMissingTick();
    }

    bool NetInputReceiveBuffer::store( uint32 tick, const uint8* pData, int32 byteCount, uint32 stamp )
    {
        const bool bValidData = _format.isValidEntrySize( byteCount ) && ( byteCount == 0 || pData != nullptr );
        if ( bValidData == false )
            return false;
        if ( _mode == NetInputWindowMode::FollowNewest )
            followTick( tick );
        if ( canStore( tick ) == false )
            return false;
        NetInputEntry& entry = _listEntry.acquire( tick );
        if ( byteCount > 0 )
            entry._bytes.assign( pData, pData + byteCount );
        else
            entry._bytes.clear();
        entry._tick  = tick;
        entry._stamp = stamp;
        advanceFirstMissingTick();
        return true;
    }

    bool NetInputReceiveBuffer::read( BitReader& reader, vector<uint32>* pOutListNewTick )
    {
        if ( pOutListNewTick != nullptr )
            pOutListNewTick->clear();
        const uint64 first      = reader.readVarUint();
        const uint64 count      = reader.readVarUint();
        const uint64 maxCount   = static_cast<uint64>( MathUtil::max( 0, _format._maxEntryCount ) );
        const uint64 tickLimit  = static_cast<uint64>( kNoWindowEnd );
        const bool   bValidHead = reader.hasOverflowed() == false && first <= tickLimit && count <= maxCount && first + count <= tickLimit;
        if ( bValidHead == false )
            return false;
        // 먼저 끝까지 훑는다 — 앞 항목만 넣고 뒤에서 깨지면 받은 쪽은 메시지를 깨짐으로 세는데 앞 입력은 이미 들어가 있다.
        BitReader check = reader;
        for ( uint64 index = 0; index < count; ++index )
        {
            if ( skipEntry( check ) == false )
                return false;
        }
        if ( _mode == NetInputWindowMode::FollowNewest && count > 0 )
            followTick( static_cast<uint32>( first + count - 1u ) );
        for ( uint64 index = 0; index < count; ++index )
        {
            const uint32 tick = static_cast<uint32>( first + index );
            if ( canStore( tick ) == false )
            {
                (void)skipEntry( reader ); // 창 밖 · 이미 가진 것 — 다시 보낸 것이 대부분이라 버퍼를 잡지 않고 넘긴다
                continue;
            }
            NetInputEntry& entry = _listEntry.acquire( tick );
            (void)readEntry( reader, entry ); // 위에서 끝까지 훑었다 — 깨지지 않는다
            entry._tick = tick;
            if ( pOutListNewTick != nullptr )
                pOutListNewTick->push_back( tick );
        }
        advanceFirstMissingTick();
        return reader.hasOverflowed() == false;
    }

    const NetInputEntry* NetInputReceiveBuffer::find( uint32 tick ) const
    {
        return _listEntry.find( tick );
    }

    const NetInputEntry* NetInputReceiveBuffer::findLatestAtOrBefore( uint32 tick, uint32 lowestTick ) const
    {
        if ( _listEntry.getCapacity() == 0 || tick < lowestTick )
            return nullptr;
        // 고리 한 바퀴보다 앞은 칸이 겹친다 — 거기까지만 본다.
        const uint32 stepCount = MathUtil::min( tick - lowestTick, static_cast<uint32>( _listEntry.getCapacity() ) - 1u ) + 1u;
        for ( uint32 step = 0; step < stepCount; ++step )
        {
            const NetInputEntry* pEntry = find( tick - step );
            if ( pEntry != nullptr )
                return pEntry;
        }
        return nullptr;
    }

    bool NetInputReceiveBuffer::canStore( uint32 tick ) const
    {
        const bool bInWindow = _windowFirst <= tick && tick < _windowEnd;
        if ( _listEntry.getCapacity() == 0 || bInWindow == false || tick < _firstMissingTick )
            return false;
        return find( tick ) == nullptr; // 이미 있다(다시 보낸 것)
    }

    void NetInputReceiveBuffer::followTick( uint32 tick )
    {
        // 받은 가장 새 틱이 창 끝에 오게 — 고리 크기보다 오래된 것은 놓는다.
        const uint64 next     = static_cast<uint64>( tick ) + 1u;
        const uint64 capacity = static_cast<uint64>( _listEntry.getCapacity() );
        if ( capacity == 0 )
            return;
        setWindow( static_cast<uint32>( next > capacity ? next - capacity : 0u ), kNoWindowEnd );
    }

    void NetInputReceiveBuffer::advanceFirstMissingTick()
    {
        // 창 아래는 더 받지 않는다 — 받은 것으로 친다.
        _firstMissingTick = MathUtil::max( _firstMissingTick, _windowFirst );
        while ( _firstMissingTick != kNoWindowEnd && find( _firstMissingTick ) != nullptr )
            ++_firstMissingTick;
    }

    bool NetInputReceiveBuffer::readEntry( BitReader& reader, NetInputEntry& outEntry ) const
    {
        const uint64 stamp = _format._bStamped == SW_TRUE ? reader.readVarUint() : 0u;
        if ( reader.hasOverflowed() || stamp > static_cast<uint64>( kNoWindowEnd ) )
            return false;
        outEntry._stamp = static_cast<uint32>( stamp );
        if ( _format._fixedEntryBytes > 0 )
        {
            outEntry._bytes.resize( static_cast<size_t>( _format._fixedEntryBytes ) );
            return reader.readBytes( outEntry._bytes.data(), _format._fixedEntryBytes );
        }
        return reader.readBlob( outEntry._bytes, _format._maxEntryBytes );
    }

    bool NetInputReceiveBuffer::skipEntry( BitReader& reader ) const
    {
        const uint64 stamp = _format._bStamped == SW_TRUE ? reader.readVarUint() : 0u;
        if ( reader.hasOverflowed() || stamp > static_cast<uint64>( kNoWindowEnd ) )
            return false;
        return _format._fixedEntryBytes > 0 ? reader.skipBytes( _format._fixedEntryBytes ) : reader.skipBlob( _format._maxEntryBytes );
    }
} // namespace sw
