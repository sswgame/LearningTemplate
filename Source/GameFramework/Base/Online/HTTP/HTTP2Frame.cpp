#include "pch.h"

#include "GameFramework/Base/Online/HTTP/HTTP2Frame.h"

namespace sw
{
    namespace
    {
        void appendUint32( uint32 value, vector<uint8>& outBytes )
        {
            outBytes.push_back( static_cast<uint8>( value >> 24 ) );
            outBytes.push_back( static_cast<uint8>( value >> 16 ) );
            outBytes.push_back( static_cast<uint8>( value >> 8 ) );
            outBytes.push_back( static_cast<uint8>( value ) );
        }

        /** @brief 머리를 쓰기 전에 길이 · 스트림 id 가 범위 안인지 보고 쓰는 공통 길입니다. */
        bool beginFrame( HTTP2FrameType type, uint8 flags, uint32 streamID, size_t bodySize, vector<uint8>& outBytes )
        {
            HTTP2FrameHeader header;
            header._length   = static_cast<uint32>( bodySize );
            header._type     = static_cast<uint8>( type );
            header._flags    = flags;
            header._streamID = streamID;
            return bodySize <= HTTP2FrameUtil::kMaxFrameLength && HTTP2FrameUtil::writeHeader( header, outBytes );
        }
    } // namespace

    uint32 HTTP2FrameUtil::readUint32( const uint8* pBytes )
    {
        return ( static_cast<uint32>( pBytes[0] ) << 24 ) | ( static_cast<uint32>( pBytes[1] ) << 16 ) | ( static_cast<uint32>( pBytes[2] ) << 8 ) | pBytes[3];
    }

    bool HTTP2FrameUtil::writeHeader( const HTTP2FrameHeader& header, vector<uint8>& outBytes )
    {
        if ( header._length > kMaxFrameLength || header._streamID > kMaxStreamID )
            return false;
        outBytes.push_back( static_cast<uint8>( header._length >> 16 ) );
        outBytes.push_back( static_cast<uint8>( header._length >> 8 ) );
        outBytes.push_back( static_cast<uint8>( header._length ) );
        outBytes.push_back( header._type );
        outBytes.push_back( header._flags );
        appendUint32( header._streamID, outBytes );
        return true;
    }

    bool HTTP2FrameUtil::readHeader( const uint8* pBytes, size_t size, HTTP2FrameHeader& outHeader )
    {
        if ( size < kHeaderSize )
            return false;
        outHeader._length   = ( static_cast<uint32>( pBytes[0] ) << 16 ) | ( static_cast<uint32>( pBytes[1] ) << 8 ) | pBytes[2];
        outHeader._type     = pBytes[3];
        outHeader._flags    = pBytes[4];
        outHeader._streamID = readUint32( pBytes + 5 ) & kMaxStreamID;
        return true;
    }

    bool HTTP2FrameUtil::writeSettings( const vector<HTTP2Setting>& listSetting, bool bAck, vector<uint8>& outBytes )
    {
        if ( bAck && listSetting.empty() == false )
            return false; // 확인 프레임은 본문이 없다
        if ( beginFrame( HTTP2FrameType::Settings, bAck ? kFlagAck : 0, 0, listSetting.size() * 6, outBytes ) == false )
            return false;
        for ( const HTTP2Setting& setting : listSetting )
        {
            outBytes.push_back( static_cast<uint8>( setting._id >> 8 ) );
            outBytes.push_back( static_cast<uint8>( setting._id ) );
            appendUint32( setting._value, outBytes );
        }
        return true;
    }

    bool HTTP2FrameUtil::readSettings( const uint8* pBody, size_t size, vector<HTTP2Setting>& outListSetting )
    {
        outListSetting.clear();
        if ( size % 6 != 0 )
            return false;
        for ( size_t offset = 0; offset < size; offset += 6 )
        {
            HTTP2Setting setting;
            setting._id    = static_cast<uint16>( ( static_cast<uint16>( pBody[offset] ) << 8 ) | pBody[offset + 1] );
            setting._value = readUint32( pBody + offset + 2 );
            outListSetting.push_back( setting );
        }
        return true;
    }

    bool HTTP2FrameUtil::writeWindowUpdate( uint32 streamID, uint32 increment, vector<uint8>& outBytes )
    {
        if ( increment == 0 || increment > kMaxStreamID )
            return false;
        if ( beginFrame( HTTP2FrameType::WindowUpdate, 0, streamID, 4, outBytes ) == false )
            return false;
        appendUint32( increment, outBytes );
        return true;
    }

    bool HTTP2FrameUtil::writePing( const uint8 ( &arrOpaque )[8], bool bAck, vector<uint8>& outBytes )
    {
        if ( beginFrame( HTTP2FrameType::Ping, bAck ? kFlagAck : 0, 0, 8, outBytes ) == false )
            return false;
        outBytes.insert( outBytes.end(), arrOpaque, arrOpaque + 8 );
        return true;
    }

    bool HTTP2FrameUtil::writeHeaders( uint32 streamID, const vector<uint8>& listHeaderBlock, bool bEndStream, uint32 maxFrameSize, vector<uint8>& outBytes )
    {
        if ( streamID == 0 || listHeaderBlock.size() > maxFrameSize )
            return false;
        const uint8 flags = static_cast<uint8>( kFlagEndHeaders | ( bEndStream ? kFlagEndStream : 0 ) );
        if ( beginFrame( HTTP2FrameType::Headers, flags, streamID, listHeaderBlock.size(), outBytes ) == false )
            return false;
        outBytes.insert( outBytes.end(), listHeaderBlock.begin(), listHeaderBlock.end() );
        return true;
    }

    bool HTTP2FrameUtil::writeData( uint32 streamID, const uint8* pData, size_t size, bool bEndStream, uint32 maxFrameSize, vector<uint8>& outBytes )
    {
        if ( streamID == 0 || size > maxFrameSize )
            return false;
        if ( beginFrame( HTTP2FrameType::Data, bEndStream ? kFlagEndStream : 0, streamID, size, outBytes ) == false )
            return false;
        if ( size > 0 )
            outBytes.insert( outBytes.end(), pData, pData + size );
        return true;
    }

    bool HTTP2FrameUtil::writeRstStream( uint32 streamID, uint32 errorCode, vector<uint8>& outBytes )
    {
        if ( streamID == 0 || beginFrame( HTTP2FrameType::RstStream, 0, streamID, 4, outBytes ) == false )
            return false;
        appendUint32( errorCode, outBytes );
        return true;
    }

    bool HTTP2FrameUtil::readGoAway( const uint8* pBody, size_t size, HTTP2GoAway& outGoAway )
    {
        if ( size < 8 )
            return false;
        outGoAway._lastStreamID = readUint32( pBody ) & kMaxStreamID;
        outGoAway._errorCode    = readUint32( pBody + 4 );
        return true;
    }
} // namespace sw
