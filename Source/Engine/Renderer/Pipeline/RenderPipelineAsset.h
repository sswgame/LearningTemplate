/**
 * @file RenderPipelineAsset.h
 * @brief 프레임 렌더 파이프라인 디스크립터(패스 그래프 + 임시 어태치먼트)입니다.
 *
 * RenderPassAsset 와의 차이:
 * - RenderPass = 어태치먼트 묶음 + RHI 바인드 타깃(포맷, 클리어)
 * - Pipeline   = 프레임 그래프 + 패스별 PSO(셰이더, 엔트리, 블렌드 · 깊이, 퍼뮤테이션)
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Asset/AssetID.h"
#include "Engine/Common/Common.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Renderer/Pipeline/RenderPassAsset.h"

namespace sw
{
    /// @brief RenderPipeline XML 의 디스크립터(패스 + 어태치먼트)입니다.
    REFLECT()
    struct RenderPipelineDesc
    {
        REFLECT_BODY();
        PROPERTY()
        string _name = "ForwardPipeline";

        /** @brief 선택 힌트입니다. Forward / Deferred / Custom */
        PROPERTY()
        string _shadingModel = "Forward";

        /** @brief 그래프가 쓰는 임시 어태치먼트와 논리 어태치먼트입니다. */
        PROPERTY()
        vector<RenderPassAttachment> _listAttachment;

        /** @brief 그래프 노드(이름, 타입, 입력, 출력)입니다. */
        PROPERTY()
        vector<RenderGraphPassDesc> _listPass;

        /**
         * @brief RHI 템플릿으로 쓰는 RenderPass XML 경로입니다(선택).
         * (예: engine/renderpass/defaultrenderpass.xml)
         */
        PROPERTY()
        vector<string> _listRenderPassRef;

        /**
         * @brief 모듈 확장이 패스 프로그램(`ModuleProgramDesc::_shaderPath`)으로 쓸 수 있는 셰이더 경로입니다(ModuleRenderPass).
         * @details 모듈이 런타임에 고르는 셰이더는 쿠커가 미리 구워 두어야 하므로(Shipping 은 PSO 를 컴파일하지 못한다) 데이터로 선언한다. 쿠커는 각 경로의 VS · PS 를 define 없이
         *          굽고, 런타임은 여기 없는 경로를 요청한 모듈 패스를 오류로 막는다.
         */
        PROPERTY( SkipIfEmpty )
        vector<string> _listModuleShader;
    };
} // namespace sw

namespace sw
{
    /**
     * @class RenderPipelineAsset
     * @brief RenderPipelineDesc XML 을 읽고 쓰며 FrameRenderer 에 그래프 패스를 제공합니다.
     */
    class SW_API RenderPipelineAsset
    {
    public:
        /** @brief 빈 파이프라인 서술로 만듭니다. */
        RenderPipelineAsset() = default;
        /** @brief 가상 소멸자입니다. */
        virtual ~RenderPipelineAsset() = default;

        /** @brief 복사를 금지합니다. */
        RenderPipelineAsset( const RenderPipelineAsset& ) = delete;
        /** @brief 대입을 금지합니다. */
        RenderPipelineAsset& operator=( const RenderPipelineAsset& ) = delete;

        /** @brief 리소스 상대 경로에서 파이프라인 XML 을 로드합니다. */
        [[nodiscard]] bool loadFromXMLFile( const AssetID& assetID );
        /**
         * @brief 디스크 파일에서 파이프라인 XML 을 로드합니다. 팩을 보지 않습니다(`ReflectedXMLFile::loadDescFromDiskFile`).
         * @details 소스 트리를 디스크로 훑은 쿠커 · 테스트용입니다 — 리소스 루트 안 절대 경로를 `loadFromXMLFile` 에 넣으면 팩 전용 구성이 거절합니다.
         */
        [[nodiscard]] bool loadFromDiskFile( string_view diskPath );
        /** @brief 파이프라인 XML 을 저장합니다. */
        [[nodiscard]] bool saveToXMLFile( string_view assetRelativePath ) const;
        /** @brief 파이프라인 XML 을 비동기로 로드합니다. */
        TaskHandle loadFromXMLFileAsync( string_view assetRelativePath );

        /** @brief 파이프라인 디스크립터를 반환합니다. */
        const RenderPipelineDesc& getDesc() const { return _desc; }
        RenderPipelineDesc&       getDesc() { return _desc; }

        /**
         * @brief 로드한 파이프라인이 스스로 모순이 없는지 검사하고 문제 수를 반환합니다.
         * @details 패스 타입 표기 해석(`_resolvedType` 도 여기서 채웁니다), 입출력이 선언된 첨부를
         *          가리키는지, 첨부 포맷 표기가 RHIFormat 으로 읽히는지, 뎁스 첨부가 실재하는 뎁스 포맷인지,
         *          컬러 출력 개수가 한계 안인지, 지오메트리 패스에 컬러 출력이 있는지, 풀스크린 패스의 입력이 타입의 계약(`RenderPassInputSignature`)과
         *          맞는지를 봅니다. 여기서 못 잡은 불일치는 런타임에 조용히 어긋나거나 GPU 를 죽입니다.
         * @return 발견한 문제 수 (0 이면 정상). 파일에서 읽을 때(`loadFromXMLFile` · `loadFromDiskFile`)는 하나라도 있으면 로드가 실패한다 —
         *         그래서 렌더러는 선언하지 않은 첨부를 만나지 않는다(코드가 이름 · 포맷을 짐작하지 않는다).
         */
        [[nodiscard]] uint32 validate( string_view sourcePath );

        const vector<RenderGraphPassDesc>& getGraphPass() const { return _desc._listPass; }

    private:
        /** @brief 읽기 입구 둘(리소스 id · 디스크 파일)이 같이 쓰는 끝 처리입니다(검사 · 로그). 검사 문제가 있으면 false 입니다. */
        [[nodiscard]] bool finishLoad( string_view sourcePath );
        /** @brief 비동기 로드 태스크 본문입니다. TaskArgs 는 this 와 경로 문자열입니다. */
        static void loadFromXMLFileAsyncJob( const TaskArgs& args );

    private:
        RenderPipelineDesc _desc;
    };
} // namespace sw
