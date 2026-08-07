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

	struct CoreServices
	{
		CommandLineManager*	   commandLineManager	 = nullptr;
		GlobalVariableManager* globalVariableManager = nullptr;
		TaskManager*		   taskManager			 = nullptr;
		TypeRegistry*		   typeRegistry			 = nullptr;
		ComponentManager*	   componentManager		 = nullptr;
		SceneManager*		   sceneManager			 = nullptr;
	};

	SW_API void bindCoreServices( const CoreServices& services );
	SW_API void unbindCoreServices();

	SW_API CommandLineManager&	  getCommandLineManager();
	SW_API GlobalVariableManager& getGlobalVariableManager();
	SW_API TaskManager&			  getTaskManager();
	SW_API TypeRegistry&		  getTypeRegistry();
	SW_API ComponentManager&	  getComponentManager();
	SW_API SceneManager&		  getSceneManager();
}
