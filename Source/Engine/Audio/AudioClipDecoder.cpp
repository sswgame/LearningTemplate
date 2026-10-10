#include "pch.h"

#include "Engine/Audio/AudioClipDecoder.h"

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

            /** @brief 확장형 하위 형식 GUID 에서 "PCM · IEEE float" 뒤 12 바이트입니다(KSDATAFORMAT_SUBTYPE_* 공통 꼬리). */
            inline static constexpr uint8 kArrSubFormatTail[12] = { 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };

            static uint16 readUint16LE( const uint8* pBytes ) { return static_cast<uint16>( pBytes[0] | ( pBytes[1] << 8 ) ); }

            static uint32 readUint32LE( const uint8* pBytes )
            {
                return static_cast<uint32>( pBytes[0] ) | ( static_cast<uint32>( pBytes[1] ) << 8 ) | ( static_cast<uint32>( pBytes[2] ) << 16 ) |
                       ( static_cast<uint32>( pBytes[3] ) << 24 );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AudioClipDecoder::decodeWav( const uint8* pBytes, size_t byteCount, AudioPcm& outPcm )
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
        uint16       baseFormatTag{ 0 }; ///< PCM · IEEE float — 확장형이면 하위 형식에서 읽는다

        while ( offset + 8 <= byteCount )
        {
            const uint8* pChunkID  = pBytes + offset;
            const uint32 chunkSize = Internal::readUint32LE( pBytes + offset + 4 );
            offset += 8;
            if ( chunkSize > byteCount - offset )
                break;

            if ( Memory::compare( pChunkID, "fmt ", 4 ) == 0 && chunkSize >= 16 )
            {
                const uint8* pFmt  = pBytes + offset;
                baseFormatTag      = Internal::readUint16LE( pFmt + 0 );
                pcm._channelCount  = Internal::readUint16LE( pFmt + 2 );
                pcm._sampleRate    = Internal::readUint32LE( pFmt + 4 );
                pcm._bitsPerSample = Internal::readUint16LE( pFmt + 14 );
                // 확장형: 유효 비트 · 채널 마스크 · 하위 형식 GUID 가 뒤에 온다. 하위 형식이 PCM · IEEE float 일 때만 받는다.
                if ( baseFormatTag == Internal::kWaveFormatExtensible )
                {
                    baseFormatTag = 0;
                    if ( chunkSize >= 40 && Memory::compare( pFmt + 28, Internal::kArrSubFormatTail, sizeof( Internal::kArrSubFormatTail ) ) == 0 )
                    {
                        pcm._bExtensible        = SW_TRUE;
                        pcm._validBitsPerSample = Internal::readUint16LE( pFmt + 18 );
                        pcm._channelMask        = Internal::readUint32LE( pFmt + 20 );
                        baseFormatTag           = static_cast<uint16>( Internal::readUint32LE( pFmt + 24 ) );
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

        if ( bHaveFmt == false || pData == nullptr || dataSize == 0 )
            return false;
        if ( ( baseFormatTag != Internal::kWaveFormatPcm && baseFormatTag != Internal::kWaveFormatIeeeFloat ) || pcm._channelCount == 0 || pcm._sampleRate == 0 ||
             pcm._bitsPerSample == 0 || ( pcm._bitsPerSample % 8u ) != 0 )
            return false;

        pcm._bFloat = baseFormatTag == Internal::kWaveFormatIeeeFloat ? SW_TRUE : SW_FALSE;
        pcm._listData.assign( pData, pData + dataSize );
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
