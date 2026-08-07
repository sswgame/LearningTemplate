/**
 * @file CoreServices.cpp
 * @brief Core.dll 전역 서비스 포인터 테이블 (헤더 inline static 금지 — DLL 경계 공유)
 */
#include "pch.h"
#include "Core/Common/CoreServices.h"
#include "Core/Utility/CommandLine/CommandLineManager.h"
#include "Core/Utility/GlobalVariable/GlobalVariableManager.h"
#include "Core/Utility/Task/TaskManager.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Object/ComponentManager.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	namespace
	{
		CoreServices s_services{};
	} // namespace

	namespace core
	{
		void bindCoreServices( const CoreServices& services )
		{
			s_services = services;
		}

		void unbindCoreServices()
		{
			s_services = {};
		}

		CommandLineManager& getCommandLineManager()
		{
			SW_LOG_ASSERT( s_services.commandLineManager != nullptr, "CommandLineManager is not bound" );
			return *s_services.commandLineManager;
		}

		GlobalVariableManager& getGlobalVariableManager()
		{
			SW_LOG_ASSERT( s_services.globalVariableManager != nullptr, "GlobalVariableManager is not bound" );
			return *s_services.globalVariableManager;
		}

		TaskManager& getTaskManager()
		{
			SW_LOG_ASSERT( s_services.taskManager != nullptr, "TaskManager is not bound" );
			return *s_services.taskManager;
		}

		TypeRegistry& getTypeRegistry()
		{
			SW_LOG_ASSERT( s_services.typeRegistry != nullptr, "TypeRegistry is not bound" );
			return *s_services.typeRegistry;
		}

		ComponentManager& getComponentManager()
		{
			SW_LOG_ASSERT( s_services.componentManager != nullptr, "ComponentManager is not bound" );
			return *s_services.componentManager;
		}

		SceneManager& getSceneManager()
		{
			SW_LOG_ASSERT( s_services.sceneManager != nullptr, "SceneManager is not bound" );
			return *s_services.sceneManager;
		}
	} // namespace Core
} // namespace sw
