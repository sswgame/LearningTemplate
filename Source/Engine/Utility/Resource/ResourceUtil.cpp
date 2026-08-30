#include "pch.h"

#include "Engine/Utility/Resource/ResourceUtil.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"

namespace sw
{
	namespace
	{
		struct ResourceUtilInternal
		{
			inline static std::shared_mutex				_s_pathCacheMutex{};
			inline static unordered_map<string, string> _s_mapResolvedPaths{};

			/**
			 * @brief 전역 ID(`engine/`·`common/`·`editor/`·`game/<pack>/`)를 도메인 루트로 매핑합니다.
			 * @param lowerRel normalizePath 된 상대 키
			 * @param outRoot 도메인 절대 루트
			 * @param outKeyUnderRoot 그 루트 아래 상대 키 (팩 이름만이면 empty)
			 * @return 알려진 전역 ID이고 해당 루트가 존재하면 true
			 */
			static bool mapGlobalIdToRoot( string_view lowerRel, string& outRoot, string& outKeyUnderRoot )
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
			static string tryUnderRoot( string_view root, string_view relFile, string_view relFolder )
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
			static string tryResolveAmong( const vector<string>& roots, string_view relFile,
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
			static void considerSaveRoot( string_view folderNorm, string_view rootPhysicalCandidate,
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
		};
	} // namespace
} // namespace sw

namespace sw
{
	SW_LOG_CALLER( "ResourceUtil" );

	bool ResourceUtil::initialize()
	{
		if ( _s_bInitialize.load( std::memory_order_acquire ) )
			return true;
		_s_bInitialize.store( true, std::memory_order_release );

		{
			std::unique_lock<std::shared_mutex> lock( ResourceUtilInternal::_s_pathCacheMutex );
			ResourceUtilInternal::_s_mapResolvedPaths.clear();
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

		if ( _s_listSearchPriority.empty() )
			_s_listSearchPriority = getDefaultSearchPriority();

		setSearchPriority( _s_listSearchPriority );
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
			std::shared_lock<std::shared_mutex> lock( ResourceUtilInternal::_s_pathCacheMutex );
			auto								it = ResourceUtilInternal::_s_mapResolvedPaths.find( cacheKey );
			if ( it != ResourceUtilInternal::_s_mapResolvedPaths.end() )
				return it->second;
		}

		const string lowerFile	 = FileUtil::normalizePath( filePath );
		const string lowerFolder = folderName.empty() ? string{} : FileUtil::normalizePath( folderName );

		string found;
		string domainRoot;
		string keyUnderRoot;
		if ( ResourceUtilInternal::mapGlobalIdToRoot( lowerFile, domainRoot, keyUnderRoot ) )
		{
			found = ResourceUtilInternal::tryUnderRoot( domainRoot, keyUnderRoot, lowerFolder );
#if defined( SW_PLATFORM_LINUX )
			// Case-sensitive FS: retry original spelling if lowercase key missed.
			if ( found.empty() && lowerFile != filePath )
			{
				const string raw	= string{ filePath };
				string		 rawKey = keyUnderRoot;
				const size_t slash	= FileUtil::normalizeSeparators( raw ).find( '/' );
				if ( slash != string::npos )
					rawKey = FileUtil::normalizeSeparators( raw ).substr( slash + 1 );
				found = ResourceUtilInternal::tryUnderRoot( domainRoot, rawKey, folderName );
			}
#endif
		}
		else
		{
			found = ResourceUtilInternal::tryResolveAmong( _s_resourceFolderList, lowerFile, lowerFolder );
#if defined( SW_PLATFORM_LINUX )
			if ( found.empty() && ( lowerFile != filePath || ( folderName.empty() == false && lowerFolder != folderName ) ) )
				found = ResourceUtilInternal::tryResolveAmong( _s_resourceFolderList, filePath, folderName );
#endif
		}

		if ( found.empty() == false )
		{
			std::unique_lock<std::shared_mutex> lock( ResourceUtilInternal::_s_pathCacheMutex );
			ResourceUtilInternal::_s_mapResolvedPaths.insert_or_assign( std::move( cacheKey ), found );
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
			if ( ResourceUtilInternal::mapGlobalIdToRoot( lowerRel, domainRoot, keyUnderRoot ) )
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

	string ResourceUtil::getActivePackFolderPath()
	{
		const string& packRoot = GameConfig::getActive()._packRoot;
		if ( packRoot.empty() )
			return {};

		const string absPath = makeAbsolutePath( packRoot );
		if ( absPath.empty() == false )
			return FileUtil::normalizeSeparators( absPath );

		const string& resourceRoot = getRootFolderPath();
		if ( resourceRoot.empty() )
			return {};
		return FileUtil::normalizeSeparators( FileUtil::joinPath( resourceRoot, packRoot ) );
	}

	string ResourceUtil::joinActivePackPath( string_view relativeUnderPack )
	{
		const string packFolder = getActivePackFolderPath();
		if ( packFolder.empty() )
			return {};
		if ( relativeUnderPack.empty() )
			return packFolder;
		return FileUtil::joinPath( packFolder, relativeUnderPack );
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

	bool ResourceUtil::setSearchPriority( const vector<string>& listPriority )
	{
		if ( listPriority.empty() )
			return false;

		_s_listSearchPriority = listPriority;

		if ( _s_resourceRootFolderPath.empty() )
			return true;

		_s_resourceFolderList.clear();
		_s_resourceFolderList.reserve( 16 );

		const string& resourceRoot = _s_resourceRootFolderPath;

		for ( const string& tokenRaw : listPriority )
		{
			const string token = FileUtil::normalizePath( tokenRaw );
			if ( token == "game" )
			{
				const string activePack = getActivePackFolderPath();
				if ( activePack.empty() == false && FileUtil::directoryExists( activePack ) )
				{
					const string normPack = FileUtil::normalizeSeparators( activePack );
					if ( std::find( _s_resourceFolderList.begin(), _s_resourceFolderList.end(), normPack ) == _s_resourceFolderList.end() )
						_s_resourceFolderList.push_back( normPack );
				}

				if ( _s_gameFolderPath.empty() == false && FileUtil::directoryExists( _s_gameFolderPath ) )
				{
					vector<string> listPackFolders;
					FileUtil::collectFolders( _s_gameFolderPath, listPackFolders, false, false );
					for ( const string& packFolder : listPackFolders )
					{
						const string normPack = FileUtil::normalizeSeparators( packFolder );
						if ( std::find( _s_resourceFolderList.begin(), _s_resourceFolderList.end(), normPack ) == _s_resourceFolderList.end() )
						{
							_s_resourceFolderList.push_back( normPack );
						}
					}
				}
			}
			else if ( token == "common" )
			{
				if ( _s_commonFolderPath.empty() == false && FileUtil::directoryExists( _s_commonFolderPath ) )
				{
					const string norm = FileUtil::normalizeSeparators( _s_commonFolderPath );
					if ( std::find( _s_resourceFolderList.begin(), _s_resourceFolderList.end(), norm ) == _s_resourceFolderList.end() )
						_s_resourceFolderList.push_back( norm );
				}
			}
			else if ( token == "engine" )
			{
				if ( _s_engineFolderPath.empty() == false && FileUtil::directoryExists( _s_engineFolderPath ) )
				{
					const string norm = FileUtil::normalizeSeparators( _s_engineFolderPath );
					if ( std::find( _s_resourceFolderList.begin(), _s_resourceFolderList.end(), norm ) == _s_resourceFolderList.end() )
						_s_resourceFolderList.push_back( norm );
				}
			}
			else if ( token == "editor" )
			{
				if ( _s_editorFolderPath.empty() == false && FileUtil::directoryExists( _s_editorFolderPath ) )
				{
					const string norm = FileUtil::normalizeSeparators( _s_editorFolderPath );
					if ( std::find( _s_resourceFolderList.begin(), _s_resourceFolderList.end(), norm ) == _s_resourceFolderList.end() )
						_s_resourceFolderList.push_back( norm );
				}
			}
			else
			{
				// 커스텀 팩 / DLC / 모드 경로 (예: "dlc/expansion1", "mods/pack1")
				const string customPath = FileUtil::joinPath( resourceRoot, token );
				if ( FileUtil::directoryExists( customPath ) )
				{
					const string norm = FileUtil::normalizeSeparators( customPath );
					if ( std::find( _s_resourceFolderList.begin(), _s_resourceFolderList.end(), norm ) == _s_resourceFolderList.end() )
						_s_resourceFolderList.push_back( norm );
				}
			}
		}

		clearPathCache();
		return true;
	}

	const vector<string>& ResourceUtil::getSearchPriority()
	{
		return _s_listSearchPriority;
	}

	const vector<string>& ResourceUtil::getDefaultSearchPriority()
	{
		static const vector<string> s_listDefaultPriority = EngineConfig{}._listResourcePriority;
		return s_listDefaultPriority;
	}

	void ResourceUtil::clearPathCache()
	{
		std::unique_lock<std::shared_mutex> lock( ResourceUtilInternal::_s_pathCacheMutex );
		ResourceUtilInternal::_s_mapResolvedPaths.clear();
	}

	string ResourceUtil::makeSaveFolderPath( string_view absoluteFolder )
	{
		const string folderNorm = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( absoluteFolder ) );

		string physicalRoot;
		string rootNorm;

		for ( const string& root : _s_resourceFolderList )
		{
			ResourceUtilInternal::considerSaveRoot( folderNorm, root, physicalRoot, rootNorm );
		}
		ResourceUtilInternal::considerSaveRoot( folderNorm, _s_gameFolderPath, physicalRoot, rootNorm );
		ResourceUtilInternal::considerSaveRoot( folderNorm, _s_engineFolderPath, physicalRoot, rootNorm );
		ResourceUtilInternal::considerSaveRoot( folderNorm, _s_commonFolderPath, physicalRoot, rootNorm );
		ResourceUtilInternal::considerSaveRoot( folderNorm, _s_editorFolderPath, physicalRoot, rootNorm );

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
				result = FileUtil::joinPath( result, rel );
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
				result = FileUtil::joinPath( result, lowerName );
		}
		return result;
	}

	atomic<bool> ResourceUtil::_s_bInitialize{ false };

	string ResourceUtil::_s_projectFolderPath;

	string ResourceUtil::_s_resourceRootFolderPath;

	string ResourceUtil::_s_engineFolderPath;

	string ResourceUtil::_s_commonFolderPath;

	string ResourceUtil::_s_gameFolderPath;

	string ResourceUtil::_s_editorFolderPath;

	vector<string> ResourceUtil::_s_listSearchPriority;

	vector<string> ResourceUtil::_s_resourceFolderList;

} // namespace sw
