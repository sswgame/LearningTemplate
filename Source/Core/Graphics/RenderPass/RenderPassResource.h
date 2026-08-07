#pragma once
/**
 * @file RenderPassResource.h
 * @brief Auto-generated documentation header
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

	REFLECT()
	struct RenderPassDesc
	{
		PROPERTY()
		std::string _name = "DefaultMainPass";

		PROPERTY()
		std::vector<RenderPassAttachment> _attachments;
	};

	class SW_API RenderPassResource
	{
	public:
		RenderPassResource()  = default;
		virtual ~RenderPassResource() = default;

		RenderPassResource( const RenderPassResource& )			   = delete;
		RenderPassResource& operator=( const RenderPassResource& ) = delete;

		/**
		 * @brief loadFromXmlFile 처리를 수행합니다.
		 */
		bool loadFromXmlFile( const std::string& assetRelativePath );

		/**
		 * @brief saveToXmlFile 처리를 수행합니다.
		 */
		bool saveToXmlFile( const std::string& assetRelativePath ) const;

		/**
		 * @brief loadFromXmlFileAsync 처리를 수행합니다.
		 */
		TaskHandle loadFromXmlFileAsync( const std::string& assetRelativePath );

		const RenderPassDesc& getDesc() const { return _desc; }
		RenderPassDesc&		  getDesc() { return _desc; }

	private:
		RenderPassDesc _desc;
	};
}
