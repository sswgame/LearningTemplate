#include "pch.h"

#include "Engine/Localization/LocalizationBinaryTable.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Serialization/ByteStream.h"
#include "Core/Serialization/LittleEndianUtil.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        struct LocalizationBinaryTableInternal
        {
            static constexpr size_t kHeaderFixedBytes = 12; ///< magic, version, cultureLength
            static constexpr size_t kCountBytes       = 8;  ///< entryCount, blobBytes
            static constexpr size_t kEntryBytes       = 16; ///< keyHash, offset, length
            static constexpr size_t kChecksumBytes    = 8;

            static uint64 computeChecksum( const uint8* pData, size_t size )
            {
                return HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( pData ), size, false );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void LocalizationBinaryTable::setCulture( string_view culture )
    {
        _culture = string( culture );
    }

    expected<void> LocalizationBinaryTable::setText( string_view key, string_view text )
    {
        const expected<uint64> keyHash = _keyIndex.add( key );
        if ( keyHash.has_value() == false )
            return unexpected( keyHash.error() );
        _mapText[*keyHash] = string( text );
        return {};
    }

    void LocalizationBinaryTable::toBytes( vector<uint8>& outBytes ) const
    {
        outBytes.clear();
        LittleEndianUtil::appendUint32( outBytes, kMagic );
        LittleEndianUtil::appendUint32( outBytes, kVersion );
        LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( _culture.size() ) );
        outBytes.insert( outBytes.end(), _culture.begin(), _culture.end() );
        uint32 blobBytes = 0;
        for ( const auto& [hash, text] : _mapText )
        {
            blobBytes += static_cast<uint32>( text.size() );
        }
        LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( _mapText.size() ) );
        LittleEndianUtil::appendUint32( outBytes, blobBytes );
        uint32 offset = 0;
        for ( const auto& [hash, text] : _mapText )
        {
            LittleEndianUtil::appendUint64( outBytes, hash );
            LittleEndianUtil::appendUint32( outBytes, offset );
            LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( text.size() ) );
            offset += static_cast<uint32>( text.size() );
        }
        for ( const auto& [hash, text] : _mapText )
        {
            outBytes.insert( outBytes.end(), text.begin(), text.end() );
        }
        const uint64 checksum = LocalizationBinaryTableInternal::computeChecksum( outBytes.data(), outBytes.size() );
        LittleEndianUtil::appendUint64( outBytes, checksum );
    }

    expected<void> LocalizationBinaryTable::parse( const uint8* pData, size_t size )
    {
        using Internal = LocalizationBinaryTableInternal;
        _culture.clear();
        _mapText.clear();
        _keyIndex.clear();
        if ( pData == nullptr || size < Internal::kHeaderFixedBytes + Internal::kCountBytes + Internal::kChecksumBytes )
            return unexpected( Error{ ErrorKind::InvalidArgument, "locres is too small" } );
        // 끝의 체크섬을 뺀 앞부분만 읽는다 — 표가 체크섬 자리를 읽지 못한다.
        const size_t checksumPos = size - Internal::kChecksumBytes;
        ByteReader   reader{ pData, checksumPos };
        ByteReader   checksumReader{ pData + checksumPos, Internal::kChecksumBytes };
        if ( reader.readUint32() != kMagic )
            return unexpected( Error{ ErrorKind::InvalidArgument, "locres magic mismatch" } );
        if ( reader.readUint32() != kVersion )
            return unexpected( Error{ ErrorKind::InvalidArgument, "locres version is not supported" } );

        if ( checksumReader.readUint64() != Internal::computeChecksum( pData, checksumPos ) )
            return unexpected( Error{ ErrorKind::InvalidArgument, "locres checksum mismatch" } );

        string_view cultureView;
        if ( reader.readStringView( cultureView ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "locres culture is out of range" } );
        string culture( cultureView );

        const uint32 entryCount = reader.readUint32();
        const uint32 blobBytes  = reader.readUint32();
        if ( reader.hasOverflowed() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "locres header is truncated" } );

        const uint64 tableBytes = static_cast<uint64>( entryCount ) * Internal::kEntryBytes;
        if ( tableBytes + blobBytes != reader.getRemainingByteCount() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "locres sizes do not match the file" } );
        ByteReader   table{ pData + reader.getOffset(), static_cast<size_t>( tableBytes ) };
        const uint8* pBlob = pData + reader.getOffset() + tableBytes;

        map<uint64, string> mapText;
        for ( uint32 entryIndex = 0; entryIndex < entryCount; ++entryIndex )
        {
            const uint64 hash   = table.readUint64();
            const uint32 offset = table.readUint32();
            const uint32 length = table.readUint32();
            if ( static_cast<uint64>( offset ) + length > blobBytes )
                return unexpected( Error{ ErrorKind::InvalidArgument, "locres text is out of range" } );
            mapText[hash] = string( reinterpret_cast<const utf8*>( pBlob + offset ), length );
        }
        _culture = std::move( culture );
        _mapText = std::move( mapText );
        return {};
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( LocalizationBinaryTable, LocalizationBinaryTable::kAssetType );
} // namespace sw
