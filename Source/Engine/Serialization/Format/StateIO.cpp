#include "pch.h"

#include "Engine/Serialization/Format/StateIO.h"

#include "Core/Log/Logger.h"
#include "Core/Math/Math.h"
#include "Core/Time/Countdown.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Serialization/Format/FormatUpgrade.h"

namespace sw
{
    void StateIO::value( hashed_string& inoutName )
    {
        if ( isReading() )
        {
            (void)readName( *_pArchive, inoutName ); // 실패는 아카이브 오류에 남는다
            return;
        }
        writeName( *_pArchive, inoutName );
    }

    void StateIO::value( string& inoutText )
    {
        if ( isReading() )
        {
            *_pArchive >> inoutText;
            return;
        }
        *_pArchive << string_view( inoutText );
    }

    void StateIO::value( int2& inoutValue )
    {
        if ( isReading() )
        {
            readInt2( *_pArchive, inoutValue );
            return;
        }
        writeInt2( *_pArchive, inoutValue );
    }

    void StateIO::value( Countdown& inoutCountdown )
    {
        if ( isReading() )
        {
            check( readCountdown( *_pArchive, inoutCountdown ) );
            return;
        }
        writeCountdown( *_pArchive, inoutCountdown );
    }

    void StateIO::value( FixedStepTimer& inoutTimer )
    {
        if ( isReading() )
        {
            check( readStepTimer( *_pArchive, inoutTimer ) );
            return;
        }
        writeStepTimer( *_pArchive, inoutTimer );
    }

    void StateIO::value( RateAccumulator& inoutAccumulator )
    {
        if ( isReading() )
        {
            check( readRateAccumulator( *_pArchive, inoutAccumulator ) );
            return;
        }
        writeRateAccumulator( *_pArchive, inoutAccumulator );
    }

    void StateIO::valueRandom( DeterministicRandom32& inoutRandom )
    {
        if ( isReading() )
        {
            check( readRandom( *_pArchive, inoutRandom ) );
            return;
        }
        writeRandom( *_pArchive, inoutRandom );
    }

    StateIO::~StateIO()
    {
        if ( _pUpgradedArchive == nullptr )
            return;
        if ( _pUpgradedArchive->isError() )
        {
            _pSourceArchive->setError();
            return;
        }
        // 단계는 머리 뒤 남은 꼬리를 그대로 옮긴다 — 읽지 않고 남은 바이트는 원래 아카이브의 같은 꼬리다.
        (void)_pSourceArchive->setOffset( _pSourceArchive->getSize() - _pUpgradedArchive->getRemainingBytes() );
    }

    void StateIO::header( uint32 tag, uint32 version, uint32 headerlessVersion )
    {
        if ( isWriting() )
        {
            writeHeader( *_pArchive, tag, version );
            return;
        }
        const uint64 start       = _pArchive->getOffset();
        uint32       readTag     = 0;
        uint32       readVersion = 0;
        *_pArchive >> readTag;
        *_pArchive >> readVersion;
        if ( check( _pArchive->isOk() ) == false )
            return;
        if ( readTag != tag )
        {
            if ( headerlessVersion == 0 )
            {
                SW_LOG_WARNING_ID( "StateIO.HeaderTagMismatch",
                                   "State header tag %# is not the expected %# - the bytes are another format or a version from before this format had a header; that version cannot be known, so it is refused (no upgrade)",
                                   readTag, tag );
                fail();
                return;
            }
            // 머리표가 없던 판이다 — 머리를 읽지 않은 자리로 돌아가 그 판의 칸으로 올린다.
            (void)_pArchive->setOffset( start );
            readVersion = headerlessVersion;
        }
        if ( readVersion == version )
            return;
        // 옛 판 — 머리 뒤 바이트를 사슬로 올리고 그 바이트에서 이어 읽는다. 한 입구에서 두 번 올리는 일은 없다(안쪽 형식의 머리는 올린 바이트 안에서 다시 올린다).
        vector<uint8> bytes;
        readRemainingBytes( *_pArchive, bytes );
        if ( check( upgradeBytes( tag, readVersion, version, bytes ) ) == false )
            return;
        unique_ptr<Archive> pUpgraded = make_unique<Archive>();
        pUpgraded->writeBytes( bytes.data(), bytes.size() );
        pUpgraded->setReadModeAndResetPosition( true );
        _pUpgradedArchive = std::move( pUpgraded ); // 안쪽 형식이 올린 바이트 안에서 또 올렸으면 앞 버퍼는 이미 다 옮겼다
        _pArchive         = _pUpgradedArchive.get();
    }

    void StateIO::readRemainingBytes( Archive& archive, vector<uint8>& outBytes )
    {
        const uint64 remaining = archive.getRemainingBytes();
        outBytes.clear();
        if ( remaining == 0 )
            return;
        const uint8* pBytes = archive.readBytesView( remaining );
        if ( pBytes != nullptr )
            outBytes.assign( pBytes, pBytes + remaining );
    }

    bool StateIO::upgradeBytes( uint32 tag, uint32 fromVersion, uint32 toVersion, vector<uint8>& inoutBytes )
    {
        // 종류 이름은 표의 네 글자다(`FourCCUtil::make` 의 바이트 순서 — 파일 앞에서 글자 순서 그대로).
        const utf8           kind[4] = { static_cast<utf8>( tag & 0xFFu ), static_cast<utf8>( ( tag >> 8 ) & 0xFFu ), static_cast<utf8>( ( tag >> 16 ) & 0xFFu ), static_cast<utf8>( ( tag >> 24 ) & 0xFFu ) };
        FormatUpgradePayload payload{};
        payload._pBytes = &inoutBytes;
        return FormatUpgradeStep::upgrade( FormatUpgradeDomain::StateBytes, string_view{ kind, 4 }, fromVersion, toVersion, payload );
    }

    bool StateIO::upgradeSectionBody( uint32 tag, uint32 fromVersion, uint32 toVersion, Archive& inoutBody )
    {
        if ( fromVersion == toVersion )
            return true;
        vector<uint8> bytes;
        Archive       body = inoutBody; // 실패하면 받은 본문을 그대로 둔다
        readRemainingBytes( body, bytes );
        if ( upgradeBytes( tag, fromVersion, toVersion, bytes ) == false )
            return false;
        Archive upgraded;
        upgraded.writeBytes( bytes.data(), bytes.size() );
        upgraded.setReadModeAndResetPosition( true );
        inoutBody = std::move( upgraded );
        return true;
    }

    bool StateIO::check( bool bCondition )
    {
        if ( isReading() && bCondition == false )
        {
            _pArchive->setError();
        }
        return isOk();
    }

    void StateIO::count( uint32 minBytesPerElement, uint32& inoutCount )
    {
        if ( isReading() )
        {
            (void)_pArchive->readCount( minBytesPerElement, inoutCount ); // 실패는 아카이브 오류에 남는다
            return;
        }
        *_pArchive << inoutCount;
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
        return readSection( archive, readTag, readVersion, outBody ) && readTag == tag && upgradeSectionBody( tag, readVersion, version, outBody );
    }

    void StateIO::writeName( Archive& outArchive, const hashed_string& name )
    {
        // 빈 이름은 빈 글로 쓴다(`view()` 는 빈 이름이면 빈 글이다 — "None" 을 쓰면 읽을 때 "None" 이라는 이름으로 볼 쪽이 생긴다).
        outArchive << name.view();
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
