#include "pch.h"

#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"

#include "Core/Container/EnumArray.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Pipeline/RenderPassKindInfo.h"

namespace sw
{
    namespace
    {
        /// 역할 → 셰이더 이름입니다. 순서는 RenderPassInputRole 과 같습니다.
        constexpr auto s_arrRoleName = makeEnumTable<RenderPassInputRole, const utf8*>( {
            "Invalid",
            "SourceColor",
            "SceneDepth",
            "GBufferAlbedo",
            "GBufferNormal",
            "ShadowMap",
            "AmbientOcclusion",
            "BloomBright",
            "GBufferMaterial",
            "SceneColorCopy",
            "SceneDepthCopy",
            "Velocity",
            "Distortion",
            "ContactShadow",
            "GBufferCustomData",
            "LocalShadowMap",
            "VelocityNeighborMax",
            "DBufferA",
            "DBufferB",
            "DBufferC",
        } );

        /// 이름이 곧 역할인 첨부입니다. 파이프라인이 이 이름을 쓰면 그 역할로 걸립니다.
        struct FixedRoleName
        {
            const utf8*         _pName;
            RenderPassInputRole _role;
        };
        constexpr FixedRoleName s_arrFixedRoleName[] = {
            {      "GBufferAlbedo",       RenderPassInputRole::GBufferAlbedo},
            {      "GBufferNormal",       RenderPassInputRole::GBufferNormal},
            {    "GBufferMaterial",     RenderPassInputRole::GBufferMaterial},
            {  "GBufferCustomData",   RenderPassInputRole::GBufferCustomData},
            {          "ShadowMap",           RenderPassInputRole::ShadowMap},
            {     "LocalShadowMap",      RenderPassInputRole::LocalShadowMap},
            {            "AOColor",    RenderPassInputRole::AmbientOcclusion},
            {        "BloomBright",         RenderPassInputRole::BloomBright},
            {     "SceneColorCopy",      RenderPassInputRole::SceneColorCopy},
            {     "SceneDepthCopy",      RenderPassInputRole::SceneDepthCopy},
            {      "SceneVelocity",            RenderPassInputRole::Velocity},
            {    "SceneDistortion",          RenderPassInputRole::Distortion},
            {  "ContactShadowMask",       RenderPassInputRole::ContactShadow},
            {"VelocityNeighborMax", RenderPassInputRole::VelocityNeighborMax},
            {           "DBufferA",            RenderPassInputRole::DBufferA},
            {           "DBufferB",            RenderPassInputRole::DBufferB},
            {           "DBufferC",            RenderPassInputRole::DBufferC},
        };
    } // namespace

    const utf8* getRenderPassInputRoleName( RenderPassInputRole role )
    {
        return role < RenderPassInputRole::Count ? s_arrRoleName[role] : s_arrRoleName[RenderPassInputRole::Invalid];
    }

    bool tryParseRenderPassInputRole( string_view roleName, RenderPassInputRole& outRole )
    {
        for ( uint32 index = static_cast<uint32>( RenderPassInputRole::SourceColor ); index < static_cast<uint32>( RenderPassInputRole::Count ); ++index )
        {
            if ( roleName == s_arrRoleName[static_cast<RenderPassInputRole>( index )] )
            {
                outRole = static_cast<RenderPassInputRole>( index );
                return true;
            }
        }
        return false;
    }

    RenderPassInputRole resolveRenderPassInputRole( string_view attachmentName, bool bDepthFormat, string_view declaredRole )
    {
        RenderPassInputRole declared{ RenderPassInputRole::Invalid };
        if ( declaredRole.empty() == false && tryParseRenderPassInputRole( declaredRole, declared ) )
            return declared;
        for ( const FixedRoleName& fixed : s_arrFixedRoleName )
        {
            if ( attachmentName == fixed._pName )
                return fixed._role;
        }
        return bDepthFormat ? RenderPassInputRole::SceneDepth : RenderPassInputRole::SourceColor;
    }

    bool RenderPassInputSignature::hasRole( RenderPassInputRole role ) const
    {
        for ( uint32 index = 0; index < _requiredCount; ++index )
        {
            if ( _arrRequired[index] == role )
                return true;
        }
        for ( uint32 index = 0; index < _optionalCount; ++index )
        {
            if ( _arrOptional[index] == role )
                return true;
        }
        return false;
    }

    const RenderPassInputSignature* findRenderPassInputSignature( RenderPassKind type )
    {
        const RenderPassKindInfo& info = getRenderPassKindInfo( type );
        return info.hasFlag( RenderPassTraitFlag::kHasInputContract ) ? &info._inputContract : nullptr;
    }
} // namespace sw
