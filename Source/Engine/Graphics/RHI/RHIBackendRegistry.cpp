#include "pch.h"

#include "Engine/Graphics/RHI/RHIBackendRegistry.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIModuleAbi.h"

#if !defined( SW_RHI_AS_MODULES )
	#if defined( SW_SHIPPING )
		#if defined( SW_RHI_TARGET_DX12 )
			#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
		#elif defined( SW_RHI_TARGET_DX11 )
			#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
		#elif defined( SW_RHI_TARGET_VULKAN )
			#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
		#elif defined( SW_RHI_TARGET_OPENGL )
			#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
		#endif
	#else
		#if defined( SW_PLATFORM_WINDOWS )
			#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
			#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
		#endif
		#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
		#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
	#endif
#endif

namespace sw
{

	namespace
	{

#if !defined( SW_RHI_AS_MODULES )
	#if defined( SW_SHIPPING )
		#if defined( SW_RHI_TARGET_DX12 )
		sw::unique_ptr<IRHIDevice> createD3D12Device()
		{
			return make_unique<D3D12RHIDevice>();
		}
		#elif defined( SW_RHI_TARGET_DX11 )
		sw::unique_ptr<IRHIDevice> createD3D11Device()
		{
			return make_unique<D3D11RHIDevice>();
		}
		#elif defined( SW_RHI_TARGET_VULKAN )
		sw::unique_ptr<IRHIDevice> createVulkanDevice()
		{
			return make_unique<VulkanRHIDevice>();
		}
		#elif defined( SW_RHI_TARGET_OPENGL )
		sw::unique_ptr<IRHIDevice> createOpenGLDevice()
		{
			return make_unique<OpenGLRHIDevice>();
		}
		#endif
	#else
		#if defined( SW_PLATFORM_WINDOWS )
		sw::unique_ptr<IRHIDevice> createD3D11Device()
		{
			return make_unique<D3D11RHIDevice>();
		}
		sw::unique_ptr<IRHIDevice> createD3D12Device()
		{
			return make_unique<D3D12RHIDevice>();
		}
		#endif
		sw::unique_ptr<IRHIDevice> createVulkanDevice()
		{
			return make_unique<VulkanRHIDevice>();
		}
		sw::unique_ptr<IRHIDevice> createOpenGLDevice()
		{
			return make_unique<OpenGLRHIDevice>();
		}
	#endif
#endif

#if defined( SW_RHI_AS_MODULES )
		bool tryLoadBackendModule( RHIBackend backend, const utf8* pModuleBaseName )
		{
			RHIBackendRegistry& reg		  = engine::getRHIBackendRegistry();
			const string		dllName	  = FileUtil::formatSharedLibraryName( pModuleBaseName );
			const string		execDir	  = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );
			const string		besideExe = execDir.empty() ? dllName.c_str() : ( execDir + "/" + dllName );

			if ( reg.tryLoadModule( backend, besideExe ) )
				return true;
			return reg.tryLoadModule( backend, dllName );
		}

		/** @brief Load requested RHI MODULE on-demand when needed (not all at once). */
		bool ensureBackendModuleLoaded( RHIBackend backend )
		{
			RHIBackendRegistry&	   reg	  = engine::getRHIBackendRegistry();
			const RHIBackendEntry* pEntry = reg.findBackend( backend );
			if ( pEntry != nullptr && pEntry->_factory.isBound() )
				return true;

			switch ( backend )
			{
	#if defined( SW_PLATFORM_WINDOWS )
				case RHIBackend::DirectX11:
					return tryLoadBackendModule( RHIBackend::DirectX11, "RHI_DX11" );
				case RHIBackend::DirectX12:
					return tryLoadBackendModule( RHIBackend::DirectX12, "RHI_DX12" );
	#endif
				case RHIBackend::OpenGL:
					return tryLoadBackendModule( RHIBackend::OpenGL, "RHI_GL" );
				case RHIBackend::Vulkan:
					return tryLoadBackendModule( RHIBackend::Vulkan, "RHI_Vulkan" );
				default:
					return false;
			}
		}
#endif

	} // namespace

	RHIBackendRegistry::RHIBackendRegistry()
	{
#if !defined( SW_RHI_AS_MODULES )
	#if defined( SW_SHIPPING )
		#if defined( SW_RHI_TARGET_DX12 )
		registerBackend( RHIBackend::DirectX12, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, createD3D12Device ), RHIAvailability::query( RHIBackend::DirectX12 ) );
		#elif defined( SW_RHI_TARGET_DX11 )
		registerBackend( RHIBackend::DirectX11, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, createD3D11Device ), RHIAvailability::query( RHIBackend::DirectX11 ) );
		#elif defined( SW_RHI_TARGET_VULKAN )
		registerBackend( RHIBackend::Vulkan, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, createVulkanDevice ), RHIAvailability::query( RHIBackend::Vulkan ) );
		#elif defined( SW_RHI_TARGET_OPENGL )
		registerBackend( RHIBackend::OpenGL, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, createOpenGLDevice ), RHIAvailability::query( RHIBackend::OpenGL ) );
		#endif
	#else
		#if defined( SW_PLATFORM_WINDOWS )
		registerBackend( RHIBackend::DirectX11, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, createD3D11Device ), RHIAvailability::query( RHIBackend::DirectX11 ) );
		registerBackend( RHIBackend::DirectX12, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, createD3D12Device ), RHIAvailability::query( RHIBackend::DirectX12 ) );
		#endif
		registerBackend( RHIBackend::Vulkan, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, createVulkanDevice ), RHIAvailability::query( RHIBackend::Vulkan ) );
		registerBackend( RHIBackend::OpenGL, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, createOpenGLDevice ), RHIAvailability::query( RHIBackend::OpenGL ) );
	#endif
#endif
	}

	void RHIBackendRegistry::registerBackend( RHIBackend backend, const RHIDeviceFactoryDelegate& factory, const RHICapabilities& caps )
	{
		for ( RHIBackendEntry& entry : _listEntries )
		{
			if ( entry._backend == backend )
			{
				entry._factory = factory;
				entry._caps	   = caps;
				return;
			}
		}
		_listEntries.push_back( RHIBackendEntry{ backend, factory, caps } );
	}

	const RHIBackendEntry* RHIBackendRegistry::findBackend( RHIBackend backend ) const
	{
		for ( const RHIBackendEntry& entry : _listEntries )
		{
			if ( entry._backend == backend )
				return &entry;
		}
		return nullptr;
	}

	unique_ptr<IRHIDevice> RHIBackendRegistry::createDevice( RHIBackend backend ) const
	{
#if defined( SW_RHI_AS_MODULES )
		ensureBackendModuleLoaded( backend );
#endif

		const RHIBackendEntry* pEntry = findBackend( backend );
		if ( pEntry == nullptr || pEntry->_factory.isBound() == false )
		{
			SW_LOG_ERROR( "[RHIBackendRegistry] No factory registered for backend %#", static_cast<int32>( backend ) );
			return nullptr;
		}
		if ( RHIAvailability::isAvailable( backend ) == false )
		{
			SW_LOG_ERROR( "[RHIBackendRegistry] Backend %# is not available on this platform", static_cast<int32>( backend ) );
			return nullptr;
		}
		return pEntry->_factory();
	}

	bool RHIBackendRegistry::tryLoadModule( RHIBackend backend, string_view modulePath )
	{
		void* pModuleHandle = FileUtil::loadDynamicLibrary( modulePath );
		if ( pModuleHandle == nullptr )
			return false;

		PFN_GetRHIModuleAbiVersion pfnVersion = reinterpret_cast<PFN_GetRHIModuleAbiVersion>( FileUtil::getDynamicSymbol( pModuleHandle, "getRHIModuleAbiVersion" ) );
		if ( pfnVersion == nullptr || pfnVersion() != kRHIModuleAbiVersion )
		{
			SW_LOG_ERROR( "[RHIBackendRegistry] RHI MODULE ABI version mismatch or missing getRHIModuleAbiVersion (%#)", modulePath );
			FileUtil::unloadDynamicLibrary( pModuleHandle );
			return false;
		}

		PFN_GetRHIModuleAbiStamp pfnStamp = reinterpret_cast<PFN_GetRHIModuleAbiStamp>( FileUtil::getDynamicSymbol( pModuleHandle, "getRHIModuleAbiStamp" ) );
		if ( pfnStamp == nullptr || pfnStamp() == nullptr || StringUtil::strcmp( pfnStamp(), kRHIModuleAbiStamp ) != 0 )
		{
			SW_LOG_ERROR( "[RHIBackendRegistry] RHI MODULE ABI stamp mismatch or missing getRHIModuleAbiStamp (%#; expected %#)", modulePath, kRHIModuleAbiStamp );
			FileUtil::unloadDynamicLibrary( pModuleHandle );
			return false;
		}

		PFN_CreateRHIDevice pfnCreate = reinterpret_cast<PFN_CreateRHIDevice>( FileUtil::getDynamicSymbol( pModuleHandle, "createRHIDevice" ) );
		if ( pfnCreate == nullptr )
		{
			FileUtil::unloadDynamicLibrary( pModuleHandle );
			return false;
		}

		RHIDeviceFactoryDelegate factory = SW_DELEGATE_LAMBDA( RHIDeviceFactoryDelegate, [pfnCreate]() -> sw::unique_ptr<IRHIDevice>
		{
			return sw::unique_ptr<IRHIDevice>{ pfnCreate() };
		} );

		registerBackend( backend, factory, RHIAvailability::query( backend ) );
		_listLoadedModules.push_back( LoadedModule{ backend, pModuleHandle } );

		SW_LOG_INFO( "[RHIBackendRegistry] Loaded module %# for backend %#", modulePath, RHI::getBackendTypeName( backend ) );
		return true;
	}

	void RHIBackendRegistry::unloadModules()
	{
		if ( _listLoadedModules.empty() )
			return;

		// Clear factories before FreeLibrary so create() cannot dispatch into unloaded code.
		// Devices from these factories must already be destroyed (see RHI::shutdown).
		for ( const auto& [backEnd, handle] : _listLoadedModules )
		{
			for ( RHIBackendEntry& entry : _listEntries )
			{
				if ( entry._backend == backEnd )
				{
					entry._factory = {};
					break;
				}
			}
		}

		for ( const LoadedModule& module : _listLoadedModules )
		{
			if ( module._pHandle != nullptr )
				FileUtil::unloadDynamicLibrary( module._pHandle );
		}
		_listLoadedModules.clear();
	}

	RHIBackendRegistry::~RHIBackendRegistry()
	{
		unloadModules();
	}
} // namespace sw
