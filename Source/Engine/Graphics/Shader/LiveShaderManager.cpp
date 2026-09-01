#include "pch.h"

#include "Engine/Graphics/Shader/LiveShaderManager.h"

#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Shader/ShaderCache.h"
#include "Engine/Utility/Module/ReloadFileManager.h"

namespace sw
{
	SW_LOG_CALLER( "LiveShaderManager" );

	vector<string> ShaderIncludeResolver::parseIncludes( string_view shaderSource )
	{
		vector<string> listInclude;
		listInclude.reserve( 8 );
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
			listInclude.push_back( includeFile );
			pos = quoteEnd + 1;
		}

		return listInclude;
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
			SW_LOG_WARNING( "Resource root empty; shader file watch not registered." );
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
			_mapWatchedShader[keyPath].push_back( { desc, onRecompiled } );

			vector<uint8> fileBytes;
			if ( FileUtil::readFile( ioPath, fileBytes ) && fileBytes.empty() == false )
			{
				string		   sourceText( reinterpret_cast<const utf8*>( fileBytes.data() ), fileBytes.size() );
				vector<string> listIncludeFile = ShaderIncludeResolver::parseIncludes( sourceText );
				string		   shaderDir	   = FileUtil::getDirectoryPart( ioPath );

				for ( const string& inc : listIncludeFile )
				{
					string incPath = ResourceUtil::getResourcePath( inc );
					if ( incPath.empty() )
						incPath = shaderDir.empty() ? string( inc ) : FileUtil::joinPath( shaderDir, inc );
					incPath = FileUtil::normalizePath( incPath );
					_mapIncludeDependency[incPath].push_back( keyPath );
				}
			}

			if ( _bInitialized == false )
			{
				string dirPart = FileUtil::getDirectoryPart( desc._filePath );
				if ( dirPart.empty() == false )
					initialize( dirPart );
			}

			SW_LOG_INFO( "Registered live shader watch target: %#", ioPath.c_str() );
		}
	}

	void LiveShaderManager::update()
	{
		if ( _bInitialized == false )
			return;

		if ( _listPendingReloadPath.empty() == false )
		{
			vector<string> listReloadToProcess;
			{
				std::unique_lock<std::shared_mutex> lock{ _mutex };
				listReloadToProcess = std::move( _listPendingReloadPath );
				_listPendingReloadPath.clear();
			}

			for ( const string& changedPath : listReloadToProcess )
			{
				vector<WatchedShaderInfo> listToCompile;
				{
					std::shared_lock<std::shared_mutex>						   lock{ _mutex };
					unordered_map<string, vector<WatchedShaderInfo>>::iterator it = _mapWatchedShader.find( changedPath );
					if ( it != _mapWatchedShader.end() )
					{
						listToCompile = it->second;
					}
				}

				for ( WatchedShaderInfo& watchedInfo : listToCompile )
				{
					SW_LOG_INFO( "Recompiling shader live: %# (Entry: %#)",
								 watchedInfo._desc._filePath.c_str(), watchedInfo._desc._entryPoint.c_str() );

					ShaderCompileResult newResult = ShaderCompiler::compileHLSL( watchedInfo._desc );
					if ( newResult._bSuccess )
					{
						engine::getShaderCache().clearCache();
						SW_LOG_INFO( "Live Shader Recompilation Succeeded for %#!",
									 watchedInfo._desc._filePath.c_str() );

						if ( watchedInfo._onRecompiled.isBound() )
							watchedInfo._onRecompiled( changedPath, newResult );
					}
					else
					{
						SW_LOG_ERROR( "Live Shader Recompilation Failed for %#:\n%#",
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
		_mapWatchedShader.clear();
		_mapIncludeDependency.clear();
		_listPendingReloadPath.clear();
		_bInitialized = false;
	}

	void LiveShaderManager::triggerReloadAll()
	{
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		_listPendingReloadPath.clear();
		_listPendingReloadPath.reserve( _mapWatchedShader.size() );
		for ( const pair<const string, vector<WatchedShaderInfo>>& pair : _mapWatchedShader )
		{
			_listPendingReloadPath.push_back( pair.first );
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
			if ( std::find( _listPendingReloadPath.begin(), _listPendingReloadPath.end(), filePath ) == _listPendingReloadPath.end() )
				_listPendingReloadPath.push_back( filePath );
		};

		if ( _mapWatchedShader.find( normalized ) != _mapWatchedShader.end() )
			enqueueUnique( normalized );

		const unordered_map<string, vector<string>>::const_iterator includeIt = _mapIncludeDependency.find( normalized );
		if ( includeIt != _mapIncludeDependency.end() )
		{
			for ( const string& shaderPath : includeIt->second )
			{
				enqueueUnique( shaderPath );
			}
		}
	}

	void LiveShaderManager::onWatchedFileChanged( const FileChangeEvent& ev )
	{
		const string fullPath = FileUtil::normalizePath( FileUtil::joinPath( ev._directory, ev._filename ) );
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
