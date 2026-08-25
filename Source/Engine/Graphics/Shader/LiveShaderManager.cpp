#include "pch.h"

#include "Engine/Graphics/Shader/LiveShaderManager.h"

#include "Engine/Graphics/Shader/ShaderCache.h"
#include "Engine/Utility/File/ReloadFileManager.h"

namespace sw
{
	vector<string> ShaderIncludeResolver::parseIncludes( string_view shaderSource )
	{
		vector<string> listIncludes;
		listIncludes.reserve( 8 );
		const string_view view( shaderSource );

		string_view::size_type pos{ 0 };
		while ( ( pos = view.find( "#include", pos ) ) != string_view::npos )
		{
			pos += 8;
			string_view::size_type quoteStart = view.find( '"', pos );
			if ( quoteStart == string_view::npos )
				break;

			string_view::size_type quoteEnd = view.find( '"', quoteStart + 1 );
			if ( quoteEnd == string_view::npos )
				break;

			string includeFile( view.substr( quoteStart + 1, quoteEnd - quoteStart - 1 ) );
			listIncludes.push_back( includeFile );
			pos = quoteEnd + 1;
		}

		return listIncludes;
	}

	LiveShaderManager::LiveShaderManager() = default;

	LiveShaderManager::~LiveShaderManager()
	{
		shutdown();
	}

	bool LiveShaderManager::initialize( string_view watchDirectory )
	{
		_watchDirectory = watchDirectory;
		_bInitialized	= true;
		return true;
	}

	void LiveShaderManager::attachReloadFileManager( ReloadFileManager& reloadFiles )
	{
		detachReloadFileManager();
		_pReloadFiles = &reloadFiles;

		const string shaderWatchRoot = ResourceUtil::getRootFolderPath();
		if ( shaderWatchRoot.empty() )
		{
			SW_LOG_WARNING( "[LiveShaderManager] Resource root empty; shader file watch not registered." );
			return;
		}

		FileWatchMatchDelegate fileWatchDelegate =
			SW_DELEGATE_METHOD( FileWatchMatchDelegate, &LiveShaderManager::onWatchedFileChanged, this );
		_fileWatchHandle = _pReloadFiles->registerWatch( shaderWatchRoot, { ".hlsl", ".hlsli" }, fileWatchDelegate );
	}

	void LiveShaderManager::watchShader( const ShaderCompileDesc& desc, const ShaderRecompiledDelegate& onRecompiled )
	{
		BLOCK( "LiveShaderManager 셰이더 등록" )
		{
			string ioPath = ResourceUtil::getResourcePath( desc._filePath );
			if ( ioPath.empty() )
				ioPath = desc._filePath;

			// Map / notify keys are lowercase; I/O keeps the real FS path.
			const string keyPath = FileUtil::normalizePath( ioPath );

			std::unique_lock<std::shared_mutex> lock{ _mutex };
			_mapWatchedShaders[keyPath].push_back( { desc, onRecompiled } );

			vector<uint8> listFileBytes;
			if ( FileUtil::readFile( ioPath, listFileBytes ) && listFileBytes.empty() == false )
			{
				string		   sourceText( reinterpret_cast<const utf8*>( listFileBytes.data() ), listFileBytes.size() );
				vector<string> listIncludeFiles = ShaderIncludeResolver::parseIncludes( sourceText );
				string		   shaderDir		= FileUtil::getDirectoryPart( desc._filePath );

				for ( const string& inc : listIncludeFiles )
				{
					string incPath = shaderDir.empty() ? string( inc ) : ( shaderDir + "/" + inc );
					incPath		   = FileUtil::normalizePath( incPath );
					_mapIncludeDependencies[incPath].push_back( keyPath );
				}
			}

			if ( _bInitialized == false )
			{
				string dirPart = FileUtil::getDirectoryPart( desc._filePath );
				if ( dirPart.empty() == false )
					initialize( dirPart );
			}

			SW_LOG_INFO( "[LiveShaderManager] Registered live shader watch target: %#", ioPath.c_str() );
		}
	}

	void LiveShaderManager::update()
	{
		if ( _bInitialized == false )
			return;

		if ( _listPendingReloadPaths.empty() == false )
		{
			vector<string> listReloadsToProcess;
			{
				std::unique_lock<std::shared_mutex> lock{ _mutex };
				listReloadsToProcess = std::move( _listPendingReloadPaths );
				_listPendingReloadPaths.clear();
			}

			for ( const string& changedPath : listReloadsToProcess )
			{
				vector<WatchedShaderInfo> listToCompile;
				{
					std::shared_lock<std::shared_mutex>						   lock{ _mutex };
					unordered_map<string, vector<WatchedShaderInfo>>::iterator it = _mapWatchedShaders.find( changedPath );
					if ( it != _mapWatchedShaders.end() )
					{
						listToCompile = it->second;
					}
				}

				for ( WatchedShaderInfo& watchedInfo : listToCompile )
				{
					SW_LOG_INFO( "[LiveShaderManager] Recompiling shader live: %# (Entry: %#)",
								 watchedInfo._desc._filePath.c_str(), watchedInfo._desc._entryPoint.c_str() );

					ShaderCompileResult newResult = ShaderCompiler::compileHLSL( watchedInfo._desc );
					if ( newResult._bSuccess )
					{
						ShaderCache::clearCache();
						SW_LOG_INFO( "[LiveShaderManager SUCCESS] Live Shader Recompilation Succeeded for %#!",
									 watchedInfo._desc._filePath.c_str() );

						if ( watchedInfo._onRecompiled.isBound() )
							watchedInfo._onRecompiled( changedPath, newResult );
					}
					else
					{
						SW_LOG_ERROR( "[LiveShaderManager ERROR] Live Shader Recompilation Failed for %#:\n%#",
									  watchedInfo._desc._filePath.c_str(), newResult._errorMessage.c_str() );
					}
				}
			}
		}
	}

	void LiveShaderManager::shutdown()
	{
		detachReloadFileManager();

		std::unique_lock<std::shared_mutex> lock{ _mutex };
		_mapWatchedShaders.clear();
		_mapIncludeDependencies.clear();
		_listPendingReloadPaths.clear();
		_bInitialized = false;
	}

	void LiveShaderManager::triggerReloadAll()
	{
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		_listPendingReloadPaths.clear();
		_listPendingReloadPaths.reserve( _mapWatchedShaders.size() );
		for ( const std::pair<const string, vector<WatchedShaderInfo>>& pair : _mapWatchedShaders )
		{
			_listPendingReloadPaths.push_back( pair.first );
		}
	}

	void LiveShaderManager::notifyFileChanged( string_view path )
	{
		if ( _bInitialized == false )
			return;

		const string normalized = FileUtil::normalizePath( path );

		std::unique_lock<std::shared_mutex> lock{ _mutex };

		auto enqueueUnique = [this]( const string& filePath )
		{
			if ( std::find( _listPendingReloadPaths.begin(), _listPendingReloadPaths.end(), filePath ) == _listPendingReloadPaths.end() )
				_listPendingReloadPaths.push_back( filePath );
		};

		if ( _mapWatchedShaders.find( normalized ) != _mapWatchedShaders.end() )
			enqueueUnique( normalized );

		const unordered_map<string, vector<string>>::const_iterator includeIt = _mapIncludeDependencies.find( normalized );
		if ( includeIt != _mapIncludeDependencies.end() )
		{
			for ( const string& shaderPath : includeIt->second )
			{
				enqueueUnique( shaderPath );
			}
		}
	}

	void LiveShaderManager::onWatchedFileChanged( const FileChangeEvent& ev )
	{
		const string fullPath = FileUtil::normalizePath( ev._directory + "/" + ev._filename );
		notifyFileChanged( fullPath );
	}

	void LiveShaderManager::detachReloadFileManager()
	{
		if ( _pReloadFiles != nullptr && _fileWatchHandle.isValid() )
			_pReloadFiles->unregisterWatch( _fileWatchHandle );
		_fileWatchHandle = {};
		_pReloadFiles	 = nullptr;
	}
} // namespace sw
