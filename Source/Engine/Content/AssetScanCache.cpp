#include "pch.h"

#include "Engine/Content/AssetScanCache.h"

#include "Core/File/ProcessRoots.h"
#include "Core/Serialization/ByteStream.h"

#include "Engine/Content/ContentHash.h"
#include "Engine/Serialization/Format/FormatDef.h"

namespace sw
{
    namespace
    {
        struct AssetScanCacheInternal
        {
            /** @brief 파일 틀입니다(줄의 배치). 뽑은 바이트의 판은 따로 든다. */
            static constexpr FormatDef kFormat{ "SWAS" };
            /** @brief 머리(표지 · 틀의 판 · 뽑은 바이트의 판 · 몸통 해시)의 바이트 수입니다. */
            static constexpr size_t kHeaderBytes = 20;
            /** @brief 줄 하나가 차지하는 최소 바이트입니다(빈 열쇠 길이 4 + 도장 16 + 빈 바이트 길이 4). */
            static constexpr size_t kMinEntryBytes = 24;
        };
    } // namespace

    AssetScanCache::AssetScanCache( uint32 payloadVersion )
        : _mapEntry{}
        , _payloadVersion{ payloadVersion }
        , _usedCount{ 0 }
        , _bChanged{ SW_FALSE }
    {
    }

    AbsolutePath AssetScanCache::makeLocalPath( string_view fileName )
    {
        return ProcessRoots::get()._derivedData.join( fileName );
    }

    expected<void> AssetScanCache::load( const AbsolutePath& cachePath )
    {
        using Internal = AssetScanCacheInternal;
        _mapEntry.clear();
        _usedCount = 0;
        _bChanged  = SW_FALSE;
        if ( FileUtil::isRegularFile( cachePath ) == false )
            return {};
        vector<uint8> bytes;
        if ( FileUtil::readFile( cachePath, bytes ) == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "asset scan cache cannot be read", cachePath.str() } );
        ByteReader   reader{ bytes.data(), bytes.size() };
        const uint32 magic          = reader.readUint32();
        const uint32 formatVersion  = reader.readUint32();
        const uint32 payloadVersion = reader.readUint32();
        const uint64 bodyHash       = reader.readUint64();
        if ( reader.hasOverflowed() || magic != Internal::kFormat._tag )
            return unexpected( Error{ ErrorKind::InvalidArgument, "asset scan cache is not a cache file", cachePath.str() } );
        if ( formatVersion != Internal::kFormat._version || payloadVersion != _payloadVersion )
            return {}; // 다른 판 — 빈 캐시로 시작하고 다음 save 가 이 판으로 덮는다
        // 정체성이 틀린 줄로 풀리면 참조가 조용히 다른 에셋을 가리킨다 — 깨진 바이트는 통째로 버린다.
        if ( ContentHash::computeBytes( bytes.data() + Internal::kHeaderBytes, bytes.size() - Internal::kHeaderBytes ) != bodyHash )
            return unexpected( Error{ ErrorKind::InvalidArgument, "asset scan cache is corrupt", cachePath.str() } );
        struct LoadedEntry
        {
            string _key;
            Entry  _entry;
        };
        vector<LoadedEntry> listLoaded;
        const bool          bRead = reader.readCountedList( listLoaded, Internal::kMinEntryBytes, [&reader]( LoadedEntry& outLoaded )
                 {
            outLoaded._key                     = reader.readString();
            outLoaded._entry._stamp._sizeBytes = reader.readUint64();
            outLoaded._entry._stamp._writeTime = reader.readUint64();
            outLoaded._entry._payload          = reader.readByteVector();
        } );
        if ( bRead == false || reader.isFullyRead() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "asset scan cache is truncated or malformed", cachePath.str() } );
        _mapEntry.reserve( listLoaded.size() );
        for ( LoadedEntry& loaded : listLoaded )
        {
            _mapEntry.emplace( std::move( loaded._key ), std::move( loaded._entry ) );
        }
        return {};
    }

    expected<void> AssetScanCache::save( const AbsolutePath& cachePath ) const
    {
        using Internal = AssetScanCacheInternal;
        if ( _bChanged == SW_FALSE && _usedCount == _mapEntry.size() )
            return {}; // 실은 그대로다
        vector<uint8> bytes;
        ByteWriter    writer{ bytes };
        Internal::kFormat.write( bytes );
        writer.writeUint32( _payloadVersion );
        const size_t hashOffset = writer.getByteCount();
        writer.writeUint64( 0 );
        writer.writeUint32( _usedCount );
        for ( const auto& [key, entry] : _mapEntry )
        {
            if ( entry._bUsed == false )
                continue; // 사라진 파일
            writer.writeString( key );
            writer.writeUint64( entry._stamp._sizeBytes );
            writer.writeUint64( entry._stamp._writeTime );
            writer.writeByteVector( entry._payload );
        }
        writer.writePodAt( hashOffset, ContentHash::computeBytes( bytes.data() + Internal::kHeaderBytes, bytes.size() - Internal::kHeaderBytes ) );
        // 함께 뜬 프로세스(백엔드별 자식)가 같은 자리를 다투는 것이 정상이라 로그 없는 쓰기다 — 진 것은 부르는 쪽이 안다.
        if ( FileUtil::ensureParentDirectoryExists( cachePath ) == false || FileUtil::tryWriteFile( cachePath, bytes.data(), bytes.size() ) != FileWriteResult::Written )
            return unexpected( Error{ ErrorKind::ExternalFailure, "asset scan cache cannot be written", cachePath.str() } );
        return {};
    }

    const vector<uint8>* AssetScanCache::find( const string& key, const FileStamp& stamp )
    {
        const auto found = _mapEntry.find( key );
        if ( found == _mapEntry.end() || found->second._stamp != stamp )
            return nullptr;
        if ( found->second._bUsed == false )
        {
            found->second._bUsed = true;
            ++_usedCount;
        }
        return &found->second._payload;
    }

    void AssetScanCache::record( const string& key, const FileStamp& stamp, vector<uint8> payload )
    {
        if ( ContentHash::isStampStable( stamp ) == false )
            return;
        Entry& entry = _mapEntry[key];
        if ( entry._bUsed == false )
            ++_usedCount;
        entry     = Entry{ stamp, std::move( payload ), true };
        _bChanged = SW_TRUE;
    }
} // namespace sw
