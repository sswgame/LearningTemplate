#include "pch.h"

#include "Engine/Graphics/Material/MaterialChangeLog.h"

#include "Core/Algorithm/VectorUtil.h"

namespace sw
{
    MaterialChangeLog::MaterialChangeLog()
        : _generation{ 1 }
    {
        ScopedLock<SpinLock> guard( _lock );
        _listEntry.resize( kCapacity );
    }

    void MaterialChangeLog::record( const void* pOwner )
    {
        ScopedLock<SpinLock> guard( _lock );
        const uint64         generation    = _generation.load( std::memory_order_relaxed ) + 1u;
        _listEntry[generation % kCapacity] = Entry{ generation, pOwner };
        _generation.store( generation, std::memory_order_release ); // 칸을 쓴 뒤에 세대를 낸다 — 세대를 본 쪽은 그 칸까지 읽을 수 있다
    }

    bool MaterialChangeLog::collectChangedOwners( uint64 sinceGeneration, uint64 untilGeneration, vector<const void*>& outListOwner ) const
    {
        outListOwner.clear();
        if ( untilGeneration <= sinceGeneration )
            return true;
        if ( untilGeneration - sinceGeneration > kCapacity )
            return false; // 고리가 넘쳤다 — 앞 칸은 이미 덮였다
        {
            ScopedLock<SpinLock> guard( _lock );
            for ( uint64 generation = sinceGeneration + 1u; generation <= untilGeneration; ++generation )
            {
                const Entry& entry = _listEntry[generation % kCapacity];
                if ( entry._generation != generation || entry._pOwner == nullptr )
                    return false; // 덮인 칸이거나 "모두" 변경이다
                outListOwner.push_back( entry._pOwner );
            }
        }
        VectorUtil::sortUnique( outListOwner );
        return true;
    }

    void MaterialCompletionWatch::watch( void* pOwner, PollFn poll )
    {
        SW_ASSERT( pOwner != nullptr && poll != nullptr );
        ScopedLock<SpinLock> guard( _lock );
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._pOwner == pOwner )
                return;
        }
        _listEntry.push_back( Entry{ pOwner, poll } );
    }

    void MaterialCompletionWatch::unwatch( const void* pOwner )
    {
        ScopedLock<SpinLock> guard( _lock );
        VectorUtil::removeIf( _listEntry, [pOwner]( const Entry& entry )
        { return entry._pOwner == pOwner; } );
    }

    uint32 MaterialCompletionWatch::pollAll( MaterialChangeLog& log )
    {
        // 잠근 채 묻는다 — 주인의 소멸자(`unwatch`)가 이 잠금을 기다리므로 묻는 동안 주인이 사라지지 않는다. 지켜보는 주인은 로드 중인 것뿐이라 적다.
        ScopedLock<SpinLock> guard( _lock );
        uint32               completeCount = 0;
        VectorUtil::removeIf( _listEntry, [&log, &completeCount]( const Entry& entry )
        {
            if ( entry._poll( entry._pOwner ) == PollResult::Pending )
                return false;
            log.record( entry._pOwner );
            ++completeCount;
            return true;
        } );
        return completeCount;
    }

    uint32 MaterialCompletionWatch::getWatchCount() const
    {
        ScopedLock<SpinLock> guard( _lock );
        return static_cast<uint32>( _listEntry.size() );
    }
} // namespace sw
