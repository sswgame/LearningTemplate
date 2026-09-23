/**
 * @file RenderPassInputContract.h
 * @brief 풀스크린 패스 타입이 **읽는 입력의 역할**입니다. 로드 시점 검증과 프레임 실행이 같은 표를 봅니다.
 * @details 예전에는 패스마다 코드가 "어느 첨부를 SourceColor 로 걸지" 를 후보 목록으로 짐작했고
 *          (`pickFirstExisting( { "TransparentColor", "LitColor", … } )`), XML 이 선언한 입력은 그래프
 *          정렬에만 쓰였습니다. 그래서 디퍼드 XML 이 Bloom 의 입력으로 `AOColor` 를 적어 두어도 아무도 걸지
 *          않았고 SSAO 는 매 프레임 돌고 버려졌습니다(백로그 1-6). 선언이 곧 바인딩이 되려면 "이 패스
 *          타입은 어떤 역할의 입력을 읽는가" 가 한 표에 있어야 합니다. 그 표가 이 파일입니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Common/Common.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassResource.h"

namespace sw
{
    /**
     * @enum RenderPassInputRole
     * @brief 첨부가 패스 안에서 맡는 역할입니다. 셰이더는 역할 이름(`g_<Role>Index`)으로 읽습니다.
     * @details 첨부 **이름**은 파이프라인이 정하고(LitColor, BloomColor …) 역할은 패스가 정합니다. 같은
     *          첨부가 Bloom 에서는 SourceColor 이고 Transparent 에서는 렌더 타깃입니다.
     */
    enum class RenderPassInputRole : uint8
    {
        Invalid = 0,
        SourceColor,      ///< 이 패스가 가공할 컬러 (직전 패스의 출력). 패스당 하나.
        SceneDepth,       ///< 씬 깊이 (깊이 포맷 첨부 전부)
        GBufferAlbedo,    ///< G버퍼 알베도
        GBufferNormal,    ///< G버퍼 노멀
        ShadowMap,        ///< 그림자 깊이
        AmbientOcclusion, ///< SSAO 결과 (AOColor)
        Count,
    };

    /** @brief 역할의 셰이더 이름을 반환합니다. `g_<Name>Index` 의 `<Name>` 이자 FrameResourceRegistry 의 키입니다. */
    const utf8* getRenderPassInputRoleName( RenderPassInputRole role );

    /**
     * @brief 첨부 이름 · 포맷으로 역할을 정합니다.
     * @details 고정 역할 이름(GBufferAlbedo · GBufferNormal · ShadowMap · AOColor)은 그 역할, 그 밖의 깊이 포맷은
     *          SceneDepth, 나머지 컬러는 SourceColor 입니다. ShadowMap 은 깊이 포맷이지만 이름이 먼저입니다.
     */
    RenderPassInputRole resolveRenderPassInputRole( string_view attachmentName, bool bDepthFormat );

    /**
     * @struct RenderPassInputContract
     * @brief 패스 타입 하나가 읽는 역할 목록입니다. 필수가 빠지면 검증 오류이고, 목록에 없는 역할을 선언해도 오류입니다.
     */
    struct RenderPassInputContract
    {
        static constexpr uint32 kMaxRole = 4;

        RenderPassType      _type{ RenderPassType::Invalid };
        RenderPassInputRole _arrRequired[kMaxRole]{};
        uint32              _requiredCount{ 0 };
        RenderPassInputRole _arrOptional[kMaxRole]{};
        uint32              _optionalCount{ 0 };

        /** @brief 이 역할을 읽는지(필수 또는 선택) 확인합니다. */
        bool reads( RenderPassInputRole role ) const;
    };

    /**
     * @brief 풀스크린 패스 타입의 입력 계약을 반환합니다. 메시 패스(Shadow · GBuffer · ForwardOpaque · Transparent …)는 nullptr 입니다.
     * @details 메시 패스의 입력은 지오메트리 드로우가 정하고(그림자 · 인스턴스 · 머티리얼), 선언은 그래프 순서용입니다.
     */
    const RenderPassInputContract* findRenderPassInputContract( RenderPassType type );
} // namespace sw
