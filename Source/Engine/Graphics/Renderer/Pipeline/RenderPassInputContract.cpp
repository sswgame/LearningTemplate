#include "pch.h"

#include "Engine/Graphics/Renderer/Pipeline/RenderPassInputContract.h"

namespace sw
{
    namespace
    {
        /// 역할 → 셰이더 이름. 순서는 RenderPassInputRole 과 같다.
        constexpr const utf8* s_arrRoleName[static_cast<uint32>( RenderPassInputRole::Count )] = {
            "Invalid",
            "SourceColor",
            "SceneDepth",
            "GBufferAlbedo",
            "GBufferNormal",
            "ShadowMap",
            "AmbientOcclusion",
        };

        /// 이름이 곧 역할인 첨부. 파이프라인이 이 이름을 쓰면 그 역할로 걸린다.
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

        /// 풀스크린 패스 타입의 계약. 여기 없는 타입은 메시 패스다.
        constexpr RenderPassInputContract s_arrContract[] = {
            {RenderPassType::Lighting, { RenderPassInputRole::GBufferAlbedo, RenderPassInputRole::GBufferNormal, RenderPassInputRole::SceneDepth }, 3,                                                                           { RenderPassInputRole::ShadowMap }, 1},
            {    RenderPassType::SSAO,                                     { RenderPassInputRole::GBufferNormal, RenderPassInputRole::SceneDepth }, 2,                                                                                                           {}, 0},
            {   RenderPassType::Bloom,                                                                        { RenderPassInputRole::SourceColor }, 1,                                                                    { RenderPassInputRole::AmbientOcclusion }, 1},
            { RenderPassType::Outline,                                       { RenderPassInputRole::SourceColor, RenderPassInputRole::SceneDepth }, 2,                                                                                                           {}, 0},
            {     RenderPassType::TAA,                                                                        { RenderPassInputRole::SourceColor }, 1,                                                                                                           {}, 0},
            { RenderPassType::Tonemap,                                                                        { RenderPassInputRole::SourceColor }, 1,                                                                                                           {}, 0},
            // Present 는 선언 입력이 없으면 "가장 나중 컬러" 후보 사슬로 폴백한다(resolvePresentSource) — 그래서 선택이다.
            // 깊이·AO 도 선택으로 받는다: Present 가 후처리 체인(postchain.hlsl)을 겸할 수 있기 때문이다.
            // 전체화면 패스는 중간 타깃 왕복이 계산보다 비싸서, 마지막 패스 하나로 합치는 것이 가장 싸다.
            { RenderPassType::Present,                                                                                                          {}, 0, { RenderPassInputRole::SourceColor, RenderPassInputRole::SceneDepth, RenderPassInputRole::AmbientOcclusion }, 3},
        };
    } // namespace

    const utf8* getRenderPassInputRoleName( RenderPassInputRole role )
    {
        const uint32 index = static_cast<uint32>( role );
        return index < static_cast<uint32>( RenderPassInputRole::Count ) ? s_arrRoleName[index] : s_arrRoleName[0];
    }

    RenderPassInputRole resolveRenderPassInputRole( string_view attachmentName, bool bDepthFormat )
    {
        for ( const FixedRoleName& fixed : s_arrFixedRoleName )
        {
            if ( attachmentName == fixed._pName )
                return fixed._role;
        }
        return bDepthFormat ? RenderPassInputRole::SceneDepth : RenderPassInputRole::SourceColor;
    }

    bool RenderPassInputContract::reads( RenderPassInputRole role ) const
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

    const RenderPassInputContract* findRenderPassInputContract( RenderPassType type )
    {
        for ( const RenderPassInputContract& contract : s_arrContract )
        {
            if ( contract._type == type )
                return &contract;
        }
        return nullptr;
    }
} // namespace sw
