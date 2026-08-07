#pragma once
/**
 * @file LiveShaderManager.h
 * @brief 파일 감시 기반 라이브 셰이더 리로드
 */

#include "Core/CoreMinimal.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Graphics/Shader/ShaderCompiler.h"
#include "Core/Graphics/Shader/ShaderCache.h"

namespace sw
{
	using ShaderRecompiledDelegate = Delegate<void( const std::string&, const ShaderCompileResult& )>;

	class SW_API ShaderIncludeResolver
	{
	public:
		/** @brief 셰이더 소스에서 #include 경로 목록을 파싱합니다. */
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

		/** @brief 셰이더를 감시 목록에 추가하고, 리컴파일 시 콜백을 등록합니다. */
		void watchShader( const ShaderCompileDesc& desc, const ShaderRecompiledDelegate& onRecompiled = {} );

		/** @brief 변경 대기열을 처리하고 필요 시 리컴파일합니다. */
		void update();

		/** @brief 감시·콜백·대기열을 정리하고 종료합니다. */
		void shutdown();

		/**
		 * @brief 모든 셰이더의 리로드를 수동으로 트리거합니다.
		 */
		void triggerReloadAll();

		/** @brief 파일 감시 매칭 경로를 대기열에 넣습니다 (본 셰이더 또는 include 의존). */
		void notifyFileChanged( const std::string& path );

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
} // namespace sw
