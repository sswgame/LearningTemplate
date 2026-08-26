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
#include "Engine/Graphics/RenderPass/RenderPassResource.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
	/// @brief RenderPipeline XML 서술 (패스 + 어태치먼트)
	REFLECT()
	struct RenderPipelineDesc
	{
		PROPERTY()
		string _name = "ForwardPipeline";

		/** @brief 선택 힌트: Forward / Deferred / Custom */
		PROPERTY()
		string _shadingModel = "Forward";

		/** @brief 그래프가 쓰는 임시/논리 어태치먼트 */
		PROPERTY()
		vector<RenderPassAttachment> _listAttachments;

		/** @brief 그래프 노드 (이름, 타입, 입력, 출력) */
		PROPERTY()
		vector<RenderGraphPassDesc> _listPasses;

		/**
		 * @brief RHI 템플릿으로 쓰는 RenderPass XML 경로 (선택)
		 * (예: renderpass/defaultrenderpass.xml 또는 engine/renderpass/...)
		 */
		PROPERTY()
		vector<string> _listRenderPassRefs;
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
		RenderPipelineDesc&		  getDesc() { return _desc; }

		const vector<RenderGraphPassDesc>&	getGraphPasses() const { return _desc._listPasses; }
		const vector<RenderPassAttachment>& getAttachments() const { return _desc._listAttachments; }

	private:
		RenderPipelineDesc _desc;
	};
} // namespace sw
