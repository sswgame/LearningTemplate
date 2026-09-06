/**
 * @file RenderPipelineResource.h
 * @brief 프레임 렌더 파이프라인 디스크립터 (패스 그래프 + 임시 어태치먼트)
 *
 * RenderPassResource와 구분:
 * - RenderPass  = 어태치먼트 세트 / RHI 바인드 타깃 (포맷, 클리어)
 * - Pipeline    = 프레임 그래프 + 패스별 PSO (셰이더, 엔트리, 블렌드/깊이, permutation)
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Common/Common.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassResource.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /// @brief RenderPipeline XML 서술 (패스 + 어태치먼트)
    REFLECT()
    struct RenderPipelineDesc
    {
        REFLECT_BODY();
        PROPERTY()
        string _name = "ForwardPipeline";

        /** @brief 선택 힌트: Forward / Deferred / Custom */
        PROPERTY()
        string _shadingModel = "Forward";

        /** @brief 그래프가 쓰는 임시/논리 어태치먼트 */
        PROPERTY()
        vector<RenderPassAttachment> _listAttachment;

        /** @brief 그래프 노드 (이름, 타입, 입력, 출력) */
        PROPERTY()
        vector<RenderGraphPassDesc> _listPass;

        /**
         * @brief RHI 템플릿으로 쓰는 RenderPass XML 경로 (선택)
         * (예: renderpass/defaultrenderpass.xml 또는 engine/renderpass/...)
         */
        PROPERTY()
        vector<string> _listRenderPassRef;
    };

    /**
     * @class RenderPipelineResource
     * @brief RenderPipelineDesc XML을 읽고 쓰며 FrameRenderer에 그래프 패스를 제공합니다
     */
    class SW_API RenderPipelineResource
    {
    public:
        /** @brief 빈 파이프라인 디스크립터. */
        RenderPipelineResource() = default;
        /** @brief 가상 소멸. */
        virtual ~RenderPipelineResource() = default;

        /** @brief 복사를 금지합니다. */
        RenderPipelineResource( const RenderPipelineResource& ) = delete;
        /** @brief 대입을 금지합니다. */
        RenderPipelineResource& operator=( const RenderPipelineResource& ) = delete;

        /** @brief 리소스 상대 경로에서 파이프라인 XML을 로드합니다. */
        bool loadFromXmlFile( string_view assetRelativePath );
        /** @brief 파이프라인 XML을 저장합니다. */
        bool saveToXmlFile( string_view assetRelativePath ) const;
        /** @brief 파이프라인 XML을 비동기로 로드합니다. */
        TaskHandle loadFromXmlFileAsync( string_view assetRelativePath );

        /** @brief 파이프라인 디스크립터를 반환합니다. */
        const RenderPipelineDesc& getDesc() const { return _desc; }
        RenderPipelineDesc&       getDesc() { return _desc; }

        /**
         * @brief 로드한 파이프라인이 스스로 모순이 없는지 검사하고 문제 수를 돌려줍니다.
         * @details 패스 타입 표기 해석(`_resolvedType` 도 여기서 채운다), 입출력이 선언된 첨부를
         *          가리키는지, 첨부 포맷 표기가 RHIFormat 으로 읽히는지, 컬러 출력 개수가 한계
         *          안인지를 본다. 여기서 못 잡은 불일치는 런타임에 조용히 어긋나거나 GPU 를
         *          죽인다 — 실제로 그런 적이 있다(`ae7fb078`).
         * @return 발견한 문제 수 (0 이면 정상). 로드는 막지 않고 로그만 남긴다.
         */
        uint32 validate( string_view sourcePath );

        const vector<RenderGraphPassDesc>&  getGraphPass() const { return _desc._listPass; }
        const vector<RenderPassAttachment>& getAttachments() const { return _desc._listAttachment; }

    private:
        /** @brief TaskArgs: this, path string. */
        static void loadFromXmlFileAsyncJob( const TaskArgs& args );

    private:
        RenderPipelineDesc _desc;
    };
} // namespace sw
