#include "pch.h"

#include "Engine/Content/ContentHash.h"

#include "Core/Hash/HashUtil.h"
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
             * @details 이 프로세스의 쓰기는 이 창을 거치지 않는다 — 쓰기 관찰자가 그 자리에서 알린다(`recordOwnWrite`). 바깥 쓰기는 알릴 길이 없어 시간 창만 남는다(git racy-git 과 같은 자리).
             */
            static constexpr int64 kRacyWindowMs = 2000;
            /** @brief `FileStamp::_writeTime` 의 100 ns 눈금이 1 ms 에 드는 수입니다. */
            static constexpr uint64 kWriteTicksPerMs = 10000u;

            /**
             * @brief @p stamp 가 바깥 쓰기에 대해 캐시해도 되는 도장인지 — 쓴 지 `kRacyWindowMs` 가 지났는지입니다.
             * @details git 의 racy-git 과 같은 결함이다: 같은 틱 안에 같은 크기로 다시 쓴 파일은 (크기, 시각) 도장이 같아 옛 해시가 돌아왔다
             *          (`ShaderCookStampTest` 가 전체 실행에서만 졌다). 바깥에서 막 쓴 파일은 매번 읽고, 자리 잡은 파일만 캐시한다.
             */
            static bool isCompleted( const FileStamp& stamp )
            {
                const int64 nowFileTimeMs = WallClock::nowUnixMilliseconds().count() + kFileTimeEpochOffsetMs;
                const int64 writeTimeMs   = static_cast<int64>( stamp._writeTime / kWriteTicksPerMs );
                return nowFileTimeMs - writeTimeMs > kRacyWindowMs;
            }

            /** @brief 파일을 읽어 해시를 구합니다(캐시 밖 본체). */
            static uint64 readFileHash( string_view absPath )
            {
                vector<uint8> bytes;
                if ( FileUtil::readFile( absPath, bytes ) == false )
                    return 0;
                return ContentHash::computeBytes( bytes );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint64 ContentHash::computeBytes( const uint8* pData, size_t size )
    {
        // CR 을 건너뛰며 바로 섞는다 — CR 을 뺀 사본을 만들지 않는다(출력 해시는 팩 하나가 수십 MB 이고 항목마다 부른다).
        // 값은 사본을 `computeStringHash64( ..., bIgnoreCase = false )` 로 섞던 것과 같다: 같은 시작값에 CR 아닌 바이트를 차례로 `fnvStep64` 한다.
        uint64 hash = HashUtil::kFNVOffset64;
        for ( size_t index = 0; index < size; ++index )
        {
            const uint8 byte = pData[index];
            if ( byte != static_cast<uint8>( '\r' ) )
                hash = HashUtil::fnvStep64( hash, byte );
        }
        return hash;
    }

    uint64 ContentHash::computeFile( string_view absPath )
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
        _owner.recordOwnWrite( filePath );
    }

    ContentHashCache::ContentHashCache()
        : _mutex{}
        , _mapEntry{}
        , _writeSerial{ 0 }
        , _writeObserver{ *this }
        , _bObserving{ SW_FALSE }
    {
    }

    SubsystemInitResult ContentHashCache::initializeSubsystem()
    {
        // 관찰자는 하나다 — 이미 걸려 있으면 다른 서비스 범위가 살아 있는 것이다(같은 프로세스의 두 엔진). 자기 쓰기를 놓치는 캐시로 돌지 않고 멈춘다.
        if ( PlatformFileUtil::registerWriteObserver( &_writeObserver ) == false )
        {
            SW_LOG_ERROR_ID( "ContentHashCache.WriteObserverTaken", "Another file write observer is registered - the content hash cache would miss this process's writes" );
            return SubsystemInitResult::Failed;
        }
        _bObserving = SW_TRUE;
        return SubsystemInitResult::Succeeded;
    }

    void ContentHashCache::shutdownSubsystem()
    {
        if ( _bObserving == SW_TRUE )
            PlatformFileUtil::unregisterWriteObserver( &_writeObserver );
        _bObserving = SW_FALSE;
    }

    string ContentHashCache::makeKey( string_view path )
    {
        return FileUtil::normalizeSeparators( path );
    }

    uint64 ContentHashCache::computeFile( string_view absPath )
    {
        const string key = makeKey( absPath );
        FileStamp    stamp{};
        const bool   bStamped   = FileUtil::getFileStamp( absPath, stamp );
        bool         bOwnWrite  = false;
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
                if ( iter->second._stamp == stamp )
                {
                    if ( iter->second._hash != 0 )
                        return iter->second._hash;
                    bOwnWrite = true; // 이 도장의 내용은 이 프로세스가 썼다 — 읽은 해시를 시간 창 없이 캐시한다
                }
            }
        }

        const uint64 hash = ContentHashInternal::readFileHash( absPath );
        if ( bStamped == false || hash == 0 || ( bOwnWrite == false && ContentHashInternal::isCompleted( stamp ) == false ) )
            return hash;
        ScopedLock<mutex> lock{ _mutex };
        // 읽는 사이에 자기 쓰기가 끼었으면(번호가 바뀌었거나 새 표가 생겼다) 읽은 내용이 어느 쓰기의 것인지 모른다 — 남기지 않는다. 바깥 쓰기가 끼었으면 다음 조회의 도장이 달라 다시 읽는다.
        const auto iter             = _mapEntry.find( key );
        const bool bOwnWriteBetween = iter != _mapEntry.end() ? ( bHadEntry == false || iter->second._writeSerial != serialSeen ) : bHadEntry;
        if ( bOwnWriteBetween == false )
            _mapEntry[key] = Entry{ stamp, hash, serialSeen };
        return hash;
    }

    void ContentHashCache::recordOwnWrite( string_view filePath )
    {
        const string      key = makeKey( filePath );
        FileStamp         stamp{};
        const bool        bFile = FileUtil::getFileStamp( filePath, stamp );
        ScopedLock<mutex> lock{ _mutex };
        ++_writeSerial;
        if ( bFile )
            _mapEntry[key] = Entry{ stamp, 0, _writeSerial }; // 해시는 다음 조회가 읽어 채운다(쓰는 길에서 해시하지 않는다 — 수십 MB 팩도 이 길로 쓴다)
        else
            _mapEntry.erase( key ); // 지웠거나 폴더다
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
    SW_ENGINE_SERVICE_SUBSYSTEM( ContentHashCache, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( ContentHashCache ) );
} // namespace sw
