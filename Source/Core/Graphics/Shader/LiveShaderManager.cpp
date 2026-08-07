/**
 * @file LiveShaderManager.cpp
 * @brief LiveShaderManager 구현
 */
#include "pch.h"
#include "LiveShaderManager.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Resource/ResourceUtil.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/File/ReloadFileManager.h"

namespace sw
{
	std::vector<std::string> ShaderIncludeResolver::parseIncludes( const std::string& shaderSource )
	{
		std::vector<std::string> includes;
		includes.reserve( 8 );
		std::string_view view( shaderSource );

		std::string_view::size_type pos = 0;
		while ( ( pos = view.find( "#include", pos ) ) != std::string_view::npos )
		{
			pos += 8;
			std::string_view::size_type quoteStart = view.find( '"', pos );
			if ( quoteStart == std::string_view::npos )
				break;

			std::string_view::size_type quoteEnd = view.find( '"', quoteStart + 1 );
			if ( quoteEnd == std::string_view::npos )
				break;

			std::string includeFile( view.substr( quoteStart + 1, quoteEnd - quoteStart - 1 ) );
			includes.push_back( includeFile );
			pos = quoteEnd + 1;
		}

		return includes;
	}

	LiveShaderManager::LiveShaderManager() = default;

	LiveShaderManager::~LiveShaderManager()
	{
		shutdown();
	}

	bool LiveShaderManager::initialize( const std::string& watchDirectory )
	{
		_watchDirectory = watchDirectory;
		_bInitialized	= true;
		return true;
	}

	void LiveShaderManager::watchShader( const ShaderCompileDesc& desc, const ShaderRecompiledDelegate& onRecompiled )
	{
		BLOCK( "LiveShaderManager 셰이더 등록" )
		{
			std::string resolvedPath = ResourceUtil::getResourcePath( desc._filePath );
			if ( resolvedPath.empty() )
			{
				resolvedPath = desc._filePath;
			}
			resolvedPath = FileUtil::normalizePath( resolvedPath );

			_watchedShaders[resolvedPath].push_back( { desc, onRecompiled } );

			std::vector<uint8> fileBytes;
			if ( FileUtil::readFile( resolvedPath, fileBytes ) && fileBytes.empty() == false )
			{
				std::string				 sourceText( reinterpret_cast<const utf8*>( fileBytes.data() ), fileBytes.size() );
				std::vector<std::string> includeFiles = ShaderIncludeResolver::parseIncludes( sourceText );
				std::string				 shaderDir	  = FileUtil::getDirectoryPart( desc._filePath );

				for ( const std::string& inc : includeFiles )
				{
					std::string incPath = shaderDir.empty() ? inc : ( shaderDir + "/" + inc );
					incPath				= FileUtil::normalizePath( incPath );
					_includeDependencies[incPath].push_back( resolvedPath );
				}
			}

			if ( _bInitialized == false )
			{
				std::string dirPart = FileUtil::getDirectoryPart( desc._filePath );
				if ( dirPart.empty() == false )
				{
					initialize( dirPart );
				}
			}

			SW_LOG_INFO( "[LiveShaderManager] Registered live shader watch target: %#", resolvedPath.c_str() );
		}
	}

	void LiveShaderManager::triggerReloadAll()
	{
		for ( const auto& pair : _watchedShaders )
		{
			_pendingReloadPaths.push_back( pair.first );
		}
	}

	void LiveShaderManager::notifyFileChanged( const std::string& path )
	{
		if ( _bInitialized == false )
			return;

		const std::string normalized = FileUtil::normalizePath( path );
		if ( _watchedShaders.find( normalized ) != _watchedShaders.end() )
			_pendingReloadPaths.push_back( normalized );

		const auto includeIt = _includeDependencies.find( normalized );
		if ( includeIt != _includeDependencies.end() )
		{
			for ( const std::string& shaderPath : includeIt->second )
				_pendingReloadPaths.push_back( shaderPath );
		}
	}

	void LiveShaderManager::update()
	{
		if ( _bInitialized == false )
			return;

		if ( _pendingReloadPaths.empty() == false )
		{
			std::vector<std::string> reloadsToProcess = std::move( _pendingReloadPaths );
			_pendingReloadPaths.clear();

			for ( const std::string& changedPath : reloadsToProcess )
			{
				std::unordered_map<std::string, std::vector<WatchedShaderInfo>>::iterator it = _watchedShaders.find( changedPath );
				if ( it != _watchedShaders.end() )
				{
					for ( WatchedShaderInfo& watchedInfo : it->second )
					{
						SW_LOG_INFO( "[LiveShaderManager] Recompiling shader live: %# (Entry: %#)",
									 watchedInfo._desc._filePath.c_str(), watchedInfo._desc._entryPoint.c_str() );

						ShaderCompileResult newResult = ShaderCompiler::compileHLSL( watchedInfo._desc );
						if ( newResult._bSuccess )
						{
							SW_LOG_INFO( "[LiveShaderManager SUCCESS] Live Shader Recompilation Succeeded for %#!",
										 watchedInfo._desc._filePath.c_str() );

							if ( watchedInfo._onRecompiled.isBound() )
							{
								watchedInfo._onRecompiled( changedPath, newResult );
							}
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
	}

	void LiveShaderManager::shutdown()
	{
		_watchedShaders.clear();
		_includeDependencies.clear();
		_pendingReloadPaths.clear();
		_bInitialized = false;
	}
}
