#include "pch.h"

#include "Engine/Serialization/Format/FormatDef.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Serialization/ByteStream.h"
#include "Core/Serialization/LittleEndianUtil.h"

#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    namespace
    {
        static_assert( FormatDef{ "SWPK" }._tag == FourCCUtil::make( "SWPK" ), "a format tag is the FourCC of its letters" );
        constexpr const utf8* kFormatHeadCut = "the bytes are shorter than a format head";
    } // namespace

    void FormatDef::write( vector<uint8>& outBytes ) const
    {
        LittleEndianUtil::appendUint32( outBytes, _tag );
        LittleEndianUtil::appendUint32( outBytes, _version );
    }

    void FormatDef::write( Archive& outArchive ) const
    {
        outArchive << _tag << _version;
    }

    void FormatDef::write( BitWriter& outWriter ) const
    {
        outWriter.writeUint32( _tag );
        outWriter.writeVarUint( _version );
    }

    expected<void> FormatDef::check( ByteReader& reader, size_t headBytes ) const
    {
        const bool   bShort  = reader.getRemainingByteCount() < headBytes;
        const uint32 tag     = reader.readUint32();
        const uint32 version = reader.readUint32();
        if ( bShort || reader.hasOverflowed() )
            return unexpected( Error{ ErrorKind::InvalidArgument, kFormatHeadCut } );
        return check( tag, version );
    }

    expected<void> FormatDef::check( Archive& archive ) const
    {
        uint32 tag     = 0;
        uint32 version = 0;
        archive >> tag >> version;
        if ( archive.isError() )
            return unexpected( Error{ ErrorKind::InvalidArgument, kFormatHeadCut } );
        return check( tag, version );
    }

    expected<void> FormatDef::check( BitReader& reader ) const
    {
        const uint32 tag     = reader.readUint32();
        const uint32 version = reader.readVarUint32();
        if ( reader.hasOverflowed() )
            return unexpected( Error{ ErrorKind::InvalidArgument, kFormatHeadCut } );
        return check( tag, version );
    }

    expected<void> FormatDef::check( uint32 tag, uint32 version ) const
    {
        if ( tag != _tag )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the bytes are another format - expected", makeTagText().view() } );
        if ( version == _version )
            return {};
        StringBuilder<constant::buffer::kMax64> detail;
        detail.appendFormat( "%# version %#, this build reads %#", makeTagText(), version, _version );
        return unexpected( Error{ ErrorKind::Unsupported, version > _version ? "the bytes were written by a newer build" : "the bytes are an older version - write them again", detail.view() } );
    }

    void FormatDef::writeVersion( BitWriter& outWriter ) const
    {
        outWriter.writeVarUint( _version );
    }

    bool FormatDef::readVersion( BitReader& reader ) const
    {
        const uint32 version = reader.readVarUint32();
        return reader.hasOverflowed() == false && version == _version;
    }

    bool FormatDef::report( const expected<void>& checked, string_view sourceName )
    {
        if ( checked.has_value() == false )
            SW_LOG_ERROR_ID( "Format.HeadRefused", "'%#' is not read: %# (%#)", sourceName, checked.error().message(), checked.error().detail() );
        return checked.has_value();
    }

    bool FormatDef::raise( uint32 fromVersion, vector<uint8>& inoutBody ) const
    {
        if ( fromVersion == _version )
            return true;
        if ( fromVersion > _version || _pSteps == nullptr )
        {
            reportCannotRaise( makeTagText().view(), fromVersion, _version );
            return false;
        }
        for ( uint32 version = fromVersion; version < _version; ++version )
        {
            if ( runStep( _pSteps[version], inoutBody ) == false )
            {
                SW_LOG_ERROR_ID( "FormatUpgrade.StepFailed", "%# format upgrade %# -> %# failed", makeTagText(), version, version + 1 );
                return false;
            }
        }
        return true;
    }

    void FormatDef::reportCannotRaise( string_view kind, uint32 fromVersion, uint32 toVersion )
    {
        if ( fromVersion > toVersion )
            SW_LOG_ERROR_ID( "FormatUpgrade.NewerThanSupported", "%# format version %# is newer than the supported %# - written by a newer build", kind, fromVersion, toVersion );
        else
            SW_LOG_ERROR_ID( "FormatUpgrade.StepMissing", "%# format version %# has no upgrade step towards %# - add the step or rewrite the data", kind, fromVersion, toVersion );
    }

    bool FormatDef::runStep( FormatStep pStep, vector<uint8>& inoutBody )
    {
        Archive oldBody( inoutBody.data(), inoutBody.size() );
        Archive newBody;
        if ( pStep( oldBody, newBody ) == false || oldBody.isError() || newBody.isError() )
            return false;
        // 단계가 읽지 않은 꼬리(바깥 형식의 칸)는 그대로 옮긴다.
        const uint64 tailSize = oldBody.getRemainingBytes();
        if ( tailSize > 0 )
            newBody.writeBytes( inoutBody.data() + oldBody.getOffset(), tailSize );
        newBody.writeData( inoutBody );
        return true;
    }

    fixed_string<constant::buffer::kMax16> FormatDef::makeTagText() const
    {
        const utf8                             arrLetter[] = { static_cast<utf8>( _tag & 0xFFu ), static_cast<utf8>( ( _tag >> 8 ) & 0xFFu ), static_cast<utf8>( ( _tag >> 16 ) & 0xFFu ),
                                                               static_cast<utf8>( ( _tag >> 24 ) & 0xFFu ) };
        fixed_string<constant::buffer::kMax16> text;
        text = string_view{ arrLetter, sizeof( arrLetter ) };
        return text;
    }
} // namespace sw
