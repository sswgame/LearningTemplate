#pragma once

#include "Core/Common/CommonHeaders.h"
#include "Core/Game/Scene/Scene.h"

namespace sw
{
	/**
	 * @class SceneManager
	 * @brief 로드된 씬들을 관리하고, Active Scene을 추적합니다.
	 */
	class SW_API SceneManager
	{
	public:
		SceneManager()	= default;
		~SceneManager() = default;

		SceneManager( const SceneManager& )			   = delete;
		SceneManager& operator=( const SceneManager& ) = delete;

		bool initialize();
		void shutdown();

		/** @brief 새로운 씬을 생성하고 활성 씬으로 지정합니다. */
		Scene* createScene( const std::string& name );

		/** @brief 현재 활성화된(주요) 씬 반환 */
		Scene* getActiveScene() const { return _activeScene; }

		/** @brief 로드된 모든 씬 업데이트 */
		void update( float32 deltaTime );

		/** @brief 로드된 씬 모두 반환 */
		const std::vector<std::unique_ptr<Scene>>& getLoadedScenes() const { return _loadedScenes; }

	private:
		std::vector<std::unique_ptr<Scene>> _loadedScenes;
		Scene*								_activeScene = nullptr;
	};
}
