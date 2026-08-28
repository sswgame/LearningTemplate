/**
 * @file LiveShaderManager.h
 * @brief 파일 감시로 셰이더를 다시 컴파일합니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Utility/File/ReloadFileManager.h"

#include <shared_mutex>

namespace sw
{
	using ShaderRecompiledDelegate = Delegate<void( string_view, const ShaderCompileResult& )>;

	/// @brief #include 경로를 리소스에서 해석
	class SW_API ShaderIncludeResolver
	{
	public:
		/** @brief 셰이더 소스에서 #include 경로를 뽑습니다. */
		static vector<string> parseIncludes( string_view shaderSource );
	};

	/// @brief 파일 감시로 셰이더를 다시 컴파일
	class SW_API LiveShaderManager
	{
	public:
		/** @brief 워치와 캐시를 비운 채 시작합니다. */
		LiveShaderManager();
		/** @brief 워치와 캐시를 정리합니다. */
		~LiveShaderManager();

		/** @brief 복사를 금지합니다. */
		LiveShaderManager( const LiveShaderManager& ) = delete;
		/** @brief 대입을 금지합니다. */
		LiveShaderManager& operator=( const LiveShaderManager& ) = delete;
		/** @brief 이동을 금지합니다. */
		LiveShaderManager( LiveShaderManager&& ) = delete;
		/** @brief 이동 대입을 금지합니다. */
		LiveShaderManager& operator=( LiveShaderManager&& ) = delete;

		/** @brief 논리 감시 디렉터리 라벨로 초기화합니다. */
		bool initialize( string_view watchDirectory = "Shaders" );

		/**
		 * @brief .hlsl/.hlsli ReloadFileManager 워치를 이 매니저가 소유합니다.
		 * @note ReloadFileManager::initialize 이후 호출. shutdown / 재연결 시 해제.
		 */
		void attachReloadFileManager( ReloadFileManager& reloadFiles );

		/** @brief 셰이더와 선택 재컴파일 콜백을 등록합니다. */
		void watchShader( const ShaderCompileDesc& desc, const ShaderRecompiledDelegate& onRecompiled = {} );

		/** @brief 대기 중인 리로드 경로를 비우고 다시 컴파일합니다. */
		void update();

		/** @brief 워치/큐를 비우고 파일 워처를 뗍니다. */
		void shutdown();

		/** @brief 감시 중인 셰이더를 모두 리로드 큐에 넣습니다. */
		void triggerReloadAll();

		/** @brief 변경된 경로(셰이더 또는 include)를 큐에 넣습니다. */
		void notifyFileChanged( string_view path );

	private:
		/** @brief ReloadFileManager 워치 콜백. */
		void onWatchedFileChanged( const FileChangeEvent& ev );
		/** @brief ReloadFileManager 워치를 뗍니다. */
		void detachReloadFileManager();

		/// @brief 감시 중인 셰이더 경로와 의존 include
		struct WatchedShaderInfo
		{
			ShaderCompileDesc		 _desc;
			ShaderRecompiledDelegate _onRecompiled;
		};

		mutable std::shared_mutex						 _mutex;
		unordered_map<string, vector<WatchedShaderInfo>> _mapWatchedShader;
		unordered_map<string, vector<string>>			 _mapIncludeDependency;
		vector<string>									 _listPendingReloadPath;
		string											 _watchDirectory;
		ReloadFileManager*								 _pReloadFiles{ nullptr };
		FileWatchHandle									 _fileWatchHandle{};
		bool											 _bInitialized{ false };
	};
} // namespace sw
