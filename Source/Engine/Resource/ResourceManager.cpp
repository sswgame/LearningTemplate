#include "pch.h"

#include "Engine/Resource/ResourceManager.h"

#include "Core/Log/Logger.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
	SW_LOG_CALLER( "ResourceManager" );

	ResourceManager::ResourceManager()
		: _assetDatabase{}
		, _assetFormatRegistry{}
		, _materialCache{ make_unique<MaterialCache>() }
		, _prefabManager{ make_unique<PrefabManager>() }
		, _resourceWatchHandle{}
		, _pReloadFileManager{ nullptr }
	{
	}

	ResourceManager::~ResourceManager() = default;

	bool ResourceManager::initialize()
	{
		if ( ResourceUtil::initialize() == false )
		{
			SW_LOG_ERROR( "Failed to initialize ResourceManager!" );
			return false;
		}
		_assetFormatRegistry.ensureBuiltins();
		return true;
	}

	void ResourceManager::shutdown()
	{
		detachReloadFileManager();

		if ( _materialCache != nullptr )
			_materialCache->clear();
		_assetDatabase.clear();
	}

	void ResourceManager::garbageCollectUnusedAssets()
	{
		engine::getAssetStreamingQueue().sweepUnusedCache();
		// Note: MaterialCache automatically cleans up materials with 0 refcount in release().
	}

	void ResourceManager::attachReloadFileManager( ReloadFileManager& reloadFileManager )
	{
		detachReloadFileManager();
		_pReloadFileManager = &reloadFileManager;

		vector<string>		   listExtension{ ".mat", ".prefab", ".json", ".xml", ".glTF", ".gltf", ".obj" };
		FileWatchMatchDelegate fileWatchDelegate{ SW_DELEGATE_METHOD( FileWatchMatchDelegate, &ResourceManager::onResourceFileChanged, this ) };
		_resourceWatchHandle = _pReloadFileManager->registerWatch( "Resource/", listExtension, fileWatchDelegate );
	}

	void ResourceManager::detachReloadFileManager()
	{
		if ( _resourceWatchHandle.isValid() && _pReloadFileManager != nullptr )
		{
			_pReloadFileManager->unregisterWatch( _resourceWatchHandle );
			_resourceWatchHandle = {};
		}
		_pReloadFileManager = nullptr;
	}

	void ResourceManager::onResourceFileChanged( const FileChangeEvent& ev )
	{
		if ( ev._action != FileWatcherAction::Modified )
			return;

		string relPath{};
		if ( FileUtil::makePathRelative( ResourceUtil::getRootFolderPath(), FileUtil::joinPath( ev._directory, ev._filename ), relPath ) == false )
			return;

		if ( relPath.empty() )
			return;

		SW_LOG_INFO( "Hot-Reloading asset: %#", relPath.c_str() );

		const string extension{ FileUtil::getExtension( ev._filename ) };
		if ( extension == ".mat" )
		{
			// Try to reload from cache
			_materialCache->reload( relPath );
		}
		else if ( extension == ".prefab" )
		{
			// Future expansion for prefabs if needed.
		}
	}

	MaterialCache& ResourceManager::getMaterialManager()
	{
		return *_materialCache;
	}

	const MaterialCache& ResourceManager::getMaterialManager() const
	{
		return *_materialCache;
	}

	PrefabManager& ResourceManager::getPrefabManager()
	{
		return *_prefabManager;
	}

	const PrefabManager& ResourceManager::getPrefabManager() const
	{
		return *_prefabManager;
	}
} // namespace sw
