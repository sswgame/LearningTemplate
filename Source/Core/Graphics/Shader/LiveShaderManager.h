#pragma once
/**
 * @file LiveShaderManager.h
 * @brief Auto-generated documentation header
 */

#include "Core/CoreMinimal.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Graphics/Shader/ShaderCompiler.h"
#include "Core/Graphics/Shader/ShaderCache.h"

namespace sw
{
	using ShaderRecompiledDelegate = Delegate<void( const std::string& , const ShaderCompileResult&  )>;

	class SW_API ShaderIncludeResolver
	{
	public:

		/**
		 * @brief parseIncludes 처리를 수행합니다.
		 */
		static std::vector<std::string> parseIncludes( const std::string& shaderSource );
	};

	class SW_API LiveShaderManager
	{
	public:
		LiveShaderManager();
		~LiveShaderManager();

		LiveShaderManager( const LiveShaderManager& )			 = delete;
		LiveShaderManager& operator=( const LiveShaderManager& ) = delete;
		LiveShaderManager( LiveShaderManager&& )				 = delete;
		LiveShaderManager& operator=( LiveShaderManager&& )		 = delete;

		/** @brief 셰이더 감시 디렉터리로 매니저를 초기화합니다. */
		bool initialize( const std::string& watchDirectory = "Shaders" );

		/**
		 * @brief watchShader 처리를 수행합니다.
		 */
		void watchShader( const ShaderCompileDesc& desc, const ShaderRecompiledDelegate& onRecompiled = {} );

		/**
		 * @brief update 처리를 수행합니다.
		 */
		void update();

		/**
		 * @brief shutdown 처리를 수행합니다.
		 */
		void shutdown();

		/**
		 * @brief 모든 셰이더의 리로드를 수동으로 트리거합니다.
		 */
		void triggerReloadAll();

	private:
		struct WatchedShaderInfo
		{
			ShaderCompileDesc		 _desc;
			ShaderRecompiledDelegate _onRecompiled;
		};

		std::unordered_map<std::string, std::vector<WatchedShaderInfo>> _watchedShaders;
		std::unordered_map<std::string, std::vector<std::string>>		_includeDependencies;
		std::vector<std::string>										_pendingReloadPaths;
		std::string														_watchDirectory;
		bool															_bInitialized = false;
	};
}
