/**
 * @file RenderPassResource.h
 * @brief 렌더 패스용 리소스 핸들/디스크립터
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Common/Common.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
	/// @brief 렌더 패스 어태치먼트 (포맷, 로드/스토어, 클리어)

	REFLECT()
	struct RenderPassAttachment
	{
		PROPERTY()
		string _name = "ColorAttachment0";

		PROPERTY()
		string _format = "R8G8B8A8_UNORM";

		PROPERTY()
		float32 _arrClearColor[4] = { 0.1f, 0.2f, 0.3f, 1.0f };

		PROPERTY()
		bool _bClear{ true };
	};

	/**
	 * @brief Single pass node inside a Render Pipeline graph.
	 * @details Graph wiring (inputs/outputs) + optional PSO recipe (shader, entries, state, permutations).
	 *          RenderPass XML stays attachment-only; these fields belong on pipeline passes.
	 */
	REFLECT()
	struct RenderGraphPassDesc
	{
		PROPERTY()
		string _name = "Pass";

		PROPERTY()
		string _type = "Opaque";

		PROPERTY()
		vector<string> _listInput;

		PROPERTY()
		vector<string> _listOutput;

		/** @brief HLSL path (engine/... or common/...). Empty → FrameRenderer type default. */
		PROPERTY()
		string _shaderPath;

		PROPERTY()
		string _vertexEntryPoint = "VSMain";

		PROPERTY()
		string _pixelEntryPoint = "PSMain";

		PROPERTY()
		string _computeEntryPoint = "CSMain";

		/** @brief Shader macros / permutations as "NAME" or "NAME=VALUE". */
		PROPERTY()
		vector<string> _listPermutation;

		PROPERTY()
		string _cullMode = "Back"; ///< None / Front / Back

		PROPERTY()
		bool _bEnableDepthTest{ true };

		PROPERTY()
		bool _bEnableDepthWrite{ true };

		PROPERTY()
		bool _bEnableBlend{ false };
	};

	/**
	 * @brief Attachment-set descriptor for an RHI render pass template.
	 * Frame composition (pass graph) belongs in RenderPipelineDesc — see RenderPipelineResource.h.
	 */
	REFLECT()
	struct RenderPassDesc
	{
		PROPERTY()
		string _name = "DefaultMainPass";

		PROPERTY()
		vector<RenderPassAttachment> _listAttachment;
	};

	/// @brief RenderPass XML 에셋 (어태치먼트 템플릿)
	class SW_API RenderPassResource
	{
	public:
		/** @brief 빈 렌더패스 디스크립터. */
		RenderPassResource() = default;
		/** @brief 가상 소멸. */
		virtual ~RenderPassResource() = default;

		/** @brief 복사를 금지합니다. */
		RenderPassResource( const RenderPassResource& ) = delete;
		/** @brief 대입을 금지합니다. */
		RenderPassResource& operator=( const RenderPassResource& ) = delete;

		/** @brief XML 파일에서 렌더 패스 디스크립터를 로드합니다. */
		bool loadFromXmlFile( string_view assetRelativePath );

		/** @brief 렌더 패스 디스크립터를 XML 파일로 저장합니다. */
		bool saveToXmlFile( string_view assetRelativePath ) const;

		/** @brief XML 로드를 비동기 작업으로 예약합니다. */
		TaskHandle loadFromXmlFileAsync( string_view assetRelativePath );

		const RenderPassDesc& getDesc() const { return _desc; }
		RenderPassDesc&		  getDesc() { return _desc; }

	private:
		/** @brief TaskArgs: this, path string. */
		static void loadFromXmlFileAsyncJob( const TaskArgs& args );

	private:
		RenderPassDesc _desc;
	};
} // namespace sw
