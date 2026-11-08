/**
 * @file RenderPipelineAsset.h
 * @brief 프레임 렌더 파이프라인 디스크립터(패스 그래프 + 임시 어태치먼트)입니다.
 *
 * RenderPassAsset 와의 차이:
 * - RenderPass = 여러 파이프라인이 함께 쓰는 첨부 묶음(이름, 포맷, 클리어)
 * - Pipeline   = 프레임 그래프 + 패스별 PSO(셰이더, 진입점, 블렌드 · 깊이 · 컬, 퍼뮤테이션)
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/Foundation.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Renderer/Pipeline/RenderGraphPassDesc.h"

namespace sw
{
    /// @brief RenderPipeline XML 의 디스크립터(패스 + 어태치먼트)입니다.
    REFLECT()
    struct RenderPipelineDesc
    {
        REFLECT_BODY();
        PROPERTY()
        string _name = "ForwardPipeline";

        /** @brief 셰이딩 방식입니다. Forward / Deferred / Custom. 패스 구성과 맞아야 합니다(Deferred 는 GBuffer · Lighting 패스가 있고, Forward 는 GBuffer 패스가 없다 — 검증). */
        PROPERTY()
        string _shadingModel = "Forward";

        /** @brief 그래프가 쓰는 임시 어태치먼트와 논리 어태치먼트입니다. */
        PROPERTY()
        vector<RenderPassAttachment> _listAttachment;

        /** @brief 뷰 그래프 노드(이름, 타입, 입력, 출력)입니다. 뷰(주 시점 · 추가 뷰)마다 다시 돈다. 타입은 `isViewPassType` 이다. */
        PROPERTY()
        vector<RenderGraphPassDesc> _listPass;

        /**
         * @brief 프레임 그래프의 자원(서브시스템의 버퍼 · 히스토리)입니다. 프레임 패스의 입출력은 이 이름이다(첨부가 아니다).
         * @details `_listRenderPassRef` 의 자원이 읽을 때 앞에 합쳐진다. 이름은 첨부와 겹칠 수 없다(검증).
         */
        PROPERTY( SkipIfEmpty )
        vector<RenderGraphResourceDesc> _listResource;

        /**
         * @brief 프레임 그래프 노드입니다. 프레임에 한 번, 뷰 그래프보다 먼저 돌며 모든 뷰의 몫을 기록한다(컬링 · 라이트 그리드 · 깊이 히스토리 · 스키닝 등 — `isFramePassType`).
         * @details 순서는 `_listResource` 이름의 읽기 · 쓰기가 정한다(뷰 그래프와 같은 규칙). 선언하지 않은 종류는 PSO 도 없고 돌지 않는다.
         *          `_listRenderPassRef` 의 프레임 패스가 읽을 때 앞에 합쳐진다(여러 파이프라인이 `engine/renderpass/framecomputerenderpass.xml` 한 벌을 쓴다).
         */
        PROPERTY( SkipIfEmpty )
        vector<RenderGraphPassDesc> _listFramePass;

        /**
         * @brief 첨부 · 프레임 자원 · 프레임 패스를 가져올 RenderPass XML 경로입니다(선택, 예: engine/renderpass/defaultrenderpass.xml).
         * @details 읽을 때 적은 순서대로 그 파일의 첨부 · 자원 · 프레임 패스를 이 파이프라인 목록 앞에 합친다(`RenderPipelineAsset::mergeRenderPassRefs`) — 렌더러는 합친 목록만 본다.
         *          읽지 못하면 로드 실패이고, 이름이 겹치면 검증 오류다.
         */
        PROPERTY( SkipIfEmpty, AssetPath )
        vector<string> _listRenderPassRef;

        /**
         * @brief 모듈 확장이 패스 프로그램(`ExtensionProgramDesc::_shaderPath`)으로 쓸 수 있는 셰이더 경로입니다(ExtensionRenderPass).
         * @details 모듈이 런타임에 고르는 셰이더는 쿠커가 미리 구워 두어야 하므로(Shipping 은 PSO 를 컴파일하지 못한다) 데이터로 선언한다. 쿠커는 각 경로의 VS · PS 를 define 없이
         *          굽고, 런타임은 여기 없는 경로를 요청한 모듈 패스를 오류로 막는다.
         */
        PROPERTY( SkipIfEmpty, AssetPath )
        vector<string> _listExtensionShader;
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
        /** @brief 파이프라인 디스크립터를 반환합니다. */
        const RenderPipelineDesc& getDesc() const { return _desc; }
        RenderPipelineDesc&       getDesc() { return _desc; }

        /**
         * @brief 로드한 파이프라인이 스스로 모순이 없는지 검사하고 문제 수를 반환합니다.
         * @details 셰이딩 방식과 패스 구성, 패스의 렌더 상태 조합, 패스의 첨부 해석, 입출력이 선언된 첨부를
         *          가리키는지, 첨부 포맷 표기가 RHIFormat 으로 읽히는지, 뎁스 첨부가 실재하는 뎁스 포맷인지,
         *          컬러 출력 개수가 한계 안인지, 지오메트리 패스에 컬러 출력이 있는지, 풀스크린 패스의 입력이 타입의 계약(`RenderPassInputSignature`)과
         *          맞는지를 봅니다. 여기서 못 잡은 불일치는 런타임에 조용히 어긋나거나 GPU 를 죽입니다.
         * @return 발견한 문제 수 (0 이면 정상). 파일에서 읽을 때(`loadFromXMLFile` · `loadFromDiskFile`)는 하나라도 있으면 로드가 실패한다 —
         *         그래서 렌더러는 선언하지 않은 첨부를 만나지 않는다(코드가 이름 · 포맷을 짐작하지 않는다).
         */
        [[nodiscard]] uint32 validate( string_view sourcePath );

        const vector<RenderGraphPassDesc>& getGraphPass() const { return _desc._listPass; }
        /** @brief 프레임 그래프의 패스입니다(`RenderPipelineDesc::_listFramePass`). */
        const vector<RenderGraphPassDesc>& getFramePass() const { return _desc._listFramePass; }
        /** @brief 프레임 그래프의 자원입니다(`RenderPipelineDesc::_listResource`). */
        const vector<RenderGraphResourceDesc>& getResources() const { return _desc._listResource; }
        /**
         * @brief 이 파이프라인이 프레임 패스 @p type 을 선언했는지입니다. 선언하지 않은 종류의 일(컬링이 채울 간접 인자 · GPU 라이트 그리드 · 스킨 결과 · 볼륨 안개 등)은
         *        준비 단계부터 하지 않는다 — 채우는 패스가 없는 버퍼를 읽게 두지 않는다.
         */
        bool hasFramePass( RenderPassType type ) const;

    private:
        /** @brief `_listRenderPassRef` 의 첨부 · 자원 · 프레임 패스를 이 파이프라인 목록 앞에 합칩니다. 렌더 패스 파일을 읽지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool mergeRenderPassRefs( string_view sourcePath );
        /** @brief 읽기 입구 둘(리소스 id · 디스크 파일)이 같이 쓰는 끝 처리입니다(첨부 합치기 · 검사 · 로그). 문제가 있으면 false 입니다. */
        [[nodiscard]] bool finishLoad( string_view sourcePath );

    private:
        RenderPipelineDesc _desc;
    };
} // namespace sw
