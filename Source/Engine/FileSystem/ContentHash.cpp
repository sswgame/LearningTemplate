#include "pch.h"

#include "Engine/FileSystem/ContentHash.h"

#include "Core/Container/StringUtil.h"
#include "Core/Hash/HashUtil.h"

#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    namespace
    {
        struct ContentHashInternal
        {
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
    uint64 ContentHashCache::computeFile( string_view absPath )
    {
        FileStamp  stamp{};
        const bool bStamped = FileUtil::getFileStamp( absPath, stamp );
        if ( bStamped )
        {
            ScopedLock<mutex> lock{ _mutex };
            const auto        iter = _mapEntry.find( string( absPath ) );
            if ( iter != _mapEntry.end() && iter->second._stamp == stamp )
                return iter->second._hash;
        }

        const uint64 hash = ContentHashInternal::readFileHash( absPath );
        // 읽는 사이에 파일이 바뀌었으면 옛 시각에 새 해시가 붙는다. 다음 조회는 시각이 달라 다시 읽으므로 틀린 값이 남지 않는다.
        if ( bStamped && hash != 0 )
        {
            ScopedLock<mutex> lock{ _mutex };
            _mapEntry[string( absPath )] = Entry{ stamp, hash };
        }
        return hash;
    }

    void ContentHashCache::clear()
    {
        ScopedLock<mutex> lock{ _mutex };
        _mapEntry = unordered_map<string, Entry>{};
    }
} // namespace sw
