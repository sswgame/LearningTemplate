#include "pch.h"

#include "Engine/Content/ContentHash.h"

#include "Core/File/ProcessRoots.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Serialization/ByteStream.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/WallClock.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    namespace
    {
        struct ContentHashInternal
        {
            /** @brief 1601-01-01 부터 1970-01-01 까지의 밀리초(`FileStamp::_writeTime` 의 기원과 유닉스 기원의 차). */
            static constexpr int64 kFileTimeEpochOffsetMs = 11644473600000LL;
            /**
             * @brief 바깥 도구가 이보다 최근에 쓴 파일의 해시는 캐시에 두지 않는다(밀리초). 쓰기 시각은 시계 틱(Windows 약 16 ms) 단위라 그 안에서 같은 크기로 다시 쓰면 도장이 같다.
             * @details 이 프로세스의 쓰기도 이 창을 거친다 — 쓰기 관찰자(`recordOwnWrite`)는 같은 도장의 옛 해시를 지울 뿐이고, 그 틱에 바깥 쓰기가 겹칠 수 있다(git racy-git 과 같은 자리).
             */
            static constexpr int64 kRacyWindowMs = 2000;
            /** @brief `FileStamp::_writeTime` 의 100 ns 눈금이 1 ms 에 드는 수입니다. */
            static constexpr uint64 kWriteTicksPerMs = 10000u;

            /** @brief 영속 항목 하나가 파일에서 차지하는 최소 바이트입니다(빈 경로 길이 4 + 크기 · 시각 · 해시 24). */
            static constexpr size_t kMinCacheEntryBytes = 28;

            /** @brief 파일을 조각으로 읽어 해시를 구하고 읽은 바이트를 @p inoutByteCount 에 더합니다(캐시 밖 본체). 못 읽으면 0 입니다. */
            static uint64 readFileHash( const AbsolutePath& absPath, atomic<uint64>& inoutByteCount )
            {
                uint64                 byteCount = 0;
                const expected<uint64> hash      = ContentHash::computeFileStream( absPath, &byteCount );
                inoutByteCount.fetch_add( byteCount );
                return hash.has_value() ? *hash : 0;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ContentHash::isStampStable( const FileStamp& stamp )
    {
        const int64 nowFileTimeMs = WallClock::nowUnixMilliseconds().count() + ContentHashInternal::kFileTimeEpochOffsetMs;
        const int64 writeTimeMs   = static_cast<int64>( stamp._writeTime / ContentHashInternal::kWriteTicksPerMs );
        return nowFileTimeMs - writeTimeMs > ContentHashInternal::kRacyWindowMs;
    }

    uint64 ContentHash::computeBytes( const uint8* pData, size_t size )
    {
        return appendBytes( kInitialState, pData, size );
    }

    uint64 ContentHash::appendBytes( uint64 state, const uint8* pData, size_t size )
    {
        return HashUtil::appendBytes64( state, pData, size );
    }

    expected<uint64> ContentHash::computeFileStream( const AbsolutePath& absPath, uint64* pOutByteCount )
    {
        uint64 fileSize = 0;
        if ( FileUtil::tryGetFileSize( absPath, fileSize ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "file cannot be sized for its content hash", absPath.str() } );
        uint64        state = kInitialState;
        vector<uint8> chunk;
        for ( uint64 offset = 0; offset < fileSize; offset += kStreamChunkBytes )
        {
            if ( FileUtil::readFile( absPath, chunk, offset, kStreamChunkBytes ) == false || chunk.empty() )
                return unexpected( Error{ ErrorKind::ExternalFailure, "file cannot be read for its content hash", absPath.str() } );
            state = appendBytes( state, chunk.data(), chunk.size() );
            if ( pOutByteCount != nullptr )
                *pOutByteCount += chunk.size();
        }
        return state;
    }

    uint64 ContentHash::computeFile( const AbsolutePath& absPath )
    {
        return engine::getSubsystem<ContentHashCache>().computeFile( absPath );
    }

    void ContentHash::clearCache()
    {
        engine::getSubsystem<ContentHashCache>().clear();
    }
} // namespace sw

namespace sw
{
    ContentHashCache::WriteObserver::WriteObserver( ContentHashCache& owner )
        : _owner{ owner }
    {
    }

    void ContentHashCache::WriteObserver::recordFileWritten( string_view filePath )
    {
        _owner.recordOwnWrite( FileUtil::toDiskPath( filePath ) );
    }

    ContentHashCache::ContentHashCache()
        : _mutex{}
        , _mapEntry{}
        , _writeSerial{ 0 }
        , _cachePath{}
        , _hashedByteCount{ 0 }
        , _writeObserver{ *this }
        , _writeObserverRegistration{}
    {
    }

    SubsystemInitResult ContentHashCache::initializeSubsystem()
    {
        // 관찰자는 하나다 — 이미 걸려 있으면 다른 서비스 범위가 살아 있는 것이다(같은 프로세스의 두 엔진). 자기 쓰기를 놓치는 캐시로 돌지 않고 멈춘다.
        _writeObserverRegistration = PlatformFileUtil::bindWriteObserver( &_writeObserver );
        if ( _writeObserverRegistration.isActive() == false )
        {
            SW_LOG_ERROR_ID( "ContentHashCache.WriteObserverTaken", "Another file write observer is registered - the content hash cache would miss this process's writes" );
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }

    void ContentHashCache::shutdownSubsystem()
    {
        _writeObserverRegistration.reset();
    }

    string ContentHashCache::makeKey( string_view path )
    {
        return FileUtil::normalizeSeparators( path );
    }

    uint64 ContentHashCache::computeFile( const AbsolutePath& absPath )
    {
        const string key = makeKey( absPath.str() );
        FileStamp    stamp{};
        const bool   bStamped   = FileUtil::getFileStamp( absPath, stamp );
        uint64       serialSeen = 0;
        bool         bHadEntry  = false;
        if ( bStamped )
        {
            ScopedLock<mutex> lock{ _mutex };
            const auto        iter = _mapEntry.find( key );
            bHadEntry              = iter != _mapEntry.end();
            if ( bHadEntry )
            {
                serialSeen = iter->second._writeSerial;
                if ( iter->second._stamp == stamp && iter->second._hash != 0 )
                    return iter->second._hash; // 자기 쓰기 표(해시 0)면 다시 읽는다
            }
        }

        const uint64 hash = ContentHashInternal::readFileHash( absPath, _hashedByteCount );
        // 자기 쓰기도 시간 창이 지나야 남긴다 — 쓴 그 틱에 바깥 도구가 같은 크기로 다시 쓰면 도장으로 알 길이 없다(두 프로세스의 순서 없는 경합).
        if ( bStamped == false || hash == 0 || ContentHash::isStampStable( stamp ) == false )
            return hash;
        ScopedLock<mutex> lock{ _mutex };
        // 읽는 사이에 자기 쓰기가 끼었으면(번호가 바뀌었거나 새 표가 생겼다) 읽은 내용이 어느 쓰기의 것인지 모른다 — 남기지 않는다. 바깥 쓰기가 끼었으면 다음 조회의 도장이 달라 다시 읽는다.
        const auto iter             = _mapEntry.find( key );
        const bool bOwnWriteBetween = iter != _mapEntry.end() ? ( bHadEntry == false || iter->second._writeSerial != serialSeen ) : bHadEntry;
        if ( bOwnWriteBetween == false )
            _mapEntry[key] = Entry{ stamp, hash, serialSeen };
        return hash;
    }

    void ContentHashCache::recordOwnWrite( const AbsolutePath& filePath )
    {
        const string      key = makeKey( filePath.str() );
        FileStamp         stamp{};
        const bool        bFile = FileUtil::getFileStamp( filePath, stamp );
        ScopedLock<mutex> lock{ _mutex };
        ++_writeSerial;
        if ( bFile )
            _mapEntry[key] = Entry{ stamp, 0, _writeSerial }; // 해시는 다음 조회가 읽어 채운다(쓰는 길에서 해시하지 않는다 — 수십 MB 팩도 이 길로 쓴다)
        else
            _mapEntry.erase( key ); // 지웠거나 폴더다
    }

    AbsolutePath ContentHashCache::makeLocalCachePath()
    {
        return ProcessRoots::get()._derivedData.join( "ContentHashCache.bin" );
    }

    expected<void> ContentHashCache::load( const AbsolutePath& cachePath )
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            _cachePath = cachePath;
        }
        vector<uint8> bytes;
        if ( FileUtil::isRegularFile( cachePath ) == false )
            return {};
        if ( FileUtil::readFile( cachePath, bytes ) == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "content hash cache cannot be read", cachePath.str() } );
        ByteReader   reader{ bytes.data(), bytes.size() };
        const uint32 magic   = reader.readUint32();
        const uint32 version = reader.readUint32();
        if ( reader.hasOverflowed() || magic != kCacheFormat._tag )
            return unexpected( Error{ ErrorKind::InvalidArgument, "content hash cache is not a cache file", cachePath.str() } );
        if ( version != kCacheFormat._version )
            return {}; // 다른 판 — 빈 캐시로 시작하고 다음 save 가 이 판으로 덮는다
        struct LoadedEntry
        {
            string _key;
            Entry  _entry;
        };
        vector<LoadedEntry> listLoaded;
        const auto          readEntry = [&reader]( LoadedEntry& outLoaded )
        {
            outLoaded._key                     = reader.readString();
            const bool bKey                    = reader.hasOverflowed() == false;
            outLoaded._entry._stamp._sizeBytes = reader.readUint64();
            outLoaded._entry._stamp._writeTime = reader.readUint64();
            outLoaded._entry._hash             = reader.readUint64();
            return bKey && outLoaded._entry._hash != 0;
        };
        const bool bRead = reader.readCountedList( listLoaded, ContentHashInternal::kMinCacheEntryBytes, readEntry );
        if ( bRead == false || reader.isFullyRead() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "content hash cache is truncated or malformed", cachePath.str() } );
        ScopedLock<mutex> lock{ _mutex };
        for ( LoadedEntry& loaded : listLoaded )
        {
            _mapEntry.emplace( std::move( loaded._key ), loaded._entry ); // 이 프로세스가 이미 구한 항목이 이긴다
        }
        return {};
    }

    expected<void> ContentHashCache::save() const
    {
        vector<uint8> bytes;
        AbsolutePath  cachePath;
        {
            ScopedLock<mutex> lock{ _mutex };
            if ( _cachePath.empty() )
                return unexpected( Error{ ErrorKind::InvalidArgument, "content hash cache has no path - call load first" } );
            cachePath = _cachePath;
            ByteWriter writer{ bytes };
            kCacheFormat.write( bytes );
            const size_t countOffset = writer.getByteCount();
            writer.writeUint32( 0 );
            uint32 count = 0;
            for ( const auto& [key, entry] : _mapEntry )
            {
                // 해시가 아직 없는 자기 쓰기 표와, 저장 시각의 시간 창 안에 고쳐진 도장은 싣지 않는다 — 다음 프로세스는 도장만 믿는다.
                if ( entry._hash == 0 || ContentHash::isStampStable( entry._stamp ) == false )
                    continue;
                writer.writeString( key );
                writer.writeUint64( entry._stamp._sizeBytes );
                writer.writeUint64( entry._stamp._writeTime );
                writer.writeUint64( entry._hash );
                ++count;
            }
            writer.writePodAt( countOffset, count );
        }
        if ( FileUtil::ensureParentDirectoryExists( cachePath ) == false || FileUtil::writeFile( cachePath, bytes.data(), bytes.size() ) == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "content hash cache cannot be written", cachePath.str() } );
        return {};
    }

    void ContentHashCache::clear()
    {
        ScopedLock<mutex> lock{ _mutex };
        _mapEntry = unordered_map<string, Entry>{};
    }
} // namespace sw

namespace sw
{
    // 파일 내용 해시 캐시 — 셰이더 컴파일 · 쿠킹이 워커에서 묻는다.
    SW_ENGINE_SERVICE_SUBSYSTEM( ContentHashCache, HostOnly, Process, Services, (), SW_ENGINE_DEFAULT_FACTORY( ContentHashCache ) );
} // namespace sw
