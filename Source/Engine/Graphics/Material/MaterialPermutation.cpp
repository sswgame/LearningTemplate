#include "pch.h"

#include "Engine/Graphics/Material/MaterialPermutation.h"

#include "Core/Hash/HashUtil.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Material/MaterialXML.h"
#include "Engine/Reflection/EnumName.h"

namespace sw
{
    uint64 MaterialPermutation::getPermutationGeneration()
    {
        const MaterialCache* const pCache = MaterialCache::findBound();
        return pCache != nullptr ? pCache->getPermutationGeneration() : 0u; // 0 = 모름(세대는 1 부터)
    }

    void MaterialPermutation::bumpPermutationGeneration()
    {
        MaterialCache* const pCache = MaterialCache::findBound();
        if ( pCache != nullptr )
            pCache->bumpPermutationGeneration(); // 엔진 없이 만든 머티리얼은 읽는 쪽도 "모름" 이라 올릴 곳이 없다
    }

    void MaterialPermutation::appendUniqueDefine( vector<string>& outListDefine, string_view define )
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

    void MaterialPermutation::appendUsageDefines( MaterialUsageFlags usage, vector<string>& outListDefine )
    {
        // define 이름은 열거자 이름에서 만든다(`PostProcess` → `MATERIAL_USAGE_POST_PROCESS`). 셰이더 쪽 철자의 원본은 MaterialUsageFlags 하나다.
        for ( uint32 bit = 0; bit < sizeof( MaterialUsageFlags ) * 8; ++bit )
        {
            const MaterialUsageFlags flag = static_cast<MaterialUsageFlags>( 1u << bit );
            if ( EnumUtil::hasFlag( usage, flag ) == false )
                continue;
            const utf8* pName = getEnumName( flag );
            SW_ASSERT( StringUtil::equals( pName, "Unknown" ) == false ); // 비트마다 ENUM 이름이 있어야 define 이 생긴다
            MaterialPermutation::appendUniqueDefine( outListDefine, string( "MATERIAL_USAGE_" ) + StringUtil::toSnakeCase( pName, true ) );
        }
    }

    void MaterialPermutation::appendQualityDefines( MaterialQualityLevel quality, vector<string>& outListDefine )
    {
        MaterialPermutation::appendUniqueDefine( outListDefine, string( "MATERIAL_QUALITY=" ) + to_string( static_cast<uint32>( quality ) ) );
        MaterialPermutation::appendUniqueDefine( outListDefine, string( "MATERIAL_QUALITY_" ) + StringUtil::toUpper( getEnumName( quality ) ) );
    }

    uint64 MaterialPermutation::hashDefines( const vector<string>& listDefine )
    {
        uint64 hash = HashUtil::kFNVOffset64;
        for ( const string& defineStr : listDefine )
        {
            hash = HashUtil::computeStringHash64( defineStr, false, hash );
            hash = HashUtil::fnvStep64( hash, 0xFFull );
        }
        return hash;
    }
} // namespace sw
