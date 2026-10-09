#include "pch.h"

#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"

#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"

namespace sw
{
    namespace
    {
        /// 역할 → 셰이더 이름입니다. 순서는 RenderPassInputRole 과 같습니다.
        constexpr const utf8* s_arrRoleName[static_cast<uint32>( RenderPassInputRole::Count )] = {
            "Invalid",
            "SourceColor",
            "SceneDepth",
            "GBufferAlbedo",
            "GBufferNormal",
            "ShadowMap",
            "AmbientOcclusion",
        };

        /// 이름이 곧 역할인 첨부입니다. 파이프라인이 이 이름을 쓰면 그 역할로 걸립니다.
        struct FixedRoleName
        {
            const utf8*         _pName;
            RenderPassInputRole _role;
        };
        constexpr FixedRoleName s_arrFixedRoleName[] = {
            {"GBufferAlbedo",    RenderPassInputRole::GBufferAlbedo},
            {"GBufferNormal",    RenderPassInputRole::GBufferNormal},
            {    "ShadowMap",        RenderPassInputRole::ShadowMap},
            {      "AOColor", RenderPassInputRole::AmbientOcclusion},
        };
    } // namespace

    const utf8* getRenderPassInputRoleName( RenderPassInputRole role )
    {
        const uint32 index = static_cast<uint32>( role );
        return index < static_cast<uint32>( RenderPassInputRole::Count ) ? s_arrRoleName[index] : s_arrRoleName[0];
    }

    bool tryParseRenderPassInputRole( string_view roleName, RenderPassInputRole& outRole )
    {
        for ( uint32 index = static_cast<uint32>( RenderPassInputRole::SourceColor ); index < static_cast<uint32>( RenderPassInputRole::Count ); ++index )
        {
            if ( roleName == s_arrRoleName[index] )
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

    bool RenderPassInputSignature::reads( RenderPassInputRole role ) const
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

    const RenderPassInputSignature* findRenderPassInputSignature( RenderPassType type )
    {
        const RenderPassTypeInfo& info = getRenderPassTypeInfo( type );
        return info.hasFlag( RenderPassTraitFlag::kHasInputContract ) ? &info._inputContract : nullptr;
    }
} // namespace sw
