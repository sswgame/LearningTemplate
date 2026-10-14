#include "pch.h"

#include "Engine/Audio/AudioClipDecoder.h"

#include "Core/Container/LittleEndianUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    namespace
    {
        struct AudioClipDecoderInternal
        {
            static constexpr uint16 kWaveFormatPcm        = 0x0001;
            static constexpr uint16 kWaveFormatIeeeFloat  = 0x0003;
            static constexpr uint16 kWaveFormatExtensible = 0xFFFE;

            /** @brief 확장형 하위 형식 GUID 에서 "PCM, IEEE float" 뒤에 오는 12 바이트입니다(KSDATAFORMAT_SUBTYPE_* 공통 꼬리). */
            inline static constexpr uint8 kArrSubFormatTail[12] = { 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AudioClipDecoder::parseWavLayout( const uint8* pBytes, size_t byteCount, AudioPcm& outFormat, size_t& outDataOffset, uint32& outDataSize )
    {
        using Internal = AudioClipDecoderInternal;
        if ( pBytes == nullptr || byteCount < 44 )
            return false;
        if ( Memory::compare( pBytes, "RIFF", 4 ) != 0 || Memory::compare( pBytes + 8, "WAVE", 4 ) != 0 )
            return false;

        AudioPcm     pcm;
        size_t       offset = 12;
        const uint8* pData  = nullptr;
        uint32       dataSize{ 0 };
        bool         bHaveFmt{ false };
        uint16       baseFormatTag{ 0 }; ///< PCM 이나 IEEE float 이다. 확장형이면 하위 형식에서 읽는다.

        while ( offset + 8 <= byteCount )
        {
            const uint8* pChunkID  = pBytes + offset;
            const uint32 chunkSize = LittleEndianUtil::readUint32( pBytes + offset + 4 );
            offset += 8;
            if ( chunkSize > byteCount - offset )
                break;

            if ( Memory::compare( pChunkID, "fmt ", 4 ) == 0 && chunkSize >= 16 )
            {
                const uint8* pFmt  = pBytes + offset;
                baseFormatTag      = LittleEndianUtil::readUint16( pFmt + 0 );
                pcm._channelCount  = LittleEndianUtil::readUint16( pFmt + 2 );
                pcm._sampleRate    = LittleEndianUtil::readUint32( pFmt + 4 );
                pcm._bitsPerSample = LittleEndianUtil::readUint16( pFmt + 14 );
                // 확장형이면 유효 비트, 채널 마스크, 하위 형식 GUID 가 뒤에 온다. 하위 형식이 PCM 이나 IEEE float 일 때만 받는다.
                if ( baseFormatTag == Internal::kWaveFormatExtensible )
                {
                    baseFormatTag = 0;
                    if ( chunkSize >= 40 && Memory::compare( pFmt + 28, Internal::kArrSubFormatTail, sizeof( Internal::kArrSubFormatTail ) ) == 0 )
                    {
                        pcm._bExtensible        = SW_TRUE;
                        pcm._validBitsPerSample = LittleEndianUtil::readUint16( pFmt + 18 );
                        pcm._channelMask        = LittleEndianUtil::readUint32( pFmt + 20 );
                        baseFormatTag           = static_cast<uint16>( LittleEndianUtil::readUint32( pFmt + 24 ) );
                    }
                }
                bHaveFmt = true;
            }
            else if ( Memory::compare( pChunkID, "data", 4 ) == 0 )
            {
                pData    = pBytes + offset;
                dataSize = chunkSize;
            }
            offset += chunkSize + ( chunkSize & 1u );
        }

        const bool bMissingWaveChunks = bHaveFmt == false || pData == nullptr || dataSize == 0;
        if ( bMissingWaveChunks )
            return false;
        const bool bUnsupportedWaveFormat = ( baseFormatTag != Internal::kWaveFormatPcm && baseFormatTag != Internal::kWaveFormatIeeeFloat ) || pcm._channelCount == 0 || pcm._sampleRate == 0 ||
                                            pcm._bitsPerSample == 0 || ( pcm._bitsPerSample % 8u ) != 0;
        if ( bUnsupportedWaveFormat )
            return false;

        pcm._bFloat   = baseFormatTag == Internal::kWaveFormatIeeeFloat ? SW_TRUE : SW_FALSE;
        outFormat     = std::move( pcm );
        outDataOffset = static_cast<size_t>( pData - pBytes );
        outDataSize   = dataSize;
        return true;
    }

    bool AudioClipDecoder::decodeWav( const uint8* pBytes, size_t byteCount, AudioPcm& outPcm )
    {
        AudioPcm pcm;
        size_t   dataOffset{ 0 };
        uint32   dataSize{ 0 };
        if ( parseWavLayout( pBytes, byteCount, pcm, dataOffset, dataSize ) == false )
            return false;
        pcm._listData.assign( pBytes + dataOffset, pBytes + dataOffset + dataSize );
        outPcm = std::move( pcm );
        return true;
    }

    bool AudioClipDecoder::isSupportedExtension( string_view path )
    {
        return FileUtil::hasExtension( path, ".wav" ) || FileUtil::hasExtension( path, ".ogg" );
    }

    bool AudioClipDecoder::decode( string_view path, const uint8* pBytes, size_t byteCount, AudioPcm& outPcm )
    {
        if ( FileUtil::hasExtension( path, ".wav" ) )
            return decodeWav( pBytes, byteCount, outPcm );
        if ( FileUtil::hasExtension( path, ".ogg" ) )
            return decodeOgg( pBytes, byteCount, outPcm );
        return false;
    }
} // namespace sw
