#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Concurrency/atomic.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialUtil.h"
#include "Engine/Reflection/EnumName.h"

namespace sw
{
    namespace
    {
        /// @brief MaterialUtil::getPermutationGeneration 참고. 정지한 씬이 퍼뮤테이션 변경을 알아채는 유일한 신호입니다.
        atomic<uint64> s_permutationGeneration{ 1 };
    } // namespace
} // namespace sw

namespace sw
{
    uint64 MaterialUtil::getPermutationGeneration()
    {
        return s_permutationGeneration.load( std::memory_order_relaxed );
    }

    void MaterialUtil::bumpPermutationGeneration()
    {
        s_permutationGeneration.fetch_add( 1, std::memory_order_relaxed );
    }

    void MaterialUtil::appendUniqueDefine( vector<string>& outListDefine, string_view define )
    {
        if ( define.empty() )
            return;
        for ( const string& existing : outListDefine )
        {
            if ( existing == define )
                return;
        }
        outListDefine.push_back( string( define ) );
    }

    void MaterialUtil::appendUsageDefines( MaterialUsageFlags usage, vector<string>& outListDefine )
    {
        // define 이름은 열거자 이름에서 만든다(`PostProcess` → `MATERIAL_USAGE_POST_PROCESS`) — 셰이더 쪽 철자의 정본은 MaterialUsageFlags 하나다.
        for ( uint32 bit = 0; bit < sizeof( MaterialUsageFlags ) * 8; ++bit )
        {
            const MaterialUsageFlags flag = static_cast<MaterialUsageFlags>( 1u << bit );
            if ( EnumUtil::hasFlag( usage, flag ) == false )
                continue;
            const utf8* pName = getEnumName( flag );
            SW_ASSERT( StringUtil::equals( pName, "Unknown" ) == false ); // 비트마다 ENUM 이름이 있어야 define 이 생긴다
            MaterialUtil::appendUniqueDefine( outListDefine, string( "MATERIAL_USAGE_" ) + StringUtil::toSnakeCase( pName, true ) );
        }
    }

    void MaterialUtil::appendQualityDefines( MaterialQualityLevel quality, vector<string>& outListDefine )
    {
        MaterialUtil::appendUniqueDefine( outListDefine, string( "MATERIAL_QUALITY=" ) + to_string( static_cast<uint32>( quality ) ) );
        MaterialUtil::appendUniqueDefine( outListDefine, string( "MATERIAL_QUALITY_" ) + StringUtil::toUpper( MaterialUtil::qualityToString( quality ) ) );
    }

    uint64 MaterialUtil::hashDefines( const vector<string>& listDefine )
    {
        uint64 hash = HashUtil::kFnvOffset64;
        for ( const string& defineStr : listDefine )
        {
            hash = StringUtil::computeHash64( defineStr, false, hash );
            hash = ( hash ^ 0xFFull ) * HashUtil::kFnvPrime64;
        }
        return hash;
    }
} // namespace sw
