#include "pch.h"

#include "Engine/Resource/Cook/ContentHash.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"

namespace sw
{
    namespace
    {
        struct ContentHashInternal
        {
            /** @brief 내용 해시 캐시 항목 하나입니다. 그 해시를 구했을 때의 파일 크기와 쓰기 시각을 함께 둡니다. */
            struct Entry
            {
                FileStamp _stamp{};
                uint64    _hash{ 0 };
            };

            inline static unordered_map<string, Entry> _s_mapEntry{};
            /// @brief `_s_mapEntry` 의 락입니다. 셰이더 컴파일 · 쿠킹은 워커에서도 돕니다.
            inline static mutex _s_mutex{};

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
        vector<uint8> normalizedByte;
        normalizedByte.reserve( size );
        for ( size_t index = 0; index < size; ++index )
        {
            if ( pData[index] != static_cast<uint8>( '\r' ) )
                normalizedByte.push_back( pData[index] );
        }
        return StringUtil::computeHash64( reinterpret_cast<const utf8*>( normalizedByte.data() ), normalizedByte.size(), false );
    }

    uint64 ContentHash::computeFile( string_view absPath )
    {
        FileStamp  stamp{};
        const bool bStamped = FileUtil::getFileStamp( absPath, stamp );
        if ( bStamped )
        {
            std::scoped_lock<mutex> lock{ ContentHashInternal::_s_mutex };
            const auto              iter = ContentHashInternal::_s_mapEntry.find( string( absPath ) );
            if ( iter != ContentHashInternal::_s_mapEntry.end() && iter->second._stamp == stamp )
                return iter->second._hash;
        }

        const uint64 hash = ContentHashInternal::readFileHash( absPath );
        // 읽는 사이에 파일이 바뀌었으면 옛 시각에 새 해시가 붙는다. 다음 조회는 시각이 달라 다시 읽으므로 틀린 값이 남지 않는다.
        if ( bStamped && hash != 0 )
        {
            std::scoped_lock<mutex> lock{ ContentHashInternal::_s_mutex };
            ContentHashInternal::_s_mapEntry[string( absPath )] = ContentHashInternal::Entry{ stamp, hash };
        }
        return hash;
    }

    void ContentHash::clearCache()
    {
        std::scoped_lock<mutex> lock{ ContentHashInternal::_s_mutex };
        ContentHashInternal::_s_mapEntry.clear();
    }
} // namespace sw
