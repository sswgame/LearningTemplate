/**
 * @file GBufferLayout.h
 * @brief G버퍼 명세 한 벌(판 번호 있음) — 첨부마다 이름 · 역할 · 포맷 · 클리어 값 · 채널의 뜻과 인코딩. C++ 패스 계약(PSO 포맷), 파이프라인 XML 검증, 디버그 표시(렌더 타깃 창의 값 풀이)가 이 표 하나를 읽는다.
 * @details 셰이더 쪽 쓰기(`SurfaceOutput::pack` — binding_output.hlsli)와 읽기(`PassTexture::loadGbuffer*` — binding_texture.hlsli)는 HLSL 이라 이 표를 include 하지 못한다.
 *          그래서 셰이더는 같은 판 번호(`SW_GBUFFER_LAYOUT_VERSION`, binding_output.hlsli)를 적고 단위 테스트(`GBufferLayoutTest`)가 두 번호를 맞춘다 — 한쪽만 바꾸면 테스트가 진다.
 *          언리얼의 GBuffer 정의(`FGBufferInfo` · ShadingModelID · CustomData)와 같은 자리다. 바꾸려면 이 표의 판을 올리고 셰이더 · 검증 · 표시를 함께 바꾼다.
 *          판 3(이 표): 알베도 알파 = 셰이딩 모델 번호(0 = 그리지 않음 — 옛 그려짐 표시를 대신한다), 머티리얼 = 금속도 · 거칠기 · 반사율 · 가림, 사용자 칸 RGBA8 한 장
 *          (번호마다의 뜻 — shading_model_ids.hlsli), 장면 색(LitColor)에 자체 발광. 대역폭: 1080p 에서 RGBA8 한 장 = 프레임당 쓰기 · 읽기 약 16 MB 더.
 *          병합 지점: W10 이 판 2 로 화면 속도(SceneVelocity, R16G16_FLOAT)를 넷째 MRT 로 더했다 — 합치면 속도 SV_TARGET3, 사용자 칸 4, 장면 색 5 이고 판은 3 그대로다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"

#include "shading_model_ids.hlsli"

namespace sw
{
    /**
     * @brief G버퍼 알베도 알파에 실리는 셰이딩 모델 번호입니다. 값은 셰이더와 같은 표(shading_model_ids.hlsli)에서 온다.
     * @details 병합 지점: W13 의 머티리얼 그래프 Output ShadingModel(DefaultLit | Hair)은 이 번호로 넓힌다 — 번호 표를 두 벌 두지 않는다.
     */
    enum class ShadingModelID : uint8
    {
        None            = SW_SHADING_MODEL_ID_NONE,
        Unlit           = SW_SHADING_MODEL_ID_UNLIT,
        DefaultLit      = SW_SHADING_MODEL_ID_DEFAULT_LIT,
        Subsurface      = SW_SHADING_MODEL_ID_SUBSURFACE,
        Cloth           = SW_SHADING_MODEL_ID_CLOTH,
        ClearCoat       = SW_SHADING_MODEL_ID_CLEAR_COAT,
        Hair            = SW_SHADING_MODEL_ID_HAIR,
        Eye             = SW_SHADING_MODEL_ID_EYE,
        ThinTranslucent = SW_SHADING_MODEL_ID_THIN_TRANSLUCENT,
        Anisotropic     = SW_SHADING_MODEL_ID_ANISOTROPIC,
        Count           = SW_SHADING_MODEL_ID_COUNT,
    };
    static_assert( static_cast<float32>( SW_SHADING_MODEL_ID_COUNT ) <= SW_SHADING_MODEL_ID_SCALE, "번호는 8 비트 UNORM 칸에 실린다(번호 / 255)" );

    /** @brief G버퍼 채널 하나의 인코딩입니다. 디버그 표시가 이것으로 값을 풉니다. */
    enum class GBufferChannelKind : uint8
    {
        Unused,       ///< 쓰지 않는다(0)
        Unorm,        ///< 0 .. 1 값 그대로(알베도 · 금속도 · 거칠기)
        OctahedralU,  ///< 월드 노멀의 옥타헤드럴 좌표 u(-1 .. 1, 다음 채널이 v)
        OctahedralV,  ///< 월드 노멀의 옥타헤드럴 좌표 v
        ShadingModel, ///< 셰이딩 모델 번호(번호 / 255 — `ShadingModelID`). 0 은 그리지 않은 픽셀
        Float,        ///< 저장한 값 그대로(HDR 장면 색 · 발광)
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
        uint8               _bPipelineClear; ///< 1 이면 클리어 값은 파이프라인의 것이다(장면 색 — 배경색). 0 이면 표의 클리어 값이어야 한다
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
         * @details 판 3: 셰이딩 모델 번호 · 반사율 · 머티리얼 가림 · 모델별 사용자 칸 · 발광(장면 색에 미리 적는다)에 화면 속도(SceneVelocity, 판 2)를 더한 여섯 MRT 다.
         *          언리얼 G버퍼의 ShadingModelID · Specular · AO · CustomData 와 베이스 패스의 속도 출력(r.VelocityOutputPass=1)과 같은 자리다.
         */
        static constexpr uint32 kVersion = 3;
        /** @brief 첨부 수입니다(G버퍼 패스의 MRT 수). */
        static constexpr uint32 kAttachmentCount = 6;
        /** @brief 화면 속도 첨부의 MRT 슬롯입니다. 포워드 불투명 패스도 같은 첨부 · 포맷을 둘째 컬러 출력으로 쓴다. */
        static constexpr uint32 kVelocitySlot = 3;

        // clang-format off
        static constexpr GBufferAttachmentSpec kArrAttachment[kAttachmentCount] = {
            { "GBufferAlbedo",   RenderPassInputRole::GBufferAlbedo,   RHIFormat::R8G8B8A8_UNORM, float4{ 0.0f, 0.0f, 0.0f, 0.0f },
              { { "Albedo R", GBufferChannelKind::Unorm }, { "Albedo G", GBufferChannelKind::Unorm }, { "Albedo B", GBufferChannelKind::Unorm }, { "Shading model", GBufferChannelKind::ShadingModel } }, 4, SW_FALSE },
            { "GBufferNormal",   RenderPassInputRole::GBufferNormal,   RHIFormat::R16G16_SNORM,   float4{ 0.0f, 0.0f, 0.0f, 0.0f },
              { { "Normal u", GBufferChannelKind::OctahedralU }, { "Normal v", GBufferChannelKind::OctahedralV }, { "", GBufferChannelKind::Unused }, { "", GBufferChannelKind::Unused } }, 2, SW_FALSE },
            { "GBufferMaterial", RenderPassInputRole::GBufferMaterial, RHIFormat::R8G8B8A8_UNORM, float4{ 0.0f, 0.0f, 0.0f, 0.0f },
              { { "Metallic", GBufferChannelKind::Unorm }, { "Roughness", GBufferChannelKind::Unorm }, { "Specular", GBufferChannelKind::Unorm }, { "Material AO", GBufferChannelKind::Unorm } }, 4, SW_FALSE },
            { "SceneVelocity",   RenderPassInputRole::Velocity,        RHIFormat::R16G16_FLOAT,   float4{ 0.0f, 0.0f, 0.0f, 0.0f },
              { { "Velocity u", GBufferChannelKind::VelocityU }, { "Velocity v", GBufferChannelKind::VelocityV }, { "", GBufferChannelKind::Unused }, { "", GBufferChannelKind::Unused } }, 2, SW_FALSE },
            { "GBufferCustomData", RenderPassInputRole::GBufferCustomData, RHIFormat::R8G8B8A8_UNORM, float4{ 0.0f, 0.0f, 0.0f, 0.0f },
              { { "Custom r (per shading model)", GBufferChannelKind::Unorm }, { "Custom g", GBufferChannelKind::Unorm }, { "Custom b", GBufferChannelKind::Unorm }, { "Custom a", GBufferChannelKind::Unorm } }, 4, SW_FALSE },
            { "LitColor",        RenderPassInputRole::SourceColor,     RHIFormat::R16G16B16A16_FLOAT, float4{ 0.0f, 0.0f, 0.0f, 0.0f },
              { { "Scene R (emissive + lit)", GBufferChannelKind::Float }, { "Scene G", GBufferChannelKind::Float }, { "Scene B", GBufferChannelKind::Float }, { "", GBufferChannelKind::Unused } }, 3, SW_TRUE },
        };
        // clang-format on

        /** @brief MRT 포맷 표입니다(`RenderPassTypeInfo` 의 G버퍼 패스 행이 PSO 포맷으로 쓴다). */
        static constexpr RHIFormat kArrColorFormat[kAttachmentCount] = { kArrAttachment[0]._format, kArrAttachment[1]._format, kArrAttachment[2]._format, kArrAttachment[3]._format,
                                                                         kArrAttachment[4]._format, kArrAttachment[5]._format };

        /** @brief 셰이딩 모델 번호를 8 비트 칸 값으로(셰이더 `SurfaceOutput::encodeShadingModel` 과 같은 식). */
        [[nodiscard]] static constexpr float32 encodeShadingModel( ShadingModelID id ) { return static_cast<float32>( id ) / SW_SHADING_MODEL_ID_SCALE; }
        /** @brief 칸 값을 번호로(셰이더 `SurfaceOutput::decodeShadingModel` 과 같은 식). 표 밖 값이면 Count 입니다. */
        [[nodiscard]] static ShadingModelID decodeShadingModel( float32 encoded );
        /** @brief 번호의 이름입니다(디버그 표시). */
        [[nodiscard]] static const utf8* getShadingModelName( ShadingModelID id );
        static_assert( kArrAttachment[kVelocitySlot]._role == RenderPassInputRole::Velocity, "화면 속도는 G버퍼의 넷째 슬롯이다(셰이더 SurfaceOutput 의 SV_TARGET3)" );

        /** @brief 이름의 첨부 명세입니다. G버퍼 첨부가 아니면 nullptr 입니다. */
        static const GBufferAttachmentSpec* findAttachment( string_view name );
        /**
         * @brief 첨부 @p spec 에서 읽은 텍셀 @p raw(포맷대로 푼 float4)를 뜻 있는 값으로 풉니다 — 노멀은 옥타헤드럴을 풀어 단위 벡터, 셰이딩 모델은 번호(이름은 값 줄의 이름)입니다.
         * @details 셰이더 읽기와 같은 식이다(`OctahedralNormal::decodeOctant` — 셰이더 `Octahedral::decode` 와 같다). 먼저 출력 목록을 비운다.
         */
        static void decodeTexel( const GBufferAttachmentSpec& spec, const float4& raw, vector<GBufferDecodedValue>& outListValue );
    };
} // namespace sw
