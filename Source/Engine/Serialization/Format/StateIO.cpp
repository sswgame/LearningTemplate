#include "pch.h"

#include "Engine/Serialization/Format/StateIO.h"

#include "Core/Math/Math.h"
#include "Core/Time/Countdown.h"
#include "Core/Time/FixedStepTimer.h"

namespace sw
{
    void StateIO::value( hashed_string& inoutName )
    {
        if ( isReading() )
        {
            (void)readName( _archive, inoutName ); // 실패는 아카이브 오류에 남는다
            return;
        }
        writeName( _archive, inoutName );
    }

    void StateIO::value( string& inoutText )
    {
        if ( isReading() )
        {
            _archive >> inoutText;
            return;
        }
        _archive << string_view( inoutText );
    }

    void StateIO::value( int2& inoutValue )
    {
        if ( isReading() )
        {
            readInt2( _archive, inoutValue );
            return;
        }
        writeInt2( _archive, inoutValue );
    }

    void StateIO::value( Countdown& inoutCountdown )
    {
        if ( isReading() )
        {
            check( readCountdown( _archive, inoutCountdown ) );
            return;
        }
        writeCountdown( _archive, inoutCountdown );
    }

    void StateIO::value( FixedStepTimer& inoutTimer )
    {
        if ( isReading() )
        {
            check( readStepTimer( _archive, inoutTimer ) );
            return;
        }
        writeStepTimer( _archive, inoutTimer );
    }

    void StateIO::value( RateAccumulator& inoutAccumulator )
    {
        if ( isReading() )
        {
            check( readRateAccumulator( _archive, inoutAccumulator ) );
            return;
        }
        writeRateAccumulator( _archive, inoutAccumulator );
    }

    void StateIO::valueRandom( DeterministicRandom32& inoutRandom )
    {
        if ( isReading() )
        {
            check( readRandom( _archive, inoutRandom ) );
            return;
        }
        writeRandom( _archive, inoutRandom );
    }

    void StateIO::header( uint32 tag, uint32 version )
    {
        if ( isReading() )
        {
            check( readHeader( _archive, tag, version ) );
            return;
        }
        writeHeader( _archive, tag, version );
    }

    bool StateIO::check( bool bCondition )
    {
        if ( isReading() && bCondition == false )
        {
            _archive.setError();
        }
        return isOk();
    }

    void StateIO::count( uint32 minBytesPerElement, uint32& inoutCount )
    {
        if ( isReading() )
        {
            (void)_archive.readCount( minBytesPerElement, inoutCount ); // 실패는 아카이브 오류에 남는다
            return;
        }
        _archive << inoutCount;
    }

    void StateIO::writeHeader( Archive& outArchive, uint32 tag, uint32 version )
    {
        outArchive << tag;
        outArchive << version;
    }

    bool StateIO::readHeader( Archive& archive, uint32 tag, uint32 version )
    {
        uint32 readTag     = 0;
        uint32 readVersion = 0;
        archive >> readTag;
        archive >> readVersion;
        return archive.isOk() && readTag == tag && readVersion == version;
    }

    void StateIO::writeSection( Archive& outArchive, uint32 tag, uint32 version, const Archive& body )
    {
        writeHeader( outArchive, tag, version );
        outArchive.writeSection( body.getData(), static_cast<uint32>( body.getSize() ) );
    }

    bool StateIO::readSection( Archive& archive, uint32& outTag, uint32& outVersion, Archive& outBody )
    {
        uint32 tag      = 0;
        uint32 version  = 0;
        uint32 bodySize = 0;
        archive >> tag;
        archive >> version;
        archive >> bodySize;
        if ( archive.isError() || archive.hasBytesAvailable( bodySize ) == false )
        {
            archive.setError();
            return false;
        }
        outBody    = archive.readSubArchive( bodySize );
        outTag     = tag;
        outVersion = version;
        return outBody.isOk();
    }

    bool StateIO::readExpectedSection( Archive& archive, uint32 tag, uint32 version, Archive& outBody )
    {
        uint32 readTag     = 0;
        uint32 readVersion = 0;
        return readSection( archive, readTag, readVersion, outBody ) && readTag == tag && readVersion == version;
    }

    void StateIO::writeName( Archive& outArchive, const hashed_string& name )
    {
        // 빈 이름의 c_str() 는 "None" 이다 - 그대로 쓰면 읽을 때 "None" 이라는 이름으로 돌아온다. 빈 이름은 빈 글로 쓴다.
        outArchive << name.viewOrEmpty();
    }

    bool StateIO::readName( Archive& archive, hashed_string& outName )
    {
        string text;
        archive >> text;
        if ( archive.isError() )
            return false;
        outName = text.empty() ? hashed_string{} : hashed_string( text );
        return true;
    }

    namespace
    {
        template <typename TID>
        void writeIDListImpl( Archive& outArchive, const vector<TID>& listID )
        {
            outArchive << static_cast<uint32>( listID.size() );
            for ( const TID id : listID )
            {
                outArchive << id;
            }
        }

        template <typename TID>
        [[nodiscard]] bool readIDListImpl( Archive& archive, vector<TID>& outListID )
        {
            uint32 count = 0;
            if ( archive.readCount( sizeof( TID ), count ) == false )
                return false;
            outListID.resize( count );
            for ( TID& id : outListID )
            {
                archive >> id;
            }
            return archive.isOk();
        }
    } // namespace

    void StateIO::writeIDList( Archive& outArchive, const vector<uint32>& listID )
    {
        writeIDListImpl( outArchive, listID );
    }
    bool StateIO::readIDList( Archive& archive, vector<uint32>& outListID )
    {
        return readIDListImpl( archive, outListID );
    }
    void StateIO::writeIDList( Archive& outArchive, const vector<uint64>& listID )
    {
        writeIDListImpl( outArchive, listID );
    }
    bool StateIO::readIDList( Archive& archive, vector<uint64>& outListID )
    {
        return readIDListImpl( archive, outListID );
    }

    void StateIO::writeInt2( Archive& outArchive, const int2& value )
    {
        outArchive << value._x;
        outArchive << value._y;
    }

    void StateIO::readInt2( Archive& archive, int2& outValue )
    {
        archive >> outValue._x;
        archive >> outValue._y;
    }

    void StateIO::writeRandom( Archive& outArchive, const DeterministicRandom32& random )
    {
        outArchive << random.getState();
    }

    bool StateIO::readRandom( Archive& archive, DeterministicRandom32& outRandom )
    {
        uint32 state = 0;
        archive >> state;
        if ( archive.isError() || state == 0 )
            return false;
        outRandom.setSeed( state ); // 상태는 0 이 아니다 — 씨앗으로 다시 두면 같은 수열이 이어진다
        return true;
    }

    void StateIO::writeStepTimer( Archive& outArchive, const FixedStepTimer& timer )
    {
        outArchive << timer._accumulator;
    }

    bool StateIO::readStepTimer( Archive& archive, FixedStepTimer& inoutTimer )
    {
        float32 accumulator = 0.0f;
        archive >> accumulator;
        // 터무니없는 값은 `consume` 의 빼기가 float 정밀도에 먹혀 끝나지 않는다(1e30 - 걸음 == 1e30). 한 프레임이 받는 시간까지만 받는다 — 걸음을 조정한 핫 리로드는 그대로 읽힌다.
        const float32 maxAccumulator = inoutTimer._step > inoutTimer._maxFrameTime ? inoutTimer._step : inoutTimer._maxFrameTime;
        if ( archive.isError() || accumulator < 0.0f || accumulator > maxAccumulator )
            return false;
        inoutTimer._accumulator = accumulator;
        return true;
    }

    void StateIO::writeCountdown( Archive& outArchive, const Countdown& countdown )
    {
        outArchive << countdown._remaining;
    }

    bool StateIO::readCountdown( Archive& archive, Countdown& outCountdown )
    {
        float32 remaining = 0.0f;
        archive >> remaining;
        // NaN 은 자기 자신과 같지 않다
        if ( archive.isError() || ( remaining == remaining ) == false )
            return false;
        outCountdown._remaining = remaining;
        return true;
    }

    void StateIO::writeRateAccumulator( Archive& outArchive, const RateAccumulator& accumulator )
    {
        outArchive << accumulator._fraction;
    }

    bool StateIO::readRateAccumulator( Archive& archive, RateAccumulator& outAccumulator )
    {
        float32 fraction = 0.0f;
        archive >> fraction;
        // 정수가 된 몫은 그때그때 빼 간다. 터무니없는 값은 `takeOne` 고리가 끝나지 않는다(1e30 - 1 == 1e30).
        if ( archive.isError() || fraction < 0.0f || 1.0f <= fraction )
            return false;
        outAccumulator._fraction = fraction;
        return true;
    }
} // namespace sw
