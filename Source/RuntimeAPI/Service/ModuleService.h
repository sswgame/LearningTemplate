/**
 * @file ModuleService.h
 * @brief App ↔ 모듈 공통 서비스 로케이터. 게임 모듈이 요청할 수 있는 id만 여기에 둡니다.
 */
#pragma once
#include "Core/Common/Macros.h"

namespace sw
{
	class LocalizationManager;
	class EventDispatcher;
	class GlobalVariableManager;
	class IAudioSystem;
	class TypeRegistry;
	class InputManager;
	class SceneManager;
	class ResourceManager;
	class DebugDrawQueue;
	struct DebugOverlayState;
	class CompressionCodecRegistry;
	class ShaderCache;
	struct GameData;
	class MonsterDataCatalog;
	class SpeciesCatalog;
	class ComponentDefaults;

	enum class ModuleServiceId : uint32
	{
		LocalizationManager = 0,
		EventDispatcher,
		GlobalVariableManager,
		AudioSystem,
		TypeRegistry,
		InputManager,
		SceneManager,
		ResourceManager,
		DebugDrawQueue,
		DebugOverlayState,
		CompressionCodecRegistry,
		ShaderCache,
		GameData,
		MonsterDataCatalog,
		SpeciesCatalog,
		ComponentDefaults,
		Count
	};

	inline constexpr uint32 kModuleServiceCount = static_cast<uint32>( ModuleServiceId::Count );

	inline constexpr uint32 toRawServiceId( ModuleServiceId id )
	{
		return static_cast<uint32>( id );
	}

	using ModuleServiceFn = void* (*)( uint32 );

	struct ModuleService
	{
		ModuleServiceFn getService{ nullptr };
	};

	template <typename T>
	struct ModuleServiceTraits;

#define SW_DECLARE_MODULE_SERVICE( Type, Id )     \
	template <>                                   \
	struct ModuleServiceTraits<Type>              \
	{                                             \
		static constexpr ModuleServiceId id = Id; \
	}

	SW_DECLARE_MODULE_SERVICE( LocalizationManager, ModuleServiceId::LocalizationManager );
	SW_DECLARE_MODULE_SERVICE( EventDispatcher, ModuleServiceId::EventDispatcher );
	SW_DECLARE_MODULE_SERVICE( GlobalVariableManager, ModuleServiceId::GlobalVariableManager );
	SW_DECLARE_MODULE_SERVICE( IAudioSystem, ModuleServiceId::AudioSystem );
	SW_DECLARE_MODULE_SERVICE( TypeRegistry, ModuleServiceId::TypeRegistry );
	SW_DECLARE_MODULE_SERVICE( InputManager, ModuleServiceId::InputManager );
	SW_DECLARE_MODULE_SERVICE( SceneManager, ModuleServiceId::SceneManager );
	SW_DECLARE_MODULE_SERVICE( ResourceManager, ModuleServiceId::ResourceManager );
	SW_DECLARE_MODULE_SERVICE( DebugDrawQueue, ModuleServiceId::DebugDrawQueue );
	SW_DECLARE_MODULE_SERVICE( DebugOverlayState, ModuleServiceId::DebugOverlayState );
	SW_DECLARE_MODULE_SERVICE( CompressionCodecRegistry, ModuleServiceId::CompressionCodecRegistry );
	SW_DECLARE_MODULE_SERVICE( ShaderCache, ModuleServiceId::ShaderCache );
	SW_DECLARE_MODULE_SERVICE( GameData, ModuleServiceId::GameData );
	SW_DECLARE_MODULE_SERVICE( MonsterDataCatalog, ModuleServiceId::MonsterDataCatalog );
	SW_DECLARE_MODULE_SERVICE( SpeciesCatalog, ModuleServiceId::SpeciesCatalog );
	SW_DECLARE_MODULE_SERVICE( ComponentDefaults, ModuleServiceId::ComponentDefaults );
} // namespace sw
