/**
 * @file GBufferLayout.h
 * @brief G버퍼 명세 한 벌(판 번호 있음) — 첨부마다 이름 · 역할 · 포맷 · 클리어 값 · 채널의 뜻과 인코딩. C++ 패스 계약(PSO 포맷), 파이프라인 XML 검증, 디버그 표시(렌더 타깃 창의 값 풀이)가 이 표 하나를 읽는다.
 * @details 셰이더 쪽 쓰기(`SurfaceOutput::pack` — binding_output.hlsli)와 읽기(`PassTexture::loadGbuffer*` — binding_texture.hlsli)는 HLSL 이라 이 표를 include 하지 못한다.
 *          그래서 셰이더는 같은 판 번호(`SW_GBUFFER_LAYOUT_VERSION`, binding_output.hlsli)를 적고 단위 테스트(`GBufferLayoutTest`)가 두 번호를 맞춘다 — 한쪽만 바꾸면 테스트가 진다.
 *          언리얼의 GBuffer 정의(`FGBufferInfo` · ShadingModelID)와 같은 자리다. 현재 알파에 실린 셰이딩 모델(Lit · Unlit)과 그려짐 표시는 그대로 두고 ID 를 덧붙일 수 있다고 가정하지 않는다
 *          — 바꾸려면 이 표의 판을 올리고 셰이더 · 검증 · 표시를 함께 바꾼다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"

namespace sw
{
    /** @brief G버퍼 채널 하나의 인코딩입니다. 디버그 표시가 이것으로 값을 풉니다. */
    enum class GBufferChannelKind : uint8
    {
        Unused,       ///< 쓰지 않는다(0)
        Unorm,        ///< 0 .. 1 값 그대로(알베도 · 금속도 · 거칠기)
        OctahedralU,  ///< 월드 노멀의 옥타헤드럴 좌표 u(-1 .. 1, 다음 채널이 v)
        OctahedralV,  ///< 월드 노멀의 옥타헤드럴 좌표 v
        ShadingModel, ///< 1 조명(Lit), 0 조명 없음(Unlit) — `SW_GBUFFER_SHADING_*`
        Drawn,        ///< 클리어 0, G버퍼 패스가 그리면 1
        VelocityU,    ///< 화면 속도 u(이번 UV - 지난 UV, 다음 채널이 v). 클리어 0 은 "움직임 없음" 이다
        VelocityV,    ///< 화면 속도 v
    };

    /** @brief 채널 하나입니다. */
    struct GBufferChannelSpec
    {
        const utf8*        _pMeaning;
        GBufferChannelKind _kind;
    };

    /** @brief G버퍼 첨부 하나입니다. 순서는 MRT 슬롯(SV_TARGET0 ..) 순서입니다. */
    struct GBufferAttachmentSpec
    {
        const utf8*         _pName; ///< 파이프라인 XML 의 첨부 이름이자 입력 역할 이름
        RenderPassInputRole _role;
        RHIFormat           _format;
        float4              _clearColor;
        GBufferChannelSpec  _arrChannel[4];
        uint32              _channelCount;
    };

    /** @brief 풀어낸 값 하나입니다(디버그 표시가 줄 하나로 보인다). */
    struct GBufferDecodedValue
    {
        const utf8* _pName;
        float3      _value;
        uint32      _componentCount; ///< 1 이면 `_value._x` 만, 3 이면 벡터
    };

    /**
     * @struct GBufferLayout
     * @brief G버퍼 명세입니다. 파일 머리말 참고.
     */
    struct SW_API GBufferLayout
    {
        /**
         * @brief 명세의 판입니다. 첨부 · 채널 · 인코딩이 바뀌면 올리고 binding_output.hlsli 의 `SW_GBUFFER_LAYOUT_VERSION` 도 같이 올린다.
         * @details 판 2: 화면 속도(SceneVelocity)를 네 번째 MRT 로 더했다 — 언리얼 베이스 패스의 속도 출력(r.VelocityOutputPass=1)과 같은 자리다.
         */
        static constexpr uint32 kVersion = 2;
        /** @brief 첨부 수입니다(G버퍼 패스의 MRT 수). */
        static constexpr uint32 kAttachmentCount = 4;
        /** @brief 화면 속도 첨부의 MRT 슬롯입니다(마지막). 포워드 불투명 패스도 같은 첨부 · 포맷을 마지막 컬러 출력으로 쓴다. */
        static constexpr uint32 kVelocitySlot = 3;

        // clang-format off
        static constexpr GBufferAttachmentSpec kArrAttachment[kAttachmentCount] = {
            { "GBufferAlbedo",   RenderPassInputRole::GBufferAlbedo,   RHIFormat::R8G8B8A8_UNORM, float4{ 0.0f, 0.0f, 0.0f, 0.0f },
              { { "Albedo R", GBufferChannelKind::Unorm }, { "Albedo G", GBufferChannelKind::Unorm }, { "Albedo B", GBufferChannelKind::Unorm }, { "Shading model", GBufferChannelKind::ShadingModel } }, 4 },
            { "GBufferNormal",   RenderPassInputRole::GBufferNormal,   RHIFormat::R16G16_SNORM,   float4{ 0.0f, 0.0f, 0.0f, 0.0f },
              { { "Normal u", GBufferChannelKind::OctahedralU }, { "Normal v", GBufferChannelKind::OctahedralV }, { "", GBufferChannelKind::Unused }, { "", GBufferChannelKind::Unused } }, 2 },
            { "GBufferMaterial", RenderPassInputRole::GBufferMaterial, RHIFormat::R8G8B8A8_UNORM, float4{ 0.0f, 0.0f, 0.0f, 0.0f },
              { { "Metallic", GBufferChannelKind::Unorm }, { "Roughness", GBufferChannelKind::Unorm }, { "Unused", GBufferChannelKind::Unused }, { "Drawn", GBufferChannelKind::Drawn } }, 4 },
            { "SceneVelocity",   RenderPassInputRole::Velocity,        RHIFormat::R16G16_FLOAT,   float4{ 0.0f, 0.0f, 0.0f, 0.0f },
              { { "Velocity u", GBufferChannelKind::VelocityU }, { "Velocity v", GBufferChannelKind::VelocityV }, { "", GBufferChannelKind::Unused }, { "", GBufferChannelKind::Unused } }, 2 },
        };
        // clang-format on

        /** @brief MRT 포맷 표입니다(`RenderPassTypeInfo` 의 G버퍼 패스 행이 PSO 포맷으로 쓴다). */
        static constexpr RHIFormat kArrColorFormat[kAttachmentCount] = { kArrAttachment[0]._format, kArrAttachment[1]._format, kArrAttachment[2]._format, kArrAttachment[3]._format };
        static_assert( kArrAttachment[kVelocitySlot]._role == RenderPassInputRole::Velocity, "화면 속도는 G버퍼의 마지막 슬롯이다(셰이더 SurfaceOutput 의 SV_TARGET3)" );

        /** @brief 이름의 첨부 명세입니다. G버퍼 첨부가 아니면 nullptr 입니다. */
        static const GBufferAttachmentSpec* findAttachment( string_view name );
        /**
         * @brief 첨부 @p spec 에서 읽은 텍셀 @p raw(포맷대로 푼 float4)를 뜻 있는 값으로 풉니다 — 노멀은 옥타헤드럴을 풀어 단위 벡터, 셰이딩 모델 · 그려짐은 0/1, 화면 속도는 u · v 값 그대로입니다.
         * @details 셰이더 읽기와 같은 식이다(`OctahedralNormal::decodeOctant` — 셰이더 `Octahedral::decode` 와 같다). 먼저 출력 목록을 비운다.
         */
        static void decodeTexel( const GBufferAttachmentSpec& spec, const float4& raw, vector<GBufferDecodedValue>& outListValue );
    };
} // namespace sw
