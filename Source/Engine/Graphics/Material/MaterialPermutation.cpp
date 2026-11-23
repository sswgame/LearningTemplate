#include "pch.h"

#include "Engine/Graphics/Material/MaterialPermutation.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/BitUtil.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Material/MaterialXML.h"
#include "Engine/Reflection/EnumName.h"

namespace sw
{
    namespace
    {
        /** @brief 기능 define 하나의 조건입니다. 조건이 없는 칸은 비운다. */
        struct MaterialFeatureRule
        {
            static constexpr uint32 kMaxConflictCount = 7;
            static constexpr uint32 kAnyAlphaMode     = BitUtil::bit( static_cast<uint32>( MaterialAlphaMode::Opaque ) ) | BitUtil::bit( static_cast<uint32>( MaterialAlphaMode::Mask ) ) |
                                                    BitUtil::bit( static_cast<uint32>( MaterialAlphaMode::Blend ) );
            static constexpr uint32 kBlendOnly  = BitUtil::bit( static_cast<uint32>( MaterialAlphaMode::Blend ) );
            static constexpr uint32 kOpaqueOnly = BitUtil::bit( static_cast<uint32>( MaterialAlphaMode::Opaque ) );
            static constexpr uint32 kNotBlend   = kAnyAlphaMode & ~kBlendOnly;

            const utf8* _pDefine{ nullptr };
            uint32      _alphaModeMask{ kAnyAlphaMode };         ///< 받는 알파 모드(비트 = `MaterialAlphaMode`)
            const utf8* _pReason{ nullptr };                     ///< 다른 알파 모드가 안 되는 까닭
            const utf8* _pRequiredProperty{ nullptr };           ///< 셰이더가 읽는 필수 프로퍼티
            const utf8* _arrConflictDefine[kMaxConflictCount]{}; ///< 함께 쓸 수 없는 기능 define
        };

        /** @brief 기능 define 표입니다 — 새 기능은 여기 한 줄이다. */
        constexpr MaterialFeatureRule kArrFeatureRule[] = {
            {       kMaterialRefractionDefine,MaterialFeatureRule::kBlendOnly,"refraction reads the scene colour copy, which exists only for the transparent pass",nullptr,                                                                                                                                           {}                                                                                                                                                                                                          },
            {       kMaterialDistortionDefine,    MaterialFeatureRule::kBlendOnly,                                 "the distortion pass draws transparent batches only",                          nullptr,                                                                                                                                                                             {}},
            {          kMaterialOutlineDefine,     MaterialFeatureRule::kNotBlend,                                    "the mesh outline pass draws opaque batches only",                          nullptr,                                                                                                                                                                             {}},
            {     kMaterialDisplacementDefine, MaterialFeatureRule::kAnyAlphaMode,                                                                              nullptr, kMaterialDisplacementMapProperty,                                                                                                                                                     { kMaterialOutlineDefine }},
            {kMaterialDomainPostProcessDefine,
             MaterialFeatureRule::kOpaqueOnly,
             "a post process material is a fullscreen draw without alpha",
             nullptr,
             { kMaterialDomainSurfaceDefine, kMaterialDistortionDefine, kMaterialOutlineDefine, kMaterialVertexDeformDefine, kMaterialDisplacementDefine, kMaterialRefractionDefine, kMaterialTranslucentDefine }                                                                                                                                                                        },
        };
    } // namespace

    bool MaterialPermutation::validateFeatures( const MaterialDef& desc, const vector<MaterialProperty>& listProperty, const vector<string>& listDefine, string_view sourceLabel )
    {
        const auto hasDefine = [&listDefine]( string_view define )
        {
            return VectorUtil::containsIf( listDefine, [define]( const string& item )
            { return string_view( item ) == define; } );
        };
        bool bSupported = true;
        for ( const MaterialFeatureRule& rule : kArrFeatureRule )
        {
            if ( hasDefine( rule._pDefine ) == false )
                continue;
            if ( ( rule._alphaModeMask & BitUtil::bit( static_cast<uint32>( desc._alphaMode ) ) ) == 0u )
            {
                SW_LOG_ERROR_ID( "Material.UnsupportedFeature", "Material '%#' (%#): %# does not work with alphaMode %# - %#", desc._name, sourceLabel, rule._pDefine, getEnumName( desc._alphaMode ), rule._pReason );
                bSupported = false;
            }
            for ( const utf8* pConflict : rule._arrConflictDefine )
            {
                if ( pConflict == nullptr || hasDefine( pConflict ) == false )
                    continue;
                SW_LOG_ERROR_ID( "Material.UnsupportedFeature", "Material '%#' (%#): %# and %# cannot be drawn together", desc._name, sourceLabel, rule._pDefine, pConflict );
                bSupported = false;
            }
            const bool bPropertyMissing = rule._pRequiredProperty != nullptr && VectorUtil::containsIf( listProperty, [&rule]( const MaterialProperty& prop )
            { return string_view( prop._name ) == rule._pRequiredProperty; } ) == false;
            if ( bPropertyMissing )
            {
                SW_LOG_ERROR_ID( "Material.UnsupportedFeature", "Material '%#' (%#): %# needs the property '%#'", desc._name, sourceLabel, rule._pDefine, rule._pRequiredProperty );
                bSupported = false;
            }
        }
        return bSupported;
    }

    uint64 MaterialPermutation::getPermutationGeneration()
    {
        const MaterialCache* const pCache = MaterialCache::findBound();
        return pCache != nullptr ? pCache->getPermutationLog().getGeneration() : 0u; // 0 = 모름(세대는 1 부터)
    }

    void MaterialPermutation::recordPermutationChange( const void* pOwner )
    {
        MaterialCache* const pCache = MaterialCache::findBound();
        if ( pCache != nullptr )
            pCache->getPermutationLog().record( pOwner ); // 엔진 없이 만든 머티리얼은 읽는 쪽도 "모름" 이라 적을 곳이 없다
    }

    void MaterialPermutation::appendUniqueDefine( vector<string>& outListDefine, string_view define )
    {
        if ( define.empty() )
            return;
        VectorUtil::addUnique( outListDefine, define );
    }

    void MaterialPermutation::mergeDefine( vector<string>& inoutListDefine, string_view define )
    {
        if ( define.empty() )
            return;
        const string_view name = define.substr( 0, define.find( '=' ) );
        for ( string& existing : inoutListDefine )
        {
            if ( string_view( existing ).substr( 0, string_view( existing ).find( '=' ) ) == name )
            {
                existing = string( define );
                return;
            }
        }
        inoutListDefine.push_back( string( define ) );
    }

    string MaterialPermutation::makeTextureCountDefine( uint32 count )
    {
        StringBuilder<constant::buffer::kMax64> builder;
        builder.appendFormat( "%#=%#", kMaterialTextureCountDefine, count );
        return string( builder.view() );
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
            StringBuilder<constant::buffer::kMax64> define;
            define.appendFormat( "MATERIAL_USAGE_%#", StringUtil::toSnakeCase( pName, true ) );
            MaterialPermutation::appendUniqueDefine( outListDefine, define.view() );
        }
    }

    void MaterialPermutation::appendQualityDefines( MaterialQualityLevel quality, vector<string>& outListDefine )
    {
        StringBuilder<constant::buffer::kMax64> define;
        define.appendFormat( "MATERIAL_QUALITY=%#", static_cast<uint32>( quality ) );
        MaterialPermutation::appendUniqueDefine( outListDefine, define.view() );
        define.clear();
        define.appendFormat( "MATERIAL_QUALITY_%#", StringUtil::toUpper( getEnumName( quality ) ) );
        MaterialPermutation::appendUniqueDefine( outListDefine, define.view() );
    }

    void MaterialPermutation::appendSurfaceModelDefines( MaterialSurfaceModel model, vector<string>& outListDefine )
    {
        StringBuilder<constant::buffer::kMax64> define;
        define.appendFormat( "MATERIAL_SURFACE_MODEL_%#", StringUtil::toUpper( getEnumName( model ) ) );
        MaterialPermutation::appendUniqueDefine( outListDefine, define.view() );
        if ( isForwardOnlySurfaceModel( model ) )
            MaterialPermutation::appendUniqueDefine( outListDefine, kMaterialForwardOnlyDefine );
    }

    bool MaterialPermutation::isForwardOnlySurfaceModel( MaterialSurfaceModel model )
    {
        return model == MaterialSurfaceModel::Toon || model == MaterialSurfaceModel::Hair || model == MaterialSurfaceModel::CustomLit;
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
