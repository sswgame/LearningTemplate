#include "pch.h"

#include "Engine/Utility/Resource/ResourceUtil.h"

#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"

namespace sw
{
	namespace
	{
		struct ResourceUtilInternal
		{
			inline static mutex							_s_pathCacheMutex{};
			inline static unordered_map<uint64, string> _s_mapResolvedPath{};

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
			 * @param listRoot getResourcePath 검색 루트들
			 * @param relFile 상대 파일 키
			 * @param relFolder 선택적 하위 폴더
			 * @return 첫 번째로 찾은 절대 경로. 없으면 empty.
			 */
			static string tryResolveAmong( const vector<string>& listRoot, string_view relFile, string_view relFolder )
			{
				for ( const string& root : listRoot )
				{
					const string hit = tryUnderRoot( root, relFile, relFolder );
					if ( hit.empty() == false )
						return hit;
				}
				return {};
			}

			/**
			 * @brief 저장 대상 폴더가 지정한 루트 아래인지 판정하고, 더 긴(구체적인) 일치를 유지합니다.
			 */
			static void considerSaveRoot( string_view folderNorm, string_view candidateRoot, string& outPhysicalRoot, string& outRootNorm )
			{
				if ( candidateRoot.empty() )
					return;
				const string norm = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( candidateRoot ) );
				if ( norm.empty() )
					return;
				if ( FileUtil::startsWithPathComponent( folderNorm, norm ) )
				{
					if ( norm.size() > outRootNorm.size() )
					{
						outRootNorm		= norm;
						outPhysicalRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( string{ candidateRoot } ) );
					}
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
			std::lock_guard<mutex> lock( ResourceUtilInternal::_s_pathCacheMutex );
			ResourceUtilInternal::_s_mapResolvedPath.clear();
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

		uint64 cacheKeyHash = StringUtil::computeHash64( filePath );
		if ( folderName.empty() == false )
		{
			cacheKeyHash = StringUtil::computeHash64( folderName, true, cacheKeyHash );
		}

		{
			std::lock_guard<mutex> lock( ResourceUtilInternal::_s_pathCacheMutex );
			auto				   it = ResourceUtilInternal::_s_mapResolvedPath.find( cacheKeyHash );
			if ( it != ResourceUtilInternal::_s_mapResolvedPath.end() )
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
			found = ResourceUtilInternal::tryResolveAmong( _s_listResourceFolder, lowerFile, lowerFolder );
#if defined( SW_PLATFORM_LINUX )
			if ( found.empty() && ( lowerFile != filePath || ( folderName.empty() == false && lowerFolder != folderName ) ) )
				found = ResourceUtilInternal::tryResolveAmong( _s_listResourceFolder, filePath, folderName );
#endif
		}

		if ( found.empty() == false )
		{
			std::lock_guard<mutex> lock( ResourceUtilInternal::_s_pathCacheMutex );
			ResourceUtilInternal::_s_mapResolvedPath.insert_or_assign( cacheKeyHash, found );
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
										 string* pOutAbsPath )
	{
		if ( relativePath.empty() )
			return false;

		const string normalizedKey = FileUtil::normalizePath( relativePath );

		// 1. 낱개 파일 우선 로드가 켜져 있는 경우, 디스크 파일 먼저 확인
		if ( _s_packManager.isAllowLooseFiles() )
		{
			const string absPath = getResourcePath( relativePath );
			if ( absPath.empty() == false && FileUtil::fileExists( absPath ) )
			{
				if ( pOutAbsPath != nullptr )
					*pOutAbsPath = absPath;
				return FileUtil::readTextFile( absPath, outText );
			}
		}

		// 2. VFS 마운트된 팩들에서 O(1) 해시 룩업 및 압축 해제 읽기
		string mountedPackPath;
		if ( _s_packManager.readTextFile( normalizedKey, outText, &mountedPackPath ) )
		{
			if ( pOutAbsPath != nullptr )
				*pOutAbsPath = "[" + FileUtil::getFileNamePart( mountedPackPath ) + "]:" + normalizedKey;
			return true;
		}

		// 3. 디스크 fallback 읽기
		string absPath = getResourcePath( relativePath );
		if ( absPath.empty() )
			absPath = string( relativePath );
		if ( pOutAbsPath != nullptr )
			*pOutAbsPath = absPath;
		return FileUtil::readTextFile( absPath, outText );
	}

	bool ResourceUtil::readBinaryResource( string_view relativePath, vector<uint8>& outBytes )
	{
		if ( relativePath.empty() )
			return false;

		const string normalizedKey = FileUtil::normalizePath( relativePath );

		if ( _s_packManager.isAllowLooseFiles() )
		{
			const string absPath = getResourcePath( relativePath );
			if ( absPath.empty() == false && FileUtil::fileExists( absPath ) )
			{
				return FileUtil::readFile( absPath, outBytes );
			}
		}

		if ( _s_packManager.readFile( normalizedKey, outBytes ) )
		{
			return true;
		}

		const string absPath = getResourcePath( relativePath );
		if ( absPath.empty() == false )
		{
			return FileUtil::readFile( absPath, outBytes );
		}
		return FileUtil::readFile( relativePath, outBytes );
	}

	bool ResourceUtil::mountPack( string_view packFilePath, int32 priority )
	{
		clearPathCache();
		return _s_packManager.mountPack( packFilePath, priority );
	}

	bool ResourceUtil::unmountPack( string_view packFilePath )
	{
		clearPathCache();
		return _s_packManager.unmountPack( packFilePath );
	}

	void ResourceUtil::unmountAllPacks()
	{
		clearPathCache();
		_s_packManager.unmountAll();
	}

	void ResourceUtil::setAllowLooseFiles( bool bAllow )
	{
		clearPathCache();
		_s_packManager.setAllowLooseFiles( bAllow );
	}

	bool ResourceUtil::isAllowLooseFiles()
	{
		return _s_packManager.isAllowLooseFiles();
	}

	ResourcePackManager& ResourceUtil::getPackManager()
	{
		return _s_packManager;
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
		if ( relativeUnderPack.empty() )
			return getActivePackFolderPath();

		const string packFolder = getActivePackFolderPath();
		if ( packFolder.empty() )
			return {};
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
		vector<string> listFolder;
		const string   lowerName = FileUtil::normalizePath( folderName );

		for ( const string& resourceFolder : _s_listResourceFolder )
		{
			const string folderPath = FileUtil::joinPath( resourceFolder, lowerName );
			if ( FileUtil::directoryExists( folderPath ) )
				listFolder.push_back( FileUtil::normalizeSeparators( folderPath ) );

#if defined( SW_PLATFORM_LINUX )
			if ( lowerName != folderName )
			{
				const string rawPath = FileUtil::joinPath( resourceFolder, folderName );
				if ( FileUtil::directoryExists( rawPath ) )
					listFolder.push_back( FileUtil::normalizeSeparators( rawPath ) );
			}
#endif
		}
		return listFolder;
	}

	bool ResourceUtil::setSearchPriority( const vector<string>& listPriority )
	{
		if ( listPriority.empty() )
			return false;

		_s_listSearchPriority = listPriority;

		if ( _s_resourceRootFolderPath.empty() )
			return true;

		_s_listResourceFolder.clear();
		_s_listResourceFolder.reserve( 16 );

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
					if ( std::find( _s_listResourceFolder.begin(), _s_listResourceFolder.end(), normPack ) == _s_listResourceFolder.end() )
						_s_listResourceFolder.push_back( normPack );
				}
				if ( _s_gameFolderPath.empty() == false && FileUtil::directoryExists( _s_gameFolderPath ) )
				{
					vector<string> listPackFolder;
					FileUtil::collectFolders( _s_gameFolderPath, listPackFolder, false, false );
					for ( const string& packFolder : listPackFolder )
					{
						const string normPack = FileUtil::normalizeSeparators( packFolder );
						if ( std::find( _s_listResourceFolder.begin(), _s_listResourceFolder.end(), normPack ) == _s_listResourceFolder.end() )
						{
							_s_listResourceFolder.push_back( normPack );
						}
					}
					const string norm = FileUtil::normalizeSeparators( _s_gameFolderPath );
					if ( std::find( _s_listResourceFolder.begin(), _s_listResourceFolder.end(), norm ) == _s_listResourceFolder.end() )
						_s_listResourceFolder.push_back( norm );
				}
			}
			else if ( token == "common" )
			{
				if ( _s_commonFolderPath.empty() == false && FileUtil::directoryExists( _s_commonFolderPath ) )
				{
					const string norm = FileUtil::normalizeSeparators( _s_commonFolderPath );
					if ( std::find( _s_listResourceFolder.begin(), _s_listResourceFolder.end(), norm ) == _s_listResourceFolder.end() )
						_s_listResourceFolder.push_back( norm );
				}
			}
			else if ( token == "engine" )
			{
				if ( _s_engineFolderPath.empty() == false && FileUtil::directoryExists( _s_engineFolderPath ) )
				{
					const string norm = FileUtil::normalizeSeparators( _s_engineFolderPath );
					if ( std::find( _s_listResourceFolder.begin(), _s_listResourceFolder.end(), norm ) == _s_listResourceFolder.end() )
						_s_listResourceFolder.push_back( norm );
				}
			}
			else if ( token == "editor" )
			{
				if ( _s_editorFolderPath.empty() == false && FileUtil::directoryExists( _s_editorFolderPath ) )
				{
					const string norm = FileUtil::normalizeSeparators( _s_editorFolderPath );
					if ( std::find( _s_listResourceFolder.begin(), _s_listResourceFolder.end(), norm ) == _s_listResourceFolder.end() )
						_s_listResourceFolder.push_back( norm );
				}
			}
			else
			{
				// 커스텀 팩 / DLC / 모드 경로 (예: "dlc/expansion1", "mods/pack1")
				const string customPath = FileUtil::joinPath( resourceRoot, token );
				if ( FileUtil::directoryExists( customPath ) )
				{
					const string norm = FileUtil::normalizeSeparators( customPath );
					if ( std::find( _s_listResourceFolder.begin(), _s_listResourceFolder.end(), norm ) == _s_listResourceFolder.end() )
						_s_listResourceFolder.push_back( norm );
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
		std::lock_guard<mutex> lock( ResourceUtilInternal::_s_pathCacheMutex );
		ResourceUtilInternal::_s_mapResolvedPath.clear();
	}

	string ResourceUtil::makeSaveFolderPath( string_view absoluteFolder )
	{
		const string folderNorm = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( absoluteFolder ) );

		string physicalRoot;
		string rootNorm;

		for ( const string& root : _s_listResourceFolder )
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

	vector<string> ResourceUtil::_s_listResourceFolder;

	ResourcePackManager ResourceUtil::_s_packManager;

} // namespace sw
