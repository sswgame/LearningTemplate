/**
 * @file RenderPipelineResource.h
 * @brief 프레임 렌더 파이프라인 서술(패스 그래프 + 임시 어태치먼트)입니다.
 *
 * RenderPassResource 와의 차이:
 * - RenderPass = 어태치먼트 묶음 · RHI 바인드 타깃(포맷, 클리어)
 * - Pipeline   = 프레임 그래프 + 패스별 PSO(셰이더, 엔트리, 블렌드 · 깊이, 퍼뮤테이션)
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
    /// @brief RenderPipeline XML 의 서술(패스 + 어태치먼트)입니다.
    REFLECT()
    struct RenderPipelineDesc
    {
        REFLECT_BODY();
        PROPERTY()
        string _name = "ForwardPipeline";

        /** @brief 선택 힌트입니다. Forward / Deferred / Custom */
        PROPERTY()
        string _shadingModel = "Forward";

        /** @brief 그래프가 쓰는 임시 · 논리 어태치먼트입니다. */
        PROPERTY()
        vector<RenderPassAttachment> _listAttachment;

        /** @brief 그래프 노드(이름 · 타입 · 입력 · 출력)입니다. */
        PROPERTY()
        vector<RenderGraphPassDesc> _listPass;

        /**
         * @brief RHI 템플릿으로 쓰는 RenderPass XML 경로입니다(선택).
         * (예: engine/renderpass/defaultrenderpass.xml)
         */
        PROPERTY()
        vector<string> _listRenderPassRef;
    };

    /**
     * @class RenderPipelineResource
     * @brief RenderPipelineDesc XML 을 읽고 쓰며 FrameRenderer 에 그래프 패스를 제공합니다.
     */
    class SW_API RenderPipelineResource
    {
    public:
        /** @brief 빈 파이프라인 서술로 만듭니다. */
        RenderPipelineResource() = default;
        /** @brief 가상 소멸자입니다. */
        virtual ~RenderPipelineResource() = default;

        /** @brief 복사를 금지합니다. */
        RenderPipelineResource( const RenderPipelineResource& ) = delete;
        /** @brief 대입을 금지합니다. */
        RenderPipelineResource& operator=( const RenderPipelineResource& ) = delete;

        /** @brief 리소스 상대 경로에서 파이프라인 XML 을 로드합니다. */
        bool loadFromXmlFile( string_view assetRelativePath );
        /** @brief 파이프라인 XML 을 저장합니다. */
        bool saveToXmlFile( string_view assetRelativePath ) const;
        /** @brief 파이프라인 XML 을 비동기로 로드합니다. */
        TaskHandle loadFromXmlFileAsync( string_view assetRelativePath );

        /** @brief 파이프라인 디스크립터를 반환합니다. */
        const RenderPipelineDesc& getDesc() const { return _desc; }
        RenderPipelineDesc&       getDesc() { return _desc; }

        /**
         * @brief 로드한 파이프라인이 스스로 모순이 없는지 검사하고 문제 수를 반환합니다.
         * @details 패스 타입 표기 해석(`_resolvedType` 도 여기서 채웁니다), 입출력이 선언된 첨부를
         *          가리키는지, 첨부 포맷 표기가 RHIFormat 으로 읽히는지, 뎁스 첨부가 실재하는 뎁스 포맷인지,
         *          컬러 출력 개수가 한계 안인지, 풀스크린 패스의 입력이 타입의 계약(`RenderPassInputContract`)과
         *          맞는지를 봅니다. 여기서 못 잡은 불일치는 런타임에 조용히 어긋나거나 GPU 를 죽입니다.
         *          실제로 그런 적이 있습니다(`ae7fb078`).
         * @return 발견한 문제 수 (0 이면 정상). 로드는 막지 않고 로그만 남깁니다.
         */
        uint32 validate( string_view sourcePath );

        const vector<RenderGraphPassDesc>& getGraphPass() const { return _desc._listPass; }

    private:
        /** @brief 비동기 로드 태스크 본문입니다. TaskArgs 는 this · 경로 문자열입니다. */
        static void loadFromXmlFileAsyncJob( const TaskArgs& args );

    private:
        RenderPipelineDesc _desc;
    };
} // namespace sw
