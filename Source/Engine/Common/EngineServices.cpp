#include "pch.h"

#include "Engine/Common/EngineServices.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/FrameArenaAllocator.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Graphics/Shader/ShaderCache.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/ResourceManager.h"

#include "RuntimeAPI/Service/ModuleService.h"

SW_LOG_CALLER( "EngineServices" );
namespace sw
{
	namespace
	{

		EngineServices s_services{};

	} // namespace

	namespace engine
	{
		void bindEngineServices( const EngineServices& services )
		{
			s_services = services;
		}

		void unbindEngineServices()
		{
			s_services = {};
		}

		bool areEngineServicesBound()
		{
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed ) \
	if constexpr ( ( required ) != 0 )                                        \
	{                                                                         \
		if ( s_services.member == nullptr )                                   \
			return false;                                                     \
	}
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed ) SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed )
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
			return true;
		}

		void fillModuleServices( ModuleService& outService, bool bGameModuleOnly )
		{
			using namespace sw::internal;
			outService = {};

#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed )                \
	if ( bGameModuleOnly == false || ( ( gameAllowed ) == 1 ) )                              \
	{                                                                                        \
		outService.arrServices[toRawServiceId( ModuleServiceId::Type )] = s_services.member; \
	}
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed ) \
	SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed ) \
	SW_ENGINE_SERVICE( member, Tag, Type, getter, 0, gameAllowed )
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
		}

#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed ) \
	Type& getter()                                                            \
	{                                                                         \
		SW_LOG_ASSERT( s_services.member != nullptr, #Type " is not bound" ); \
		return *s_services.member;                                            \
	}
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed ) \
	const Type& getter()                                                            \
	{                                                                               \
		SW_LOG_ASSERT( s_services.member != nullptr, #Type " is not bound" );       \
		return *s_services.member;                                                  \
	}
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed ) \
	Type* getter()                                                      \
	{                                                                   \
		return s_services.member;                                       \
	}
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
	} // namespace engine
} // namespace sw
