#pragma once
/**
 * @file RHIBackendRegistry.h
 * @brief Pluggable RHI backend factory registry (static link or MODULE DLL via SW_RHI_AS_MODULES)
 *
 * @note Lifetime: create() devices must be destroyed before unloadModules() / registry teardown.
 *       RHI::shutdown() calls unloadModules after resetting the active device.
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

		/**
		 * @brief Clears MODULE-backed factories, then FreeLibrary.
		 * @warning Destroy all IRHIDevice instances created from MODULE backends first.
		 */
		void unloadModules();

	private:
		RHIBackendRegistry() = default;
		~RHIBackendRegistry();

		RHIBackendRegistry( const RHIBackendRegistry& )			   = delete;
		RHIBackendRegistry& operator=( const RHIBackendRegistry& ) = delete;

		struct LoadedModule
		{
			RHIBackend _backend{};
			void*	   _handle = nullptr;
		};

		std::vector<RHIBackendEntry> _entries;
		std::vector<LoadedModule>	 _loadedModules;
	};

	/** @brief C ABI for RHI MODULE DLLs — export as: extern "C" SW_MODULE_API IRHIDevice* createRHIDevice(); */
	using PFN_CreateRHIDevice = IRHIDevice* (*)();
} // namespace sw
