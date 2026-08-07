#pragma once
/**
 * @file RenderPassManager.h
 * @brief Auto-generated documentation header
 */

#include "Core/CoreMinimal.h"

#include "RenderPassResource.h"
namespace sw
{

	class SW_API RenderPassManager
	{

	public:

		/**
		 * @brief initialize 처리를 수행합니다.
		 */
		bool		initialize();
		/**
		 * @brief shutdown 처리를 수행합니다.
		 */
		void		shutdown();

		/**
		 * @brief loadRenderPass 처리를 수행합니다.
		 */
		RenderPassResource* loadRenderPass( const std::string_view assetRelativePath );

		/**
		 * @brief findRenderPass 처리를 수행합니다.
		 */
		RenderPassResource* findRenderPass( hashed_string name );

		/**
		 * @brief clearCache 처리를 수행합니다.
		 */
		void clearCache();

	public:
		RenderPassManager()					  = default;
		~RenderPassManager() 				  = default;
		RenderPassManager( const RenderPassManager& ) = delete;
		RenderPassManager& operator=( const RenderPassManager& ) = delete;

		std::unordered_map<hashed_string, std::unique_ptr<RenderPassResource>> _renderPassMap;
	};
}
