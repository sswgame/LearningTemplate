#include "pch.h"

#include "Engine/Utility/Resource/ResourceUtil.h"

SW_LOG_CALLER( "ResourceUtil" );
namespace sw
{

	namespace
	{
		std::shared_mutex			  _s_pathCacheMutex;
		unordered_map<string, string> _s_mapResolvedPaths;

		/**
		 * @brief 전역 ID(`engine/`·`common/`·`editor/`·`game/<pack>/`)를 도메인 루트로 매핑합니다.
		 * @param lowerRel normalizePath 된 상대 키
		 * @param outRoot 도메인 절대 루트
		 * @param outKeyUnderRoot 그 루트 아래 상대 키 (팩 이름만이면 empty)
		 * @return 알려진 전역 ID이고 해당 루트가 존재하면 true
		 */
		bool mapGlobalIdToRoot( string_view lowerRel, string& outRoot, string& outKeyUnderRoot )
		{
			outRoot.clear();
			outKeyUnderRoot.clear();

			struct StaticDomainRoute
			{
				string_view _prefix;
				const string& ( *_pGetFolderFunc )();
			};

			static constexpr StaticDomainRoute kStaticDomainRoutes[] = {
				{path::kEnginePack, &ResourceUtil::getEngineFolderPath},
				{path::kCommonPack, &ResourceUtil::getCommonFolderPath},
				{path::kEditorPack, &ResourceUtil::getEditorFolderPath},
			};

			for ( const auto& route : kStaticDomainRoutes )
			{
				if ( FileUtil::startsWithPathComponent( lowerRel, route._prefix ) )
				{
					outRoot			= route._pGetFolderFunc();
					outKeyUnderRoot = FileUtil::suffixAfterPathComponent( lowerRel, route._prefix );
					return outRoot.empty() == false;
				}
			}

			if ( FileUtil::startsWithPathComponent( lowerRel, path::kGamePack ) )
			{
				const string& gamesRoot = ResourceUtil::getGameFolderPath();
				if ( gamesRoot.empty() )
					return false;
				const string rest  = FileUtil::suffixAfterPathComponent( lowerRel, path::kGamePack );
				const size_t slash = rest.find( '/' );
				if ( slash == string::npos )
				{
					outRoot			= FileUtil::joinPath( gamesRoot, rest );
					outKeyUnderRoot = {};
					return FileUtil::directoryExists( outRoot );
				}
				outRoot			= FileUtil::joinPath( gamesRoot, rest.substr( 0, slash ) );
				outKeyUnderRoot = rest.substr( slash + 1 );
				return FileUtil::directoryExists( outRoot );
			}
			return false;
		}

		/**
		 * @brief 단일 검색/도메인 루트 아래에서 파일이 있으면 절대 경로를 반환합니다.
		 * @param root 절대 루트
		 * @param relFile 루트(또는 folder) 아래 상대 파일
		 * @param relFolder 선택적 하위 폴더 (비우면 root 직하)
		 * @return 존재하면 normalizeSeparators 된 절대 경로, 아니면 empty
		 */
		string tryUnderRoot( string_view root, string_view relFile, string_view relFolder )
		{
			if ( root.empty() || relFile.empty() )
				return {};
			const string absolutePath = relFolder.empty()
										  ? FileUtil::joinPath( root, relFile )
										  : FileUtil::joinPath( FileUtil::joinPath( root, relFolder ), relFile );
			if ( FileUtil::fileExists( absolutePath ) )
				return FileUtil::normalizeSeparators( absolutePath );
			return {};
		}

		/**
		 * @brief 검색 루트 목록을 순회하며 tryUnderRoot 합니다.
		 * @param roots getResourcePath 검색 루트들
		 * @param relFile 상대 파일 키
		 * @param relFolder 선택적 하위 폴더
		 * @return 첫 번째로 찾은 절대 경로. 없으면 empty.
		 */
		string tryResolveAmong( const vector<string>& roots, string_view relFile,
								string_view relFolder )
		{
			for ( const string& resourceFolder : roots )
			{
				string found = tryUnderRoot( resourceFolder, relFile, relFolder );
				if ( found.empty() == false )
					return found;
			}
			return {};
		}

		/**
		 * @brief folderNorm가 후보 루트에 속하면, 더 구체적인 루트로 ioPhysicalRoot/ioRootNorm를 갱신합니다.
		 * @param folderNorm normalizePath 된 대상 폴더(절대)
		 * @param rootPhysicalCandidate 후보 물리 루트
		 * @param ioPhysicalRoot 현재까지 고른 FS 대소문자 유지 루트
		 * @param ioRootNorm 현재까지 고른 normalizePath 루트
		 */
		void considerSaveRoot( string_view folderNorm, string_view rootPhysicalCandidate,
							   string& ioPhysicalRoot, string& ioRootNorm )
		{
			if ( rootPhysicalCandidate.empty() )
				return;
			string rootPhysical	 = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( rootPhysicalCandidate ) );
			string candidateNorm = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( rootPhysical ) );
			if ( candidateNorm.empty() || FileUtil::startsWithPathComponent( folderNorm, candidateNorm ) == false )
				return;

			if ( ioRootNorm.empty() || candidateNorm.size() > ioRootNorm.size() )
			{
				ioPhysicalRoot = std::move( rootPhysical );
				ioRootNorm	   = std::move( candidateNorm );
			}
		}

	} // namespace

	bool ResourceUtil::initialize()
	{
		if ( _s_bInitialize.load( std::memory_order_acquire ) )
			return true;
		_s_bInitialize.store( true, std::memory_order_release );

		{
			std::unique_lock<std::shared_mutex> lock( _s_pathCacheMutex );
			_s_mapResolvedPaths.clear();
		}

		string currentPath = FileUtil::getCurrentPath();
		string rootPath;
		while ( currentPath.empty() == false )
		{
			const string folderPath = FileUtil::joinPath( currentPath, path::kResourceFolder );
			if ( FileUtil::directoryExists( folderPath ) )
			{
				rootPath = currentPath;
				break;
			}
			const string parent = FileUtil::getDirectoryPart( currentPath );
			if ( parent.empty() || parent == currentPath )
				break;
			currentPath = parent;
		}

		SW_LOG_ASSERT( rootPath.empty() == false, "RootFolder를 찾지 못했습니다" );
		if ( rootPath.empty() )
			return false;

		SW_LOG_INFO( "RootFolder : %#", rootPath );

		_s_projectFolderPath = FileUtil::normalizeSeparators( FileUtil::trimTrailingSlashes( rootPath ) );

		const string resourceRoot	= FileUtil::joinPath( rootPath, path::kResourceFolder );
		const string resourceEngine = FileUtil::joinPath( resourceRoot, path::kEnginePack );
		const string resourceCommon = FileUtil::joinPath( resourceRoot, path::kCommonPack );
		const string resourceGames	= FileUtil::joinPath( resourceRoot, path::kGamePack );
		const string resourceEditor = FileUtil::joinPath( resourceRoot, path::kEditorPack );

		// Display-only top-level (not a search root — no assets live directly under Resource/).
		_s_resourceRootFolderPath =
			FileUtil::directoryExists( resourceRoot ) ? FileUtil::normalizeSeparators( resourceRoot ) : "";

		_s_engineFolderPath = FileUtil::directoryExists( resourceEngine ) ? FileUtil::normalizeSeparators( resourceEngine ) : "";
		_s_commonFolderPath = FileUtil::directoryExists( resourceCommon ) ? FileUtil::normalizeSeparators( resourceCommon ) : "";
		_s_editorFolderPath = FileUtil::directoryExists( resourceEditor ) ? FileUtil::normalizeSeparators( resourceEditor ) : "";

		_s_gameFolderPath =
			FileUtil::directoryExists( resourceGames ) ? FileUtil::normalizeSeparators( resourceGames ) : "";

		_s_resourceFolderList.reserve( 16 );

		// Search roots: game/<pack>/, common/, engine/, editor/ (never Resource/ or game/ container).
		if ( FileUtil::directoryExists( resourceGames ) )
		{
			vector<string> listPackFolders;
			FileUtil::collectFolders( resourceGames, listPackFolders, false, false );
			for ( const string& packFolder : listPackFolders )
			{
				_s_resourceFolderList.push_back( FileUtil::normalizeSeparators( packFolder ) );
			}
		}

		if ( _s_commonFolderPath.empty() == false )
			_s_resourceFolderList.push_back( _s_commonFolderPath );

		if ( _s_engineFolderPath.empty() == false )
			_s_resourceFolderList.push_back( _s_engineFolderPath );

		if ( _s_editorFolderPath.empty() == false )
			_s_resourceFolderList.push_back( _s_editorFolderPath );

		return true;
	}

	string ResourceUtil::getResourcePath( string_view filePath, string_view folderName )
	{
		if ( filePath.empty() )
			return {};

		string cacheKey;
		if ( folderName.empty() )
		{
			cacheKey = string( filePath );
		}
		else
		{
			cacheKey.reserve( folderName.size() + 1 + filePath.size() );
			cacheKey.append( folderName.data(), folderName.size() );
			cacheKey.push_back( '|' );
			cacheKey.append( filePath.data(), filePath.size() );
		}

		{
			std::shared_lock<std::shared_mutex> lock( _s_pathCacheMutex );
			auto								it = _s_mapResolvedPaths.find( cacheKey );
			if ( it != _s_mapResolvedPaths.end() )
				return it->second;
		}

		const string lowerFile	 = FileUtil::normalizePath( filePath );
		const string lowerFolder = folderName.empty() ? string{} : FileUtil::normalizePath( folderName );

		string found;
		string domainRoot;
		string keyUnderRoot;
		if ( mapGlobalIdToRoot( lowerFile, domainRoot, keyUnderRoot ) )
		{
			found = tryUnderRoot( domainRoot, keyUnderRoot, lowerFolder );
#if defined( SW_PLATFORM_LINUX )
			// Case-sensitive FS: retry original spelling if lowercase key missed.
			if ( found.empty() && lowerFile != filePath )
			{
				const string raw	= string{ filePath };
				string		 rawKey = keyUnderRoot;
				const size_t slash	= FileUtil::normalizeSeparators( raw ).find( '/' );
				if ( slash != string::npos )
					rawKey = FileUtil::normalizeSeparators( raw ).substr( slash + 1 );
				found = tryUnderRoot( domainRoot, rawKey, folderName );
			}
#endif
		}
		else
		{
			found = tryResolveAmong( _s_resourceFolderList, lowerFile, lowerFolder );
#if defined( SW_PLATFORM_LINUX )
			if ( found.empty() && ( lowerFile != filePath || ( folderName.empty() == false && lowerFolder != folderName ) ) )
				found = tryResolveAmong( _s_resourceFolderList, filePath, folderName );
#endif
		}

		if ( found.empty() == false )
		{
			std::unique_lock<std::shared_mutex> lock( _s_pathCacheMutex );
			_s_mapResolvedPaths.insert_or_assign( std::move( cacheKey ), found );
		}

		return found;
	}
	string ResourceUtil::makeAbsolutePath( string_view relativePath )
	{
		if ( relativePath.empty() )
			return {};

		string result = getResourcePath( relativePath );
		if ( result.empty() )
		{
			const string lowerRel = FileUtil::normalizePath( relativePath );
			string		 domainRoot;
			string		 keyUnderRoot;
			if ( mapGlobalIdToRoot( lowerRel, domainRoot, keyUnderRoot ) )
				result = FileUtil::joinPath( domainRoot, keyUnderRoot );
		}
		return result;
	}
	bool ResourceUtil::readTextResource( string_view relativePath, string& outText,
										 string* outAbsPath )
	{
		string absPath = getResourcePath( relativePath );
		if ( absPath.empty() )
			absPath = string( relativePath );
		if ( outAbsPath != nullptr )
			*outAbsPath = absPath;
		return FileUtil::readTextFile( absPath, outText );
	}
	const string& ResourceUtil::getEngineFolderPath()
	{
		return _s_engineFolderPath;
	}
	const string& ResourceUtil::getCommonFolderPath()
	{
		return _s_commonFolderPath;
	}
	const string& ResourceUtil::getGameFolderPath()
	{
		return _s_gameFolderPath;
	}
	const string& ResourceUtil::getEditorFolderPath()
	{
		return _s_editorFolderPath;
	}

	const string& ResourceUtil::getRootFolderPath()
	{
		return _s_resourceRootFolderPath;
	}

	const string& ResourceUtil::getProjectFolderPath()
	{
		return _s_projectFolderPath;
	}

	vector<string> ResourceUtil::getResourceFolders( string_view folderName )
	{
		vector<string> listFolders;
		const string   lowerName = FileUtil::normalizePath( folderName );

		for ( const string& resourceFolder : _s_resourceFolderList )
		{
			const string folderPath = FileUtil::joinPath( resourceFolder, lowerName );
			if ( FileUtil::directoryExists( folderPath ) )
				listFolders.push_back( FileUtil::normalizeSeparators( folderPath ) );

#if defined( SW_PLATFORM_LINUX )
			if ( lowerName != folderName )
			{
				const string rawPath = FileUtil::joinPath( resourceFolder, folderName );
				if ( FileUtil::directoryExists( rawPath ) )
					listFolders.push_back( FileUtil::normalizeSeparators( rawPath ) );
			}
#endif
		}
		return listFolders;
	}

	string ResourceUtil::makeSaveFolderPath( string_view absoluteFolder )
	{
		const string folderNorm = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( absoluteFolder ) );

		string physicalRoot;
		string rootNorm;

		for ( const string& root : _s_resourceFolderList )
		{
			considerSaveRoot( folderNorm, root, physicalRoot, rootNorm );
		}
		considerSaveRoot( folderNorm, _s_gameFolderPath, physicalRoot, rootNorm );
		considerSaveRoot( folderNorm, _s_engineFolderPath, physicalRoot, rootNorm );
		considerSaveRoot( folderNorm, _s_commonFolderPath, physicalRoot, rootNorm );
		considerSaveRoot( folderNorm, _s_editorFolderPath, physicalRoot, rootNorm );

		if ( physicalRoot.empty() )
			return FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( string{ absoluteFolder } ) );

		string result = physicalRoot;
		if ( folderNorm.size() > rootNorm.size() )
		{
			string rel = folderNorm.substr( rootNorm.size() );
			while ( rel.empty() == false && rel.front() == '/' )
			{
				rel.erase( rel.begin() );
			}
			if ( rel.empty() == false )
				result += "/" + rel;
		}
		return result;
	}

	string ResourceUtil::makeSavePath( string_view absoluteFolder, string_view fileName )
	{
		string result = makeSaveFolderPath( absoluteFolder );
		if ( fileName.empty() == false )
		{
			const string lowerName = FileUtil::normalizePath( fileName );
			if ( result.empty() )
				result = lowerName;
			else
				result += "/" + lowerName;
		}
		return result;
	}

	std::atomic<bool> ResourceUtil::_s_bInitialize{ false };

	string ResourceUtil::_s_projectFolderPath;

	string ResourceUtil::_s_resourceRootFolderPath;

	string ResourceUtil::_s_engineFolderPath;

	string ResourceUtil::_s_commonFolderPath;

	string ResourceUtil::_s_gameFolderPath;

	string ResourceUtil::_s_editorFolderPath;

	vector<string> ResourceUtil::_s_resourceFolderList;

} // namespace sw
