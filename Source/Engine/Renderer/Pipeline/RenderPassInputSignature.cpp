#include "pch.h"

#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
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
            "BloomBright",
            "GBufferMaterial",
            "SceneColorCopy",
        };

        /// 이름이 곧 역할인 첨부입니다. 파이프라인이 이 이름을 쓰면 그 역할로 걸립니다.
        struct FixedRoleName
        {
            const utf8*         _pName;
            RenderPassInputRole _role;
        };
        constexpr FixedRoleName s_arrFixedRoleName[] = {
            {  "GBufferAlbedo",    RenderPassInputRole::GBufferAlbedo},
            {  "GBufferNormal",    RenderPassInputRole::GBufferNormal},
            {"GBufferMaterial",  RenderPassInputRole::GBufferMaterial},
            {      "ShadowMap",        RenderPassInputRole::ShadowMap},
            {        "AOColor", RenderPassInputRole::AmbientOcclusion},
            {    "BloomBright",      RenderPassInputRole::BloomBright},
            { "SceneColorCopy",   RenderPassInputRole::SceneColorCopy},
        };
    } // namespace

    const utf8* getRenderPassInputRoleName( RenderPassInputRole role )
    {
        const uint32 index = static_cast<uint32>( role );
        return index < static_cast<uint32>( RenderPassInputRole::Count ) ? s_arrRoleName[index] : s_arrRoleName[0];
    }

    EmulatedTextureSlot getEmulatedTextureSlot( RenderPassInputRole role )
    {
        if ( role == RenderPassInputRole::Invalid || role == RenderPassInputRole::Count )
            return EmulatedTextureSlot::None;
        uint32     slot{ 0 };
        const bool bFound = shaderslot::emulated::tryFindSlot( getRenderPassInputRoleName( role ), slot );
        SW_ASSERT( bFound ); // 모든 역할이 표에 있어야 한다 — 빠진 역할은 같은 슬롯 충돌 검사를 조용히 건너뛴다
        if ( bFound == false )
            return EmulatedTextureSlot::None;
        if ( slot == shaderslot::kEngineGBufferMaterialTexture )
            return EmulatedTextureSlot::GBufferMaterial;
        return static_cast<EmulatedTextureSlot>( slot - shaderslot::kEngineTexture0 );
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
