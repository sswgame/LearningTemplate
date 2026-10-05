#include "pch.h"

#include "GameFramework/Base/Utility/StateArchiveUtil.h"

#include "Core/Container/string.h"
#include "Core/Math/Math.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Utility/FixedStepTimer.h"
#include "GameFramework/Base/Utility/GameRandom.h"

namespace sw
{
    void StateArchiveUtil::writeHeader( Archive& outArchive, uint32 tag, uint32 version )
    {
        outArchive << tag;
        outArchive << version;
    }

    bool StateArchiveUtil::readHeader( Archive& archive, uint32 tag, uint32 version )
    {
        uint32 readTag     = 0;
        uint32 readVersion = 0;
        archive >> readTag;
        archive >> readVersion;
        return archive.isOk() && readTag == tag && readVersion == version;
    }

    void StateArchiveUtil::writeName( Archive& outArchive, const hashed_string& name )
    {
        outArchive << string_view( name.c_str() );
    }

    bool StateArchiveUtil::readName( Archive& archive, hashed_string& outName )
    {
        string text;
        archive >> text;
        if ( archive.isError() )
            return false;
        outName = text.empty() ? hashed_string{} : hashed_string( text );
        return true;
    }

    bool StateArchiveUtil::readCount( Archive& archive, uint32 minBytesPerElement, uint32& outCount )
    {
        uint32 count = 0;
        archive >> count;
        if ( archive.isError() )
            return false;
        const uint64 minBytes = static_cast<uint64>( count ) * static_cast<uint64>( minBytesPerElement > 0 ? minBytesPerElement : 1 );
        if ( minBytes > archive.getRemainingBytes() )
        {
            archive.setError();
            return false;
        }
        outCount = count;
        return true;
    }

    void StateArchiveUtil::writeInt2( Archive& outArchive, const int2& value )
    {
        outArchive << value._x;
        outArchive << value._y;
    }

    void StateArchiveUtil::readInt2( Archive& archive, int2& outValue )
    {
        archive >> outValue._x;
        archive >> outValue._y;
    }

    void StateArchiveUtil::writeRandom( Archive& outArchive, const GameRandom& random )
    {
        outArchive << random.getState();
    }

    bool StateArchiveUtil::readRandom( Archive& archive, GameRandom& outRandom )
    {
        uint32 state = 0;
        archive >> state;
        if ( archive.isError() || state == 0 )
            return false;
        outRandom.setSeed( state ); // 상태는 0 이 아니다 — 씨앗으로 다시 두면 같은 수열이 이어진다
        return true;
    }

    void StateArchiveUtil::writeStepTimer( Archive& outArchive, const FixedStepTimer& timer )
    {
        outArchive << timer._accumulator;
    }

    bool StateArchiveUtil::readStepTimer( Archive& archive, FixedStepTimer& inoutTimer )
    {
        float32 accumulator = 0.0f;
        archive >> accumulator;
        if ( archive.isError() || accumulator < 0.0f )
            return false;
        inoutTimer._accumulator = accumulator;
        return true;
    }
} // namespace sw
