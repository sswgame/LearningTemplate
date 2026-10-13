#include "pch.h"

#include "Core/String/TagID.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/unordered_map.h"
#include "Core/Memory/LifetimeScope.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    namespace
    {
        struct TagRegistryInternal
        {
            static inline std::shared_mutex s_mutex;
            /**
             * @brief 태그 id → 이름 풀 글자입니다. 엔진 수명 범위가 소유합니다 — 글자를 든 이름 풀(`HashedStringPool::shutdown`)보다 먼저 닫히고,
             *        기동 뒤에 새 태그(`tag.add` 등)가 키운 표가 종료 누수 보고 전에 돌아갑니다.
             */
            static inline ScopeOwned<unordered_map<uint64, const utf8*>> s_mapIDToStr;

            static void registerTag( uint64 tagID, const utf8* pStr )
            {
                if ( tagID == 0 || pStr == nullptr )
                    return;
                unordered_map<uint64, const utf8*>* const pMap = s_mapIDToStr.getOrCreate();
                if ( pMap == nullptr )
                    return;
                std::unique_lock<std::shared_mutex> lock{ s_mutex };
                ( *pMap )[tagID] = pStr;
            }

            static const utf8* findString( uint64 tagID )
            {
                const unordered_map<uint64, const utf8*>* const pMap = s_mapIDToStr.find();
                if ( tagID == 0 || pMap == nullptr )
                    return nullptr;
                std::shared_lock<std::shared_mutex> lock{ s_mutex };
                const auto                          iter = pMap->find( tagID );
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
        // intern 이 대소문자를 무시하므로 `Player` 와 `player` 는 같은 문자열을 돌려받는다. ID 도 같은 규칙이라(`computeID`)
        // 둘은 같은 태그가 된다.
        const hashed_string hashedName{ str };
        const uint64        tagID = computeID( str.data(), str.size() );

        TagRegistryInternal::registerTag( tagID, hashedName.c_str() );
        return TagID{ tagID, hashedName.c_str() };
    }
} // namespace sw
