#pragma once
/**
 * @file RHIBackendRegistry.h
 * @brief Pluggable RHI backend factory registry (static link or MODULE DLL via SW_RHI_AS_MODULES)
 */

#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Graphics/RHI/RHICapabilities.h"
#include "Core/Utility/Delegate/Delegate.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{
	using RHIDeviceFactoryDelegate = Delegate<std::unique_ptr<IRHIDevice>()>;

	struct RHIBackendEntry
	{
		RHIBackend				 _backend{};
		RHIDeviceFactoryDelegate _factory;
		RHICapabilities			 _caps{};
	};

	class SW_API RHIBackendRegistry
	{
	public:
		static RHIBackendRegistry& get();

		void				   registerBackend( RHIBackend backend, const RHIDeviceFactoryDelegate& factory, const RHICapabilities& caps );
		const RHIBackendEntry* find( RHIBackend backend ) const;

		std::unique_ptr<IRHIDevice> create( RHIBackend backend ) const;

		/** @brief Optional MODULE load: looks for DLL exporting createRHIDevice. */
		bool tryLoadModule( RHIBackend backend, const std::string& modulePath );

		/** @brief Unloads MODULE DLLs previously loaded via tryLoadModule. */
		void unloadModules();

	private:
		RHIBackendRegistry() = default;
		~RHIBackendRegistry();

		RHIBackendRegistry( const RHIBackendRegistry& )			   = delete;
		RHIBackendRegistry& operator=( const RHIBackendRegistry& ) = delete;

		std::vector<RHIBackendEntry> _entries;
		std::vector<void*>			 _loadedModuleHandles;
	};

	/** @brief C ABI for RHI MODULE DLLs — export as: extern "C" SW_MODULE_API IRHIDevice* createRHIDevice(); */
	using PFN_CreateRHIDevice = IRHIDevice* (*)();
} // namespace sw
