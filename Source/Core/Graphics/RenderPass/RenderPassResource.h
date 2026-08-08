#pragma once
/**
 * @file RenderPassResource.h
 * @brief 렌더 패스용 리소스 핸들/디스크립터
 */

#include "Core/Common/Common.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Utility/Task/TaskManager.h"
namespace sw
{

	REFLECT()
	struct RenderPassAttachment
	{
		PROPERTY()
		std::string _name = "ColorAttachment0";

		PROPERTY()
		std::string _format = "R8G8B8A8_UNORM";

		PROPERTY()
		float32 _clearColor[4] = { 0.1f, 0.2f, 0.3f, 1.0f };

		PROPERTY()
		bool _bClear = true;
	};

	/** @brief Single pass node inside a pipeline / render-graph descriptor */
	REFLECT()
	struct RenderGraphPassDesc
	{
		PROPERTY()
		std::string _name = "Pass";

		PROPERTY()
		std::string _type = "Opaque";

		PROPERTY()
		std::vector<std::string> _inputs;

		PROPERTY()
		std::vector<std::string> _outputs;
	};

	/** @brief Optional graph-level description (passes + resources) */
	REFLECT()
	struct RenderGraphDesc
	{
		PROPERTY()
		std::string _name = "DefaultGraph";

		PROPERTY()
		std::vector<RenderGraphPassDesc> _passes;
	};

	REFLECT()
	struct RenderPassDesc
	{
		PROPERTY()
		std::string _name = "DefaultMainPass";

		PROPERTY()
		std::vector<RenderPassAttachment> _attachments;

		PROPERTY()
		std::vector<RenderGraphPassDesc> _passes;

		PROPERTY()
		RenderGraphDesc _graph;
	};

	class SW_API RenderPassResource
	{
	public:
		RenderPassResource()		  = default;
		virtual ~RenderPassResource() = default;

		RenderPassResource( const RenderPassResource& )			   = delete;
		RenderPassResource& operator=( const RenderPassResource& ) = delete;

		/** @brief XML 파일에서 렌더 패스 디스크립터를 로드합니다. */
		bool loadFromXmlFile( const std::string& assetRelativePath );

		/** @brief 렌더 패스 디스크립터를 XML 파일로 저장합니다. */
		bool saveToXmlFile( const std::string& assetRelativePath ) const;

		/** @brief XML 로드를 비동기 작업으로 예약합니다. */
		TaskHandle loadFromXmlFileAsync( const std::string& assetRelativePath );

		const RenderPassDesc& getDesc() const { return _desc; }
		RenderPassDesc&		  getDesc() { return _desc; }

		/** @brief Effective graph passes (top-level _passes, else _graph._passes). */
		const std::vector<RenderGraphPassDesc>& getGraphPasses() const;

	private:
		RenderPassDesc _desc;
	};
} // namespace sw
