#include "pch.h"

#include "Engine/Graphics/Material/MaterialCache.h"

#include "Core/Common/StdHeaders.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Utility/Resource/ResourceManager.h"

namespace sw
{
	SW_LOG_CALLER( "MaterialCache" );

	struct MaterialCache::Impl
	{
		struct Entry
		{
			unique_ptr<Material> _material;
			string				 _path;
			uint32				 _refCount{ 0 };
			bool				 _bGpuInit{ false };
		};

		map<string, Entry, std::less<>> _mapEntry;
		IRHIDevice*						_pDevice;
		std::shared_mutex				_mutex;

		Impl()
			: _mapEntry{}
			, _pDevice{ nullptr }
			, _mutex{}
		{
		}
	};

	MaterialCache::MaterialCache()
		: _impl{ make_unique<Impl>() }
	{
	}

	MaterialCache::~MaterialCache() = default;

	Material* MaterialCache::acquire( string_view relativePath, IRHIDevice* pDevice )
	{
		if ( relativePath.empty() || _impl == nullptr )
			return nullptr;

		if ( pDevice != nullptr )
			_impl->_pDevice = pDevice;

		const string key = FileUtil::normalizePath( relativePath );
		engine::getResourceManager().getAssetDatabase().ensureMeta( key );

		std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
		Impl::Entry&						entry = _impl->_mapEntry[key];
		if ( entry._material == nullptr )
		{
			entry._material = make_unique<Material>();
			entry._path		= key;
		}
		++entry._refCount;

		if ( pDevice != nullptr && entry._bGpuInit == false )
		{
			if ( entry._material->initialize( pDevice, key ) == false )
			{
				SW_LOG_ERROR( "Failed to initialize Material %#", key.c_str() );
				--entry._refCount;
				if ( entry._refCount == 0 )
					_impl->_mapEntry.erase( key );
				return nullptr;
			}
			entry._bGpuInit = true;
		}

		return entry._material.get();
	}

	void MaterialCache::release( string_view relativePath )
	{
		if ( relativePath.empty() || _impl == nullptr )
			return;

		const string key = FileUtil::normalizePath( relativePath );

		std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
		auto								it = _impl->_mapEntry.find( key );
		if ( it != _impl->_mapEntry.end() )
		{
			--it->second._refCount;
			if ( it->second._refCount == 0 )
				_impl->_mapEntry.erase( it );
		}
	}

	void MaterialCache::reload( string_view relativePath )
	{
		if ( relativePath.empty() || _impl == nullptr )
			return;

		const string key{ FileUtil::normalizePath( relativePath ) };

		std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
		auto								it{ _impl->_mapEntry.find( key ) };
		if ( it != _impl->_mapEntry.end() )
		{
			if ( it->second._bGpuInit && _impl->_pDevice != nullptr )
				it->second._material->shutdown( _impl->_pDevice );
			it->second._bGpuInit = false;

			if ( _impl->_pDevice != nullptr )
			{
				if ( it->second._material->initialize( _impl->_pDevice, key ) == false )
					SW_LOG_ERROR( "Hot-Reload failed for Material %#", key.c_str() );
				else
					it->second._bGpuInit = true;
			}
		}
	}

	void MaterialCache::shutdownAllGpu( IRHIDevice* pDevice )
	{
		if ( _impl == nullptr )
			return;

		std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
		for ( auto& [path, entry] : _impl->_mapEntry )
		{
			(void)path;
			if ( entry._material != nullptr && entry._bGpuInit )
			{
				entry._material->shutdown( pDevice );
				entry._bGpuInit = false;
			}
		}
	}

	bool MaterialCache::reinitializeAll( IRHIDevice* pDevice )
	{
		if ( pDevice == nullptr || _impl == nullptr )
			return false;
		bool								ok{ true };
		std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
		for ( auto& [path, entry] : _impl->_mapEntry )
		{
			if ( entry._material == nullptr || path.empty() )
				continue;
			if ( entry._material->initialize( pDevice, path ) == false )
			{
				SW_LOG_ERROR( "reinitialize failed for %#", path.c_str() );
				ok = false;
				continue;
			}
			entry._bGpuInit = true;
		}
		return ok;
	}

	void MaterialCache::clear()
	{
		if ( _impl != nullptr )
		{
			std::unique_lock<std::shared_mutex> lock{ _impl->_mutex };
			_impl->_mapEntry.clear();
		}
	}
} // namespace sw
