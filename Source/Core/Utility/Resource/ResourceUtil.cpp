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
	bool							   ResourceUtil::_s_bInitialize = false;
	std::string						   ResourceUtil::_s_engineFolderPath;
	std::string						   ResourceUtil::_s_commonFolderPath;
	std::string						   ResourceUtil::_s_gameFolderPath;
	std::vector<std::filesystem::path> ResourceUtil::_s_resourceFolderList;
	std::vector<std::string>		   ResourceUtil::_s_resourceFolderStrList;

	namespace
	{
		constexpr const utf8* kResourceFolder = "Resource";

		std::string trimTrailingSlashes( std::string path )
		{
			while ( path.size() > 1 && ( path.back() == '/' || path.back() == '\\' ) )
				path.pop_back();
			return path;
		}

		std::string toGeneric( std::string path )
		{
			for ( utf8& c : path )
			{
				if ( c == '\\' )
					c = '/';
			}
			return path;
		}
	} // namespace

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
		const std::string		 lowerName = FileUtil::normalizePath( folderName );

		for ( const std::filesystem::path& resourceFolder : _s_resourceFolderList )
		{
			std::filesystem::path folderPath = resourceFolder / lowerName;
			if ( exists( folderPath ) && is_directory( folderPath ) )
			{
				folders.push_back( FileUtil::normalizeSeparators( folderPath.generic_string() ) );
				continue;
			}

			// Legacy mixed-case folder fallback
			if ( lowerName != folderName )
			{
				folderPath = resourceFolder / folderName;
				if ( exists( folderPath ) && is_directory( folderPath ) )
					folders.push_back( FileUtil::normalizeSeparators( folderPath.generic_string() ) );
			}
		}
		return folders;
	}

	bool ResourceUtil::initialize()
	{
		if ( _s_bInitialize )
			return true;
		_s_bInitialize = true;

		std::filesystem::path currentPath = FileUtil::getCurrentPath();

		std::filesystem::path rootPath{};
		while ( currentPath.has_parent_path() )
		{
			const std::filesystem::path folderPath = currentPath / kResourceFolder;
			if ( FileUtil::isDirectoryExist( folderPath.string() ) )
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

		_s_engineFolderPath = FileUtil::isDirectoryExist( resourceEngine.string() ) ? FileUtil::normalizeSeparators( resourceEngine.string() ) : "";
		_s_commonFolderPath = FileUtil::isDirectoryExist( resourceCommon.string() ) ? FileUtil::normalizeSeparators( resourceCommon.string() ) : "";
		_s_gameFolderPath	= FileUtil::isDirectoryExist( resourceGame.string() ) ? FileUtil::normalizeSeparators( resourceGame.string() ) : "";

		_s_resourceFolderList.reserve( 10 );

		// 1. Game sub-packages (Base, DLC1, etc.) - Highest priority
		if ( FileUtil::isDirectoryExist( resourceGame.string() ) )
		{
			for ( const auto& entry : std::filesystem::directory_iterator( resourceGame ) )
			{
				if ( entry.is_directory() )
					_s_resourceFolderList.push_back( entry.path() / "" );
			}
		}

		// 2. Game root
		if ( FileUtil::isDirectoryExist( resourceGame.string() ) )
			_s_resourceFolderList.push_back( resourceGame );

		// 3. Common
		if ( FileUtil::isDirectoryExist( resourceCommon.string() ) )
			_s_resourceFolderList.push_back( resourceCommon );

		// 4. Engine
		if ( FileUtil::isDirectoryExist( resourceEngine.string() ) )
			_s_resourceFolderList.push_back( resourceEngine );

		// 5. Root - Lowest priority
		_s_resourceFolderList.push_back( resourceRoot );

		_s_resourceFolderStrList.reserve( _s_resourceFolderList.size() );
		for ( const auto& path : _s_resourceFolderList )
		{
			_s_resourceFolderStrList.push_back( FileUtil::normalizeSeparators( path.string() ) );
		}

		return true;
	}

	std::string ResourceUtil::getResourcePath( const std::string_view filePath, const std::string_view folderName )
	{
		const std::string lowerFile	  = FileUtil::normalizePath( filePath );
		const std::string lowerFolder = folderName.empty() ? std::string{} : FileUtil::normalizePath( folderName );

		auto tryResolve = [&]( const std::string_view relFile, const std::string_view relFolder ) -> std::string
		{
			for ( const std::filesystem::path& resourceFolder : _s_resourceFolderList )
			{
				const std::filesystem::path absolutePath = relFolder.empty()
															   ? resourceFolder / relFile
															   : resourceFolder / relFolder / relFile;
				if ( exists( absolutePath ) )
					return FileUtil::normalizeSeparators( absolutePath.generic_string() );
			}
			return {};
		};

		std::string found = tryResolve( lowerFile, lowerFolder );
		// Legacy mixed-case relative path fallback
		if ( found.empty()
			 && ( lowerFile != filePath || ( folderName.empty() == false && lowerFolder != folderName ) ) )
		{
			found = tryResolve( filePath, folderName );
		}
		return found;
	}

	std::string ResourceUtil::makeSaveFolderPath( const std::string_view absoluteFolder )
	{
		const std::string folderNorm = trimTrailingSlashes( FileUtil::normalizePath( absoluteFolder ) );

		std::string physicalRoot;
		std::string rootNorm;
		for ( const std::filesystem::path& root : _s_resourceFolderList )
		{
			std::string rootPhysical = trimTrailingSlashes( toGeneric( root.generic_string() ) );
			std::string candidateNorm = trimTrailingSlashes( FileUtil::normalizePath( rootPhysical ) );
			if ( candidateNorm.empty() )
				continue;

			const bool bExact = ( folderNorm == candidateNorm );
			const bool bUnder = ( folderNorm.size() > candidateNorm.size()
								  && folderNorm.compare( 0, candidateNorm.size(), candidateNorm ) == 0
								  && folderNorm[candidateNorm.size()] == '/' );
			if ( bExact == false && bUnder == false )
				continue;

			if ( rootNorm.empty() || candidateNorm.size() > rootNorm.size() )
			{
				physicalRoot = std::move( rootPhysical );
				rootNorm	 = std::move( candidateNorm );
			}
		}

		if ( physicalRoot.empty() )
			return trimTrailingSlashes( FileUtil::normalizeSeparators( std::string{ absoluteFolder } ) );

		std::string result = physicalRoot;
		if ( folderNorm.size() > rootNorm.size() )
		{
			std::string rel = folderNorm.substr( rootNorm.size() );
			while ( rel.empty() == false && rel.front() == '/' )
				rel.erase( rel.begin() );
			if ( rel.empty() == false )
				result += "/" + rel; // already lowercase from folderNorm
		}
		return result;
	}

	std::string ResourceUtil::makeSavePath( const std::string_view absoluteFolder, const std::string_view fileName )
	{
		std::string result = makeSaveFolderPath( absoluteFolder );
		if ( fileName.empty() == false )
		{
			const std::string lowerName = FileUtil::normalizePath( fileName );
			if ( result.empty() )
				result = lowerName;
			else
				result += "/" + lowerName;
		}
		return result;
	}

} // namespace sw
