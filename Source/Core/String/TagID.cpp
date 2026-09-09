#include "pch.h"

#include "Core/String/TagID.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/unordered_map.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    namespace
    {
        struct TagRegistryInternal
        {
            static inline std::shared_mutex                  s_mutex;
            static inline unordered_map<uint64, const utf8*> s_mapIdToStr;

            static void registerTag( uint64 tagId, const utf8* pStr )
            {
                if ( tagId == 0 || pStr == nullptr )
                    return;
                std::unique_lock<std::shared_mutex> lock{ s_mutex };
                s_mapIdToStr[tagId] = pStr;
            }

            static const utf8* findString( uint64 tagId )
            {
                if ( tagId == 0 )
                    return nullptr;
                std::shared_lock<std::shared_mutex> lock{ s_mutex };
                const auto                          iter = s_mapIdToStr.find( tagId );
                return iter != s_mapIdToStr.end() ? iter->second : nullptr;
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
        hashed_string hashedName{ str };

        uint64 hashValue = StringUtil::kOffset64;
        for ( size_t charIndex = 0; charIndex < str.length(); ++charIndex )
        {
            hashValue = ( hashValue ^ static_cast<uint64>( str[charIndex] ) ) * StringUtil::kPrime64;
        }

        TagRegistryInternal::registerTag( hashValue, hashedName.c_str() );
        return TagID{ hashValue, hashedName.c_str() };
    }
} // namespace sw
