/**
 * @file RenderPassAsset.h
 * @brief 렌더 패스 XML(여러 파이프라인이 함께 쓰는 첨부 묶음) 에셋을 정의합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/Foundation.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Renderer/Pipeline/RenderGraphPassDesc.h"

namespace sw
{
    /**
     * @brief 여러 파이프라인이 함께 쓰는 묶음 서술입니다(첨부 · 프레임 자원 · 프레임 패스). 파이프라인이 `_listRenderPassRef` 로 가져옵니다.
     * @details 뷰 그래프(`_listPass`)는 RenderPipelineDesc 가 담당한다. 프레임 패스는 셰이딩 방식과 무관해 포워드 · 디퍼드가 한 벌을 나눈다(`framecomputerenderpass.xml`).
     */
    REFLECT()
    struct RenderPassDesc
    {
        REFLECT_BODY();
        PROPERTY()
        string _name = "DefaultMainPass";

        PROPERTY( SkipIfEmpty )
        vector<RenderPassAttachment> _listAttachment;

        PROPERTY( SkipIfEmpty )
        vector<RenderGraphResourceDesc> _listResource;

        PROPERTY( SkipIfEmpty )
        vector<RenderGraphPassDesc> _listFramePass;
    };
} // namespace sw

namespace sw
{
    /// @brief RenderPass XML 에셋입니다.
    class SW_API RenderPassAsset
    {
    public:
        /** @brief XML 파일에서 렌더 패스 디스크립터를 로드합니다. */
        [[nodiscard]] bool loadFromXMLFile( const AssetID& assetID );

        const RenderPassDesc& getDesc() const { return _desc; }

    private:
        RenderPassDesc _desc;
    };
} // namespace sw
