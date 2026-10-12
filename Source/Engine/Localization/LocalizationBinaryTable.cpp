#include "pch.h"

#include "Engine/Localization/LocalizationBinaryTable.h"

#include "Core/Container/LittleEndianUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/String/hashed_string.h"

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
                return StringUtil::computeHash64( reinterpret_cast<const utf8*>( pData ), size, false );
            }

            static bool fail( string* pOutError, string_view reason )
            {
                if ( pOutError != nullptr )
                    *pOutError = string( reason );
                return false;
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

    void LocalizationBinaryTable::setText( string_view key, string_view text )
    {
        setTextByHash( hashed_string::computeHash( key ), text );
    }

    void LocalizationBinaryTable::setTextByHash( uint64 keyHash, string_view text )
    {
        _mapText[keyHash] = string( text );
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

    bool LocalizationBinaryTable::parse( const uint8* pData, size_t size, string* pOutError )
    {
        using Internal = LocalizationBinaryTableInternal;
        _culture.clear();
        _mapText.clear();
        if ( pData == nullptr || size < Internal::kHeaderFixedBytes + Internal::kCountBytes + Internal::kChecksumBytes )
            return Internal::fail( pOutError, "locres is too small" );
        if ( LittleEndianUtil::readUint32( pData ) != kMagic )
            return Internal::fail( pOutError, "locres magic mismatch" );
        if ( LittleEndianUtil::readUint32( pData + 4 ) != kVersion )
            return Internal::fail( pOutError, "locres version is not supported" );

        const size_t checksumPos = size - Internal::kChecksumBytes;
        if ( LittleEndianUtil::readUint64( pData + checksumPos ) != Internal::computeChecksum( pData, checksumPos ) )
            return Internal::fail( pOutError, "locres checksum mismatch" );

        const uint32 cultureLength = LittleEndianUtil::readUint32( pData + 8 );
        size_t       position      = Internal::kHeaderFixedBytes;
        if ( cultureLength > checksumPos - position )
            return Internal::fail( pOutError, "locres culture is out of range" );
        string culture( reinterpret_cast<const utf8*>( pData + position ), cultureLength );
        position += cultureLength;

        if ( Internal::kCountBytes > checksumPos - position )
            return Internal::fail( pOutError, "locres header is truncated" );
        const uint32 entryCount = LittleEndianUtil::readUint32( pData + position );
        const uint32 blobBytes  = LittleEndianUtil::readUint32( pData + position + 4 );
        position += Internal::kCountBytes;

        const uint64 tableBytes = static_cast<uint64>( entryCount ) * Internal::kEntryBytes;
        if ( tableBytes + blobBytes != checksumPos - position )
            return Internal::fail( pOutError, "locres sizes do not match the file" );
        const uint8* pTable = pData + position;
        const uint8* pBlob  = pTable + tableBytes;

        map<uint64, string> mapText;
        for ( uint32 entryIndex = 0; entryIndex < entryCount; ++entryIndex )
        {
            const uint8* pEntry = pTable + static_cast<size_t>( entryIndex ) * Internal::kEntryBytes;
            const uint64 hash   = LittleEndianUtil::readUint64( pEntry );
            const uint32 offset = LittleEndianUtil::readUint32( pEntry + 8 );
            const uint32 length = LittleEndianUtil::readUint32( pEntry + 12 );
            if ( static_cast<uint64>( offset ) + length > blobBytes )
                return Internal::fail( pOutError, "locres text is out of range" );
            mapText[hash] = string( reinterpret_cast<const utf8*>( pBlob + offset ), length );
        }
        _culture = std::move( culture );
        _mapText = std::move( mapText );
        return true;
    }
} // namespace sw
