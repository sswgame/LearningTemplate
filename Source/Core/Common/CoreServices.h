#pragma once

/**
 * @file CoreServices.h
 * @brief App이 소유한 코어 매니저 포인터를 Core.dll에 바인딩하는 서비스 테이블
 */

#include "Core/Common/CommonDefines.h"

namespace sw
{
	class CommandLineManager;
	class GlobalVariableManager;
	class TaskManager;
	class TypeRegistry;
	class ComponentManager;
	class SceneManager;

	/** @brief App 소유 코어 매니저 포인터 묶음 */
	struct CoreServices
	{
		CommandLineManager*	   commandLineManager	 = nullptr;
		GlobalVariableManager* globalVariableManager = nullptr;
		TaskManager*		   taskManager			 = nullptr;
		TypeRegistry*		   typeRegistry			 = nullptr;
		ComponentManager*	   componentManager		 = nullptr;
		SceneManager*		   sceneManager			 = nullptr;
	};

	/** @brief Core.dll 전역 조회가 사용할 매니저 포인터를 바인딩합니다. */
	SW_API void bindCoreServices( const CoreServices& services );
	/** @brief 바인딩을 해제합니다(앱 종료 시). */
	SW_API void unbindCoreServices();

	/** @brief 바인딩된 CommandLineManager를 반환합니다. */
	SW_API CommandLineManager& getCommandLineManager();
	/** @brief 바인딩된 GlobalVariableManager를 반환합니다. */
	SW_API GlobalVariableManager& getGlobalVariableManager();
	/** @brief 바인딩된 TaskManager를 반환합니다. */
	SW_API TaskManager& getTaskManager();
	/** @brief 바인딩된 TypeRegistry를 반환합니다. */
	SW_API TypeRegistry& getTypeRegistry();
	/** @brief 바인딩된 ComponentManager를 반환합니다. */
	SW_API ComponentManager& getComponentManager();
	/** @brief 바인딩된 SceneManager를 반환합니다. */
	SW_API SceneManager& getSceneManager();
} // namespace sw
