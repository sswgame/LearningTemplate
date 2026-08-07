/**
 * @file ResourceUtil.cpp
 * @brief ResourceUtil 경로 해석 구현
 */
#include "pch.h"
#include "ResourceUtil.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	bool										 ResourceUtil::_s_bInitialize = false;
	std::string									 ResourceUtil::_s_engineFolderPath;
	std::string									 ResourceUtil::_s_commonFolderPath;
	std::string									 ResourceUtil::_s_gameFolderPath;
	std::vector<std::filesystem::path>			 ResourceUtil::_s_resourceFolderList;
	std::vector<std::string>					 ResourceUtil::_s_resourceFolderStrList;
	std::unordered_map<std::string, std::string> ResourceUtil::_s_resourcePathCache;

	namespace
	{
		constexpr const utf8* kResourceFolder = "Resource";
	}

	const std::string& ResourceUtil::getEngineFolderPath()
	{
		return _s_engineFolderPath;
	}

	const std::string& ResourceUtil::getCommonFolderPath()
	{
		return _s_commonFolderPath;
	}

	const std::string& ResourceUtil::getGameFolderPath()
	{
		return _s_gameFolderPath;
	}

	const std::string& ResourceUtil::getRootFolderPath()
	{
		static const std::string s_emptyStr;
		return _s_resourceFolderStrList.empty() ? s_emptyStr : _s_resourceFolderStrList.back();
	}

	std::vector<std::string> ResourceUtil::getResourceFolders( const std::string_view folderName )
	{
		std::vector<std::string> folders;
		for ( const std::filesystem::path& resourceFolder : _s_resourceFolderList )
		{
			std::filesystem::path folderPath = resourceFolder / folderName;
			if ( exists( folderPath ) && is_directory( folderPath ) )
			{
				folders.push_back( FileUtil::normalizePath( folderPath.generic_string() ) );
			}
		}
		return folders;
	}

	bool ResourceUtil::initialize()
	{
		if ( _s_bInitialize )
			return true;
		_s_bInitialize = true;

		std::filesystem::path currentPath = std::filesystem::current_path();

		std::filesystem::path rootPath{};
		while ( currentPath.has_parent_path() )
		{
			const std::filesystem::path folderPath = currentPath / kResourceFolder;
			if ( exists( folderPath ) && is_directory( folderPath ) )
			{
				rootPath = currentPath;
				break;
			}
			currentPath = currentPath.parent_path();
		}

		SW_LOG_ASSERT( rootPath.empty() == false, "RootFolder를 찾지 못했습니다" );
		if ( rootPath.empty() )
			return false;

		SW_LOG_INFO( "RootFolder : %#", rootPath.string() );

		const std::filesystem::path resourceRoot   = rootPath / kResourceFolder / "";
		const std::filesystem::path resourceEngine = rootPath / kResourceFolder / "Engine" / "";
		const std::filesystem::path resourceCommon = rootPath / kResourceFolder / "Common" / "";
		const std::filesystem::path resourceGame   = rootPath / kResourceFolder / "Game" / "";

		_s_engineFolderPath = exists( resourceEngine ) ? resourceEngine.string() : "";
		_s_commonFolderPath = exists( resourceCommon ) ? resourceCommon.string() : "";
		_s_gameFolderPath   = exists( resourceGame ) ? resourceGame.string() : "";

		_s_resourceFolderList.reserve( 10 );

		// 1. Game sub-packages (Base, DLC1, etc.) - Highest priority
		if ( exists( resourceGame ) && is_directory( resourceGame ) )
		{
			for ( const auto& entry : std::filesystem::directory_iterator( resourceGame ) )
			{
				if ( entry.is_directory() )
				{
					_s_resourceFolderList.push_back( entry.path() / "" );
				}
			}
		}

		// 2. Game root
		if ( exists( resourceGame ) )   _s_resourceFolderList.push_back( resourceGame );

		// 3. Common
		if ( exists( resourceCommon ) ) _s_resourceFolderList.push_back( resourceCommon );

		// 4. Engine
		if ( exists( resourceEngine ) ) _s_resourceFolderList.push_back( resourceEngine );

		// 5. Root - Lowest priority
		_s_resourceFolderList.push_back( resourceRoot );

		_s_resourceFolderStrList.reserve( _s_resourceFolderList.size() );
		for ( const auto& path : _s_resourceFolderList )
		{
			_s_resourceFolderStrList.push_back( path.string() );
		}

		return true;
	}

	std::string ResourceUtil::getResourcePath( const std::string_view filePath, const std::string_view folderName  )
	{
		std::string cacheKey;
		if ( folderName.empty() )
		{
			cacheKey = filePath;
		}
		else
		{
			cacheKey.reserve( folderName.size() + 1 + filePath.size() );
			cacheKey.append( folderName ).append( "/" ).append( filePath );
		}

		auto iter = _s_resourcePathCache.find( cacheKey );
		if ( iter != _s_resourcePathCache.end() )
		{
			return iter->second;
		}

		for ( const std::filesystem::path& resourceFolder : _s_resourceFolderList )
		{
			const std::filesystem::path absolutePath = ( folderName.empty() ) ? resourceFolder / filePath : resourceFolder / folderName / filePath;
			if ( exists( absolutePath ) )
			{
				std::string resolved = FileUtil::normalizePath( absolutePath.generic_string() );
				_s_resourcePathCache.try_emplace( std::move( cacheKey ), resolved );
				return resolved;
			}
		}

		return std::string{};
	}

	void ResourceUtil::clearCache()
	{
		_s_resourcePathCache.clear();
	}
}
