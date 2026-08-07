/**
 * @file RHIBackendRegistry.cpp
 */
#include "pch.h"
#include "RHIBackendRegistry.h"
#if !defined( SW_RHI_AS_MODULES )
	#if defined( SW_PLATFORM_WINDOWS )
		#include "DX11/D3D11RHIDevice.h"
		#include "DX12/D3D12RHIDevice.h"
	#endif
	#include "Vulkan/VulkanRHIDevice.h"
	#include "GL/OpenGLRHIDevice.h"
#endif
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"


namespace sw
{
	namespace
	{
#if !defined( SW_RHI_AS_MODULES )
	#if defined( SW_PLATFORM_WINDOWS )
		std::unique_ptr<IRHIDevice> createD3D11Device()
		{
			return std::make_unique<D3D11RHIDevice>();
		}

		std::unique_ptr<IRHIDevice> createD3D12Device()
		{
			return std::make_unique<D3D12RHIDevice>();
		}
	#endif

		std::unique_ptr<IRHIDevice> createVulkanDevice()
		{
			return std::make_unique<VulkanRHIDevice>();
		}

		std::unique_ptr<IRHIDevice> createOpenGLDevice()
		{
			return std::make_unique<OpenGLRHIDevice>();
		}
#endif

#if defined( SW_RHI_AS_MODULES )
		bool tryLoadBackendModule( RHIBackend backend, const char* moduleBaseName )
		{
			RHIBackendRegistry& reg		  = RHIBackendRegistry::get();
			const std::string	dllName	  = FileUtil::formatSharedLibraryName( moduleBaseName );
			const std::string	execDir	  = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );
			const std::string	besideExe = execDir.empty() ? dllName : ( execDir + "/" + dllName );

			if ( reg.tryLoadModule( backend, besideExe ) )
				return true;
			return reg.tryLoadModule( backend, dllName );
		}

		/** @brief Load RHI MODULE plugins after Core is fully initialized (not during CRT static init / loader lock). */
		void ensureRhiModulesLoaded()
		{
			static std::once_flag s_once;
			std::call_once( s_once,
							[]()
			{
	#if defined( SW_PLATFORM_WINDOWS )
				if ( tryLoadBackendModule( RHIBackend::DirectX11, "RHI_DX11" ) == false )
					SW_LOG_WARNING( "[RHIBackendRegistry] Failed to load RHI_DX11 module" );
				if ( tryLoadBackendModule( RHIBackend::DirectX12, "RHI_DX12" ) == false )
					SW_LOG_WARNING( "[RHIBackendRegistry] Failed to load RHI_DX12 module" );
	#endif
				if ( tryLoadBackendModule( RHIBackend::OpenGL, "RHI_GL" ) == false )
					SW_LOG_WARNING( "[RHIBackendRegistry] Failed to load RHI_GL module" );
				if ( tryLoadBackendModule( RHIBackend::Vulkan, "RHI_Vulkan" ) == false )
					SW_LOG_WARNING( "[RHIBackendRegistry] Failed to load RHI_Vulkan module" );
			} );
		}
#endif

		struct StaticBackendRegistration
		{
			StaticBackendRegistration()
			{
#if !defined( SW_RHI_AS_MODULES )
				auto& reg = RHIBackendRegistry::get();
	#if defined( SW_PLATFORM_WINDOWS )
				reg.registerBackend( RHIBackend::DirectX11, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, createD3D11Device ), RHIAvailability::query( RHIBackend::DirectX11 ) );
				reg.registerBackend( RHIBackend::DirectX12, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, createD3D12Device ), RHIAvailability::query( RHIBackend::DirectX12 ) );
	#endif
				reg.registerBackend( RHIBackend::Vulkan, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, createVulkanDevice ), RHIAvailability::query( RHIBackend::Vulkan ) );
				reg.registerBackend( RHIBackend::OpenGL, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, createOpenGLDevice ), RHIAvailability::query( RHIBackend::OpenGL ) );
#endif
			}
		};

		StaticBackendRegistration s_staticBackends{};
	} // namespace

	RHIBackendRegistry& RHIBackendRegistry::get()
	{
		static RHIBackendRegistry s_instance;
		return s_instance;
	}

	RHIBackendRegistry::~RHIBackendRegistry()
	{
		unloadModules();
	}

	void RHIBackendRegistry::unloadModules()
	{
		if ( _loadedModules.empty() )
			return;

		// Clear factories before FreeLibrary so create() cannot dispatch into unloaded code.
		// Devices from these factories must already be destroyed (see RHI::shutdown).
		for ( const LoadedModule& module : _loadedModules )
		{
			for ( RHIBackendEntry& entry : _entries )
			{
				if ( entry._backend == module._backend )
				{
					entry._factory = {};
					break;
				}
			}
		}

		for ( const LoadedModule& module : _loadedModules )
		{
			if ( module._handle != nullptr )
				FileUtil::freeDynamicLibrary( module._handle );
		}
		_loadedModules.clear();
	}

	void RHIBackendRegistry::registerBackend( RHIBackend backend, const RHIDeviceFactoryDelegate& factory, const RHICapabilities& caps )
	{
		for ( RHIBackendEntry& entry : _entries )
		{
			if ( entry._backend == backend )
			{
				entry._factory = factory;
				entry._caps	   = caps;
				return;
			}
		}
		_entries.push_back( RHIBackendEntry{ backend, factory, caps } );
	}

	const RHIBackendEntry* RHIBackendRegistry::find( RHIBackend backend ) const
	{
		for ( const RHIBackendEntry& entry : _entries )
		{
			if ( entry._backend == backend )
				return &entry;
		}
		return nullptr;
	}

	std::unique_ptr<IRHIDevice> RHIBackendRegistry::create( RHIBackend backend ) const
	{
#if defined( SW_RHI_AS_MODULES )
		ensureRhiModulesLoaded();
#endif

		const RHIBackendEntry* entry = find( backend );
		if ( entry == nullptr || entry->_factory.isBound() == false )
		{
			SW_LOG_ERROR( "[RHIBackendRegistry] No factory registered for backend %#", static_cast<int32>( backend ) );
			return nullptr;
		}
		if ( RHIAvailability::isAvailable( backend ) == false )
		{
			SW_LOG_ERROR( "[RHIBackendRegistry] Backend %# is not available on this platform", static_cast<int32>( backend ) );
			return nullptr;
		}
		return entry->_factory();
	}

	bool RHIBackendRegistry::tryLoadModule( RHIBackend backend, const std::string& modulePath )
	{
		void* handle = FileUtil::loadDynamicLibrary( modulePath );
		if ( handle == nullptr )
			return false;

		auto* pfn = reinterpret_cast<PFN_CreateRHIDevice>( FileUtil::getDynamicSymbol( handle, "createRHIDevice" ) );
		if ( pfn == nullptr )
		{
			FileUtil::freeDynamicLibrary( handle );
			return false;
		}

		RHIDeviceFactoryDelegate factory = SW_DELEGATE_LAMBDA( RHIDeviceFactoryDelegate,
															   [pfn]() -> std::unique_ptr<IRHIDevice>
		{
			return std::unique_ptr<IRHIDevice>( pfn() );
		} );

		registerBackend( backend, factory, RHIAvailability::query( backend ) );
		_loadedModules.push_back( LoadedModule{ backend, handle } );

		SW_LOG_INFO( "[RHIBackendRegistry] Loaded module %# for backend %#", modulePath, static_cast<int32>( backend ) );
		return true;
	}
} // namespace sw
