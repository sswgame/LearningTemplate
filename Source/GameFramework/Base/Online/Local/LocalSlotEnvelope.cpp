#include "pch.h"

#include "GameFramework/Base/Online/Local/LocalSlotEnvelope.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Container/StringUtil.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include <cstring>

namespace sw
{
    namespace
    {
        struct LocalSlotEnvelopeInternal
        {
            static constexpr uint8  kArrMagic[4] = { 'S', 'W', 'L', 'S' };
            static constexpr size_t kCrcSize     = 4;
            static constexpr size_t kKeyIDSize   = 8;
            static constexpr utf8   kSealInfo[]  = "swls-seal-v1";
            static constexpr utf8   kKeyIDInfo[] = "swls-key-id-v1";

            struct Header
            {
                uint64         _originalSize{ 0 };
                uint64         _bodySize{ 0 };
                uint64         _keyID{ 0 };
                uint32         _formatVersion{ 0 };
                uint8          _codec{ 0 };
                LocalStoreSeal _seal{ LocalStoreSeal::None };
            };

            static void writeUint32( uint8* pOut, uint32 value )
            {
                for ( int32 index = 0; index < 4; ++index )
                {
                    pOut[index] = static_cast<uint8>( value >> ( index * 8 ) );
                }
            }

            static void writeUint64( uint8* pOut, uint64 value )
            {
                for ( int32 index = 0; index < 8; ++index )
                {
                    pOut[index] = static_cast<uint8>( value >> ( index * 8 ) );
                }
            }

            static uint32 readUint32( const uint8* pData )
            {
                uint32 value = 0;
                for ( int32 index = 3; index >= 0; --index )
                {
                    value = ( value << 8 ) | pData[index];
                }
                return value;
            }

            static uint64 readUint64( const uint8* pData )
            {
                uint64 value = 0;
                for ( int32 index = 7; index >= 0; --index )
                {
                    value = ( value << 8 ) | pData[index];
                }
                return value;
            }

            static void writeHeader( const Header& header, uint8* pOut )
            {
                std::memcpy( pOut, kArrMagic, 4 );
                pOut[4] = LocalSlotEnvelope::kEnvelopeVersion;
                pOut[5] = static_cast<uint8>( header._seal );
                pOut[6] = header._codec;
                pOut[7] = 0;
                writeUint32( pOut + 8, header._formatVersion );
                writeUint64( pOut + 12, header._originalSize );
                writeUint64( pOut + 20, header._bodySize );
                writeUint64( pOut + 28, header._keyID );
            }

            static CompressionCodecRegistry* findRegistry( const LocalSealContext& context )
            {
                return context._pCodecRegistry != nullptr ? context._pCodecRegistry : CompressionCodecRegistry::getActive();
            }

            /** @brief 장치 키에서 봉인 키 · 키 표시를 가릅니다. 창구 · 키가 없으면 Invalid. */
            static LocalStoreResult deriveKeys( const LocalSealContext& context, uint8 ( &outSealKey )[NetSecurityConstant::kAeadKeySize], uint64& outKeyID )
            {
                if ( context._pSecurityProvider == nullptr || context._pKeyProvider == nullptr )
                    return LocalStoreResult::Invalid;
                uint8 arrDeviceKey[ILocalStoreKeyProvider::kKeySize] = {};
                if ( context._pKeyProvider->getSealKey( arrDeviceKey ) == false )
                    return LocalStoreResult::IoError;
                uint8      arrKeyID[kKeyIDSize] = {};
                const bool bDerived             = context._pSecurityProvider->computeHkdfSha256( arrDeviceKey, ILocalStoreKeyProvider::kKeySize, nullptr, 0,
                                                                                                 reinterpret_cast<const uint8*>( kSealInfo ), sizeof( kSealInfo ) - 1, outSealKey,
                                                                                                 NetSecurityConstant::kAeadKeySize ) &&
                                      context._pSecurityProvider->computeHkdfSha256( arrDeviceKey, ILocalStoreKeyProvider::kKeySize, nullptr, 0,
                                                                                     reinterpret_cast<const uint8*>( kKeyIDInfo ), sizeof( kKeyIDInfo ) - 1, arrKeyID,
                                                                                     static_cast<int32>( kKeyIDSize ) );
                std::memset( arrDeviceKey, 0, sizeof( arrDeviceKey ) );
                if ( bDerived == false )
                    return LocalStoreResult::IoError;
                outKeyID = readUint64( arrKeyID );
                return LocalStoreResult::Ok;
            }

            /** @brief 압축합니다. 코덱이 없거나 줄지 않으면 원문(코덱 0)입니다. */
            static LocalStoreResult compressBody( const LocalSealContext& context, const uint8* pBody, size_t bodySize, uint8 codec, vector<uint8>& outBytes,
                                                  uint8& outCodec )
            {
                outCodec = 0;
                outBytes.assign( pBody, pBody + bodySize );
                if ( codec == 0 || bodySize == 0 )
                    return LocalStoreResult::Ok;
                CompressionCodecRegistry* pRegistry = findRegistry( context );
                ICompressionCodec*        pCodec    = pRegistry != nullptr ? pRegistry->getCodec( static_cast<CompressionCodecType>( codec ) ) : nullptr;
                if ( pCodec == nullptr )
                    return LocalStoreResult::Invalid; // 등록되지 않은 코덱으로 쓰지 않는다(읽을 수 없는 슬롯이 된다)
                vector<uint8> compressed( pCodec->compressBound( bodySize ) );
                size_t        compressedSize = 0;
                if ( pCodec->compress( pBody, bodySize, compressed.data(), compressed.size(), compressedSize ) == false || compressedSize >= bodySize )
                    return LocalStoreResult::Ok;
                compressed.resize( compressedSize );
                outBytes = std::move( compressed );
                outCodec = codec;
                return LocalStoreResult::Ok;
            }

            static LocalStoreResult decompressBody( const LocalSealContext& context, const uint8* pBody, size_t bodySize, const Header& header, vector<uint8>& outBodyBytes )
            {
                if ( header._codec == 0 )
                {
                    if ( header._originalSize != bodySize )
                        return LocalStoreResult::Corrupt;
                    outBodyBytes.assign( pBody, pBody + bodySize );
                    return LocalStoreResult::Ok;
                }
                CompressionCodecRegistry* pRegistry = findRegistry( context );
                ICompressionCodec*        pCodec    = pRegistry != nullptr ? pRegistry->getCodec( static_cast<CompressionCodecType>( header._codec ) ) : nullptr;
                if ( pCodec == nullptr )
                    return LocalStoreResult::Corrupt; // 이 구성에 없는 코덱 — 다른 코덱으로 바꾸지 않는다
                outBodyBytes.resize( static_cast<size_t>( header._originalSize ) );
                size_t     decompressedSize = 0;
                const bool bDecompressed    = pCodec->decompress( pBody, bodySize, outBodyBytes.data(), outBodyBytes.size(), decompressedSize );
                if ( bDecompressed == false || decompressedSize != header._originalSize )
                    return LocalStoreResult::Corrupt;
                return LocalStoreResult::Ok;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LocalStoreResult LocalSlotEnvelope::seal( const LocalSealContext& context, const uint8* pBody, size_t bodySize, const LocalStoreWriteOptions& options,
                                              vector<uint8>& outEnvelopeBytes )
    {
        using Internal = LocalSlotEnvelopeInternal;
        outEnvelopeBytes.clear();
        if ( static_cast<int64>( bodySize ) > ILocalStore::kMaxSlotSize )
            return LocalStoreResult::Invalid;
        const bool bKnownSeal = options._seal == LocalStoreSeal::None || options._seal == LocalStoreSeal::Authenticated || options._seal == LocalStoreSeal::Encrypted;
        if ( bKnownSeal == false )
            return LocalStoreResult::Invalid;

        Internal::Header header;
        header._formatVersion                               = options._formatVersion;
        header._originalSize                                = bodySize;
        header._seal                                        = options._seal;
        uint8 arrSealKey[NetSecurityConstant::kAeadKeySize] = {};
        if ( options._seal != LocalStoreSeal::None )
        {
            const LocalStoreResult keyResult = Internal::deriveKeys( context, arrSealKey, header._keyID );
            if ( keyResult != LocalStoreResult::Ok )
                return keyResult;
        }
        vector<uint8>          storedBody;
        const LocalStoreResult compressResult = Internal::compressBody( context, pBody, bodySize, options._compressionCodec, storedBody, header._codec );
        if ( compressResult != LocalStoreResult::Ok )
            return compressResult;
        header._bodySize = storedBody.size();

        outEnvelopeBytes.resize( kHeaderSize );
        Internal::writeHeader( header, outEnvelopeBytes.data() );
        if ( options._seal == LocalStoreSeal::None )
        {
            outEnvelopeBytes.insert( outEnvelopeBytes.end(), storedBody.begin(), storedBody.end() );
            uint8 arrCrc[Internal::kCrcSize] = {};
            Internal::writeUint32( arrCrc, StringUtil::computeCrc32( outEnvelopeBytes.data(), outEnvelopeBytes.size() ) );
            outEnvelopeBytes.insert( outEnvelopeBytes.end(), arrCrc, arrCrc + Internal::kCrcSize );
            return LocalStoreResult::Ok;
        }

        unique_ptr<INetAead> aead = context._pSecurityProvider->createAead( NetAeadAlgorithm::Aes256Gcm, arrSealKey );
        std::memset( arrSealKey, 0, sizeof( arrSealKey ) );
        uint8 arrNonce[NetSecurityConstant::kAeadNonceSize] = {};
        if ( aead == nullptr || context._pSecurityProvider->fillRandomBytes( arrNonce, NetSecurityConstant::kAeadNonceSize ) == false )
            return LocalStoreResult::IoError;
        if ( options._seal == LocalStoreSeal::Authenticated )
        {
            outEnvelopeBytes.insert( outEnvelopeBytes.end(), storedBody.begin(), storedBody.end() );
            const size_t authenticatedSize = outEnvelopeBytes.size();
            outEnvelopeBytes.insert( outEnvelopeBytes.end(), arrNonce, arrNonce + NetSecurityConstant::kAeadNonceSize );
            uint8 arrTag[NetSecurityConstant::kAeadTagSize] = {};
            if ( aead->seal( arrNonce, outEnvelopeBytes.data(), static_cast<int32>( authenticatedSize ), nullptr, 0, arrTag ) == false )
                return LocalStoreResult::IoError;
            outEnvelopeBytes.insert( outEnvelopeBytes.end(), arrTag, arrTag + NetSecurityConstant::kAeadTagSize );
            return LocalStoreResult::Ok;
        }
        outEnvelopeBytes.insert( outEnvelopeBytes.end(), arrNonce, arrNonce + NetSecurityConstant::kAeadNonceSize );
        const size_t cipherOffset = outEnvelopeBytes.size();
        outEnvelopeBytes.resize( cipherOffset + storedBody.size() + NetSecurityConstant::kAeadTagSize );
        if ( aead->seal( arrNonce, outEnvelopeBytes.data(), static_cast<int32>( kHeaderSize ), storedBody.data(), static_cast<int32>( storedBody.size() ),
                         outEnvelopeBytes.data() + cipherOffset ) == false )
            return LocalStoreResult::IoError;
        return LocalStoreResult::Ok;
    }

    LocalStoreResult LocalSlotEnvelope::open( const LocalSealContext& context, const uint8* pEnvelopeBytes, size_t envelopeByteCount, vector<uint8>& outBodyBytes,
                                              uint32& outFormatVersion )
    {
        using Internal = LocalSlotEnvelopeInternal;
        outBodyBytes.clear();
        if ( envelopeByteCount < kHeaderSize || std::memcmp( pEnvelopeBytes, Internal::kArrMagic, 4 ) != 0 || pEnvelopeBytes[4] != kEnvelopeVersion )
            return LocalStoreResult::Corrupt;
        if ( pEnvelopeBytes[5] > static_cast<uint8>( LocalStoreSeal::Encrypted ) )
            return LocalStoreResult::Corrupt; // 모르는 봉인
        Internal::Header header;
        header._seal          = static_cast<LocalStoreSeal>( pEnvelopeBytes[5] );
        header._codec         = pEnvelopeBytes[6];
        header._formatVersion = Internal::readUint32( pEnvelopeBytes + 8 );
        header._originalSize  = Internal::readUint64( pEnvelopeBytes + 12 );
        header._bodySize      = Internal::readUint64( pEnvelopeBytes + 20 );
        header._keyID         = Internal::readUint64( pEnvelopeBytes + 28 );
        const bool bSizesSane = header._originalSize <= static_cast<uint64>( ILocalStore::kMaxSlotSize ) && header._bodySize <= envelopeByteCount;
        if ( bSizesSane == false )
            return LocalStoreResult::Corrupt;
        const size_t  bodySize = static_cast<size_t>( header._bodySize );
        const uint8*  pBody    = nullptr;
        vector<uint8> plainBody;
        switch ( header._seal )
        {
            case LocalStoreSeal::None:
            {
                if ( envelopeByteCount != kHeaderSize + bodySize + Internal::kCrcSize )
                    return LocalStoreResult::Corrupt;
                const uint32 storedCrc = Internal::readUint32( pEnvelopeBytes + kHeaderSize + bodySize );
                if ( StringUtil::computeCrc32( pEnvelopeBytes, kHeaderSize + bodySize ) != storedCrc )
                    return LocalStoreResult::Corrupt;
                pBody = pEnvelopeBytes + kHeaderSize;
                break;
            }
            case LocalStoreSeal::Authenticated:
            case LocalStoreSeal::Encrypted:
            {
                const size_t expectedSize = kHeaderSize + bodySize + NetSecurityConstant::kAeadNonceSize + NetSecurityConstant::kAeadTagSize;
                if ( envelopeByteCount != expectedSize )
                    return LocalStoreResult::Corrupt;
                uint8                  arrSealKey[NetSecurityConstant::kAeadKeySize] = {};
                uint64                 keyID                                         = 0;
                const LocalStoreResult keyResult                                     = Internal::deriveKeys( context, arrSealKey, keyID );
                if ( keyResult != LocalStoreResult::Ok )
                    return keyResult;
                if ( keyID != header._keyID )
                {
                    std::memset( arrSealKey, 0, sizeof( arrSealKey ) );
                    return LocalStoreResult::WrongKey;
                }
                unique_ptr<INetAead> aead = context._pSecurityProvider->createAead( NetAeadAlgorithm::Aes256Gcm, arrSealKey );
                std::memset( arrSealKey, 0, sizeof( arrSealKey ) );
                if ( aead == nullptr )
                    return LocalStoreResult::IoError;
                if ( header._seal == LocalStoreSeal::Authenticated )
                {
                    const uint8* pNonce      = pEnvelopeBytes + kHeaderSize + bodySize;
                    const uint8* pTag        = pNonce + NetSecurityConstant::kAeadNonceSize;
                    uint8        arrEmpty[1] = {};
                    if ( aead->open( pNonce, pEnvelopeBytes, static_cast<int32>( kHeaderSize + bodySize ), pTag, NetSecurityConstant::kAeadTagSize, arrEmpty ) == false )
                        return LocalStoreResult::Corrupt;
                    pBody = pEnvelopeBytes + kHeaderSize;
                    break;
                }
                const uint8* pNonce  = pEnvelopeBytes + kHeaderSize;
                const uint8* pCipher = pNonce + NetSecurityConstant::kAeadNonceSize;
                plainBody.resize( bodySize );
                if ( aead->open( pNonce, pEnvelopeBytes, static_cast<int32>( kHeaderSize ), pCipher, static_cast<int32>( bodySize + NetSecurityConstant::kAeadTagSize ),
                                 plainBody.data() ) == false )
                    return LocalStoreResult::Corrupt;
                pBody = plainBody.data();
                break;
            }
        }
        const LocalStoreResult result = Internal::decompressBody( context, pBody, bodySize, header, outBodyBytes );
        if ( result == LocalStoreResult::Ok )
            outFormatVersion = header._formatVersion;
        return result;
    }
} // namespace sw
