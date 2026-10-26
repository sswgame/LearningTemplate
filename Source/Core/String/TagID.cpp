#include "pch.h"

#include "Core/String/TagID.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Memory/LifetimeScope.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    namespace
    {
        struct TagRegistryInternal
        {
            static inline shared_mutex s_mutex;
            /**
             * @brief 태그 id → 이름 풀 글자입니다. 엔진 수명 범위가 소유합니다 — 글자를 든 이름 풀(`HashedStringPool::shutdown`)보다 먼저 닫히고,
             *        기동 뒤에 새 태그(`tag.add` 등)가 키운 표가 종료 누수 보고 전에 돌아갑니다.
             */
            static inline ScopeOwned<unordered_map<uint64, const utf8*>> s_mapIDToStr;

            static void registerTag( uint64 tagID, const hashed_string& name )
            {
                const utf8* const pStr = name.c_str();
                if ( tagID == 0 || pStr == nullptr )
                    return;
                unordered_map<uint64, const utf8*>* const pMap = s_mapIDToStr.getOrCreate();
                if ( pMap == nullptr )
                    return;
                std::unique_lock<shared_mutex> lock{ s_mutex };
                const auto [iter, bInserted] = pMap->try_emplace( tagID, pStr );
                // 이름 풀은 철자마다 따로 글자를 두므로(`Player` · `player` 는 같은 이름, 다른 포인터) 포인터가 아니라 이름 같음(대소문자 무시)으로 본다.
                // 같은 id 에 다른 이름이 오면 64 비트 해시가 부딪힌 것이다 — 덮어쓰면 한 태그가 다른 태그의 이름으로 조용히 바뀌므로 처음 등록을 지키고 개발 빌드에서 멈춘다.
                SW_ASSERT( bInserted || hashed_string{ string_view{ iter->second } } == name );
                (void)iter;
                (void)bInserted;
            }

            static const utf8* findString( uint64 tagID )
            {
                const unordered_map<uint64, const utf8*>* const pMap = s_mapIDToStr.find();
                if ( tagID == 0 || pMap == nullptr )
                    return nullptr;
                std::shared_lock<shared_mutex> lock{ s_mutex };
                const auto                     iter = pMap->find( tagID );
                return iter != pMap->end() ? iter->second : nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* TagID::getString() const
    {
        if ( _pString != nullptr )
            return _pString;
        return TagRegistryInternal::findString( _id );
    }

    TagID TagID::request( string_view str )
    {
        // intern 의 같음이 대소문자를 무시하므로 `Player` 와 `player` 는 같은 이름이다(글자는 철자마다 따로). ID 도 같은 규칙이라(`computeID`)
        // 둘은 같은 태그가 된다.
        const hashed_string hashedName{ str };
        const uint64        tagID = computeID( str.data(), str.size() );

        TagRegistryInternal::registerTag( tagID, hashedName );
        return TagID{ tagID, hashedName.c_str() };
    }
} // namespace sw
