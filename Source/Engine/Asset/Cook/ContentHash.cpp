#include "pch.h"

#include "Engine/Asset/Cook/ContentHash.h"

#include "Core/Common/HashUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/LifetimeScope.h"

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
            using EntryMap = unordered_map<string, Entry>;

            /**
             * @brief 경로 → 해시 캐시입니다. 엔진 수명 범위가 소유합니다 — 기동 뒤 첫 셰이더 반사 · 쿠킹이 채운 항목이 종료 누수 보고 전에 돌아갑니다
             *        (정적 맵이면 프로세스 끝까지 살아 보고에 "Shader 가 늘었다" 로 남았다).
             */
            inline static ScopeOwned<EntryMap> _s_mapEntry{};
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
        // CR 을 건너뛰며 바로 섞는다 — CR 을 뺀 사본을 만들지 않는다(출력 해시는 팩 하나가 수십 MB 이고 항목마다 부른다).
        // 값은 사본을 `computeStringHash64( ..., bIgnoreCase = false )` 로 섞던 것과 같다: 같은 시작값에 CR 아닌 바이트를 차례로 `fnvStep64` 한다.
        uint64 hash = HashUtil::kFnvOffset64;
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
        FileStamp                            stamp{};
        const bool                           bStamped  = FileUtil::getFileStamp( absPath, stamp );
        ContentHashInternal::EntryMap* const pMapEntry = bStamped ? ContentHashInternal::_s_mapEntry.getOrCreate() : nullptr;
        if ( pMapEntry != nullptr )
        {
            ScopedLock<mutex> lock{ ContentHashInternal::_s_mutex };
            const auto        iter = pMapEntry->find( string( absPath ) );
            if ( iter != pMapEntry->end() && iter->second._stamp == stamp )
                return iter->second._hash;
        }

        const uint64 hash = ContentHashInternal::readFileHash( absPath );
        // 읽는 사이에 파일이 바뀌었으면 옛 시각에 새 해시가 붙는다. 다음 조회는 시각이 달라 다시 읽으므로 틀린 값이 남지 않는다.
        if ( pMapEntry != nullptr && hash != 0 )
        {
            ScopedLock<mutex> lock{ ContentHashInternal::_s_mutex };
            ( *pMapEntry )[string( absPath )] = ContentHashInternal::Entry{ stamp, hash };
        }
        return hash;
    }

    void ContentHash::clearCache()
    {
        ContentHashInternal::EntryMap* const pMapEntry = ContentHashInternal::_s_mapEntry.find();
        if ( pMapEntry == nullptr )
            return;
        ScopedLock<mutex> lock{ ContentHashInternal::_s_mutex };
        pMapEntry->clear();
    }
} // namespace sw
