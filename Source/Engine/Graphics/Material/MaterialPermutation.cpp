#include "pch.h"

#include "Core/Concurrency/atomic.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialUtil.h"

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

    void MaterialUtil::appendUniqueDefine( vector<string>& outListDefine, string_view def )
    {
        if ( def.empty() )
            return;
        for ( const string& existing : outListDefine )
        {
            if ( existing == def )
                return;
        }
        outListDefine.push_back( string( def ) );
    }

    void MaterialUtil::appendUsageDefines( MaterialUsageFlags usage, vector<string>& outListDefine )
    {
        if ( EnumUtil::hasFlag( usage, MaterialUsageFlags::StaticMesh ) )
            MaterialUtil::appendUniqueDefine( outListDefine, "MATERIAL_USAGE_STATIC_MESH" );
        if ( EnumUtil::hasFlag( usage, MaterialUsageFlags::SkeletalMesh ) )
            MaterialUtil::appendUniqueDefine( outListDefine, "MATERIAL_USAGE_SKELETAL_MESH" );
        if ( EnumUtil::hasFlag( usage, MaterialUsageFlags::Instanced ) )
            MaterialUtil::appendUniqueDefine( outListDefine, "MATERIAL_USAGE_INSTANCED" );
        if ( EnumUtil::hasFlag( usage, MaterialUsageFlags::Particles ) )
            MaterialUtil::appendUniqueDefine( outListDefine, "MATERIAL_USAGE_PARTICLES" );
        if ( EnumUtil::hasFlag( usage, MaterialUsageFlags::Decal ) )
            MaterialUtil::appendUniqueDefine( outListDefine, "MATERIAL_USAGE_DECAL" );
        if ( EnumUtil::hasFlag( usage, MaterialUsageFlags::UI ) )
            MaterialUtil::appendUniqueDefine( outListDefine, "MATERIAL_USAGE_UI" );
        if ( EnumUtil::hasFlag( usage, MaterialUsageFlags::PostProcess ) )
            MaterialUtil::appendUniqueDefine( outListDefine, "MATERIAL_USAGE_POSTPROCESS" );
        if ( EnumUtil::hasFlag( usage, MaterialUsageFlags::LightFunction ) )
            MaterialUtil::appendUniqueDefine( outListDefine, "MATERIAL_USAGE_LIGHTFUNCTION" );
        if ( EnumUtil::hasFlag( usage, MaterialUsageFlags::MorphTargets ) )
            MaterialUtil::appendUniqueDefine( outListDefine, "MATERIAL_USAGE_MORPHTARGETS" );
        if ( EnumUtil::hasFlag( usage, MaterialUsageFlags::SplineMesh ) )
            MaterialUtil::appendUniqueDefine( outListDefine, "MATERIAL_USAGE_SPLINEMESH" );
    }

    void MaterialUtil::appendQualityDefines( MaterialQualityLevel quality, vector<string>& outListDefine )
    {
        MaterialUtil::appendUniqueDefine( outListDefine, string( "MATERIAL_QUALITY=" ) + to_string( static_cast<uint32>( quality ) ) );
        MaterialUtil::appendUniqueDefine( outListDefine, string( "MATERIAL_QUALITY_" ) + StringUtil::toUpper( MaterialUtil::qualityToString( quality ) ) );
    }

    uint64 MaterialUtil::hashDefines( const vector<string>& listDefine )
    {
        uint64 hash = StringUtil::kOffset64;
        for ( const string& defineStr : listDefine )
        {
            hash = StringUtil::computeHash64( defineStr, false, hash );
            hash = ( hash ^ 0xFFull ) * StringUtil::kPrime64;
        }
        return hash;
    }
} // namespace sw
