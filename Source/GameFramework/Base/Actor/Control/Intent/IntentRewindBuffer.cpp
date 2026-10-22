#include "pch.h"

#include "GameFramework/Base/Actor/Control/Intent/IntentRewindBuffer.h"

#include "GameFramework/Base/Foundation/Utility/ConfigRangeUtil.h"

namespace sw
{
    SW_LOG_CALLER( "IntentRewind" );

    IntentRewindBuffer::IntentRewindBuffer()
        : _listSnapshot{}
        , _byteCount{ 0 }
        , _maxBytes{ 0 }
        , _intervalTicks{ 0 }
    {
    }

    bool IntentRewindBuffer::initialize( const IntentRewindConfig& config )
    {
        const string rangeError = ConfigRangeUtil::describeRangeErrors( config );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR( "IntentRewindBuffer: refused to start - settings '%#'", rangeError.c_str() );
            return false;
        }
        _intervalTicks = static_cast<uint32>( config._intervalTicks );
        _maxBytes      = static_cast<size_t>( config._maxMegabytes ) * constant::kBytesPerMegabyte;
        clear();
        return true;
    }

    bool IntentRewindBuffer::isKeyTick( uint32 tick ) const
    {
        return _intervalTicks > 0 && tick % _intervalTicks == 0;
    }

    bool IntentRewindBuffer::add( uint32 tick, vector<uint8> bytes )
    {
        if ( _listSnapshot.empty() == false && tick <= _listSnapshot.back()._tick )
        {
            SW_LOG_ERROR( "Rewind key at tick %# does not follow tick %# - discard the future after a seek first", tick, _listSnapshot.back()._tick );
            return false;
        }
        if ( bytes.size() > _maxBytes )
        {
            SW_LOG_ERROR( "Rewind key at tick %# is %# bytes, over the whole budget of %# bytes - it is not kept", tick, static_cast<uint64>( bytes.size() ),
                          static_cast<uint64>( _maxBytes ) );
            return false;
        }
        _byteCount += bytes.size();
        _listSnapshot.push_back( KeySnapshot{ std::move( bytes ), tick } );
        // 상한 — 가장 오래된 것부터 버린다(방금 넣은 것은 상한 안이라 남는다).
        size_t dropCount = 0;
        while ( _byteCount > _maxBytes && dropCount + 1 < _listSnapshot.size() )
        {
            _byteCount -= _listSnapshot[dropCount]._bytes.size();
            ++dropCount;
        }
        _listSnapshot.erase( _listSnapshot.begin(), _listSnapshot.begin() + static_cast<ptrdiff_t>( dropCount ) );
        return true;
    }

    const IntentRewindBuffer::KeySnapshot* IntentRewindBuffer::findAtOrBefore( uint32 tick ) const
    {
        const KeySnapshot* pFound = nullptr;
        for ( const KeySnapshot& snapshot : _listSnapshot )
        {
            if ( snapshot._tick > tick )
                break;
            pFound = &snapshot;
        }
        return pFound;
    }

    void IntentRewindBuffer::discardAfter( uint32 tick )
    {
        while ( _listSnapshot.empty() == false && _listSnapshot.back()._tick > tick )
        {
            _byteCount -= _listSnapshot.back()._bytes.size();
            _listSnapshot.pop_back();
        }
    }

    void IntentRewindBuffer::clear()
    {
        _listSnapshot.clear();
        _byteCount = 0;
    }
} // namespace sw
