#include "pch.h"

#include "Engine/Resource/ResourcePackManager.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
	namespace
	{
		SW_LOG_CALLER( "ResourcePackManager" );

		struct ResourcePackManagerInternal
		{
			/**
			 * @brief 팩 파일명이 우선순위 토큰과 일치하는지 대소문자 무시로 판별합니다 (Zero Allocation).
			 * @param stem 팩 파일명 (확장자 제외, 예: "game", "game_demo", "dlc_expansion")
			 * @param token 검색 우선순위 토큰 (예: "game", "dlc", "engine")
			 */
			static bool matchesTokenCaseInsensitive( string_view stem, string_view token )
			{
				if ( stem.empty() || token.empty() )
					return false;

				if ( StringUtil::equals( stem, token, true ) )
					return true;

				// token + '_' 또는 token + '.' 접두사 매칭
				if ( stem.size() > token.size() && StringUtil::startsWith( stem, token, true ) )
				{
					const utf8 delimiter = stem[token.size()];
					if ( delimiter == '_' || delimiter == '.' )
						return true;
				}

				return false;
			}

			/**
			 * @brief EngineConfig 리소스 우선순위 목록(listPriority)을 기반으로 팩의 기본 마운트 우선순위를 동적 계산합니다 (Zero Allocation).
			 */
			static int32 calculatePackDefaultPriority( string_view packFileName, const vector<string>& listPriority )
			{
				string_view fileName;
				FileUtil::getFileNamePart( packFileName, fileName );
				string_view stem;
				FileUtil::removeExtension( fileName, stem );
				if ( stem.empty() )
					return 0;

				const vector<string>& listPriorityEffective = listPriority.empty() ? ResourceUtil::getDefaultSearchPriority() : listPriority;
				const size_t		  priorityCount			= listPriorityEffective.size();

				// 1. listPriorityEffective 순서에 맞춰 일치하는 토큰 검색 (앞쪽 인덱스일수록 높은 우선순위)
				for ( size_t index = 0; index < priorityCount; ++index )
				{
					const string_view token{ listPriorityEffective[index] };
					if ( matchesTokenCaseInsensitive( stem, token ) )
					{
						return static_cast<int32>( ( priorityCount - index ) * 1000 );
					}
				}

				// 2. 패치 접두사(patch_)가 붙은 경우 대상 모듈 우선순위 + 500 가중치 부여
				if ( stem.size() >= 6 && StringUtil::startsWith( stem, "patch_", true ) )
				{
					const string_view subStem = stem.substr( 6 );
					for ( size_t index = 0; index < priorityCount; ++index )
					{
						const string_view token{ listPriorityEffective[index] };
						if ( matchesTokenCaseInsensitive( subStem, token ) )
						{
							return static_cast<int32>( ( priorityCount - index ) * 1000 ) + 500;
						}
					}
					// 모듈 미지정 전체 단독 핫픽스 팩 (최상위 우선순위)
					return static_cast<int32>( ( priorityCount + 1 ) * 1000 );
				}

				// 3. 일치하는 우선순위 토큰이 없는 일반 팩 기본 우선순위
				return 0;
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	ResourcePackManager::ResourcePackManager()
		: _vfsMutex{}
		, _listMountedPack{}
		, _dlcValidator{}
		, _bAllowLooseFiles{ false }
	{
	}

	ResourcePackManager::~ResourcePackManager()
	{
		unmountAll();
	}

	ResourcePackManager::ResourcePackManager( ResourcePackManager&& other ) noexcept
	{
		std::lock_guard<mutex> lock( other._vfsMutex );
		_listMountedPack  = std::move( other._listMountedPack );
		_dlcValidator	  = std::move( other._dlcValidator );
		_bAllowLooseFiles = other._bAllowLooseFiles;
	}

	ResourcePackManager& ResourcePackManager::operator=( ResourcePackManager&& other ) noexcept
	{
		if ( this != &other )
		{
			std::scoped_lock<mutex, mutex> lock( _vfsMutex, other._vfsMutex );
			_listMountedPack  = std::move( other._listMountedPack );
			_dlcValidator	  = std::move( other._dlcValidator );
			_bAllowLooseFiles = other._bAllowLooseFiles;
		}
		return *this;
	}

	bool ResourcePackManager::mountPack( string_view packFilePath, int32 priority )
	{
		if ( packFilePath.empty() )
			return false;

		const string normalizedPath = FileUtil::normalizeSeparators( packFilePath );

		int32 effectivePriority = priority;
		if ( effectivePriority <= 0 )
		{
			effectivePriority = ResourcePackManagerInternal::calculatePackDefaultPriority( packFilePath, ResourceUtil::getSearchPriority() );
		}

		auto pReader = make_unique<ResourcePackReader>();
		if ( pReader->open( normalizedPath ) == false )
		{
			SW_LOG_ERROR( "Failed to open and mount pack: %#", packFilePath );
			return false;
		}

		// DLC 소유권 인증 검사
		const uint32 dlcAppId = pReader->getDlcAppId();
		if ( dlcAppId > 0 )
		{
			if ( _dlcValidator.isBound() && _dlcValidator( dlcAppId ) == false )
			{
				SW_LOG_WARNING( "Access denied for DLC pack %# (DLC AppID %# not owned)", packFilePath, dlcAppId );
				return false;
			}
		}

		{
			std::lock_guard<mutex> lock( _vfsMutex );

			// 이미 마운트되어 있다면 이전 팩 제거
			for ( auto it = _listMountedPack.begin(); it != _listMountedPack.end(); ++it )
			{
				if ( it->_pReader != nullptr && FileUtil::pathsEqualNormalized( it->_pReader->getPackPath(), normalizedPath ) )
				{
					_listMountedPack.erase( it );
					break;
				}
			}

			MountedPack mounted{};
			mounted._priority = effectivePriority;
			mounted._pReader  = std::move( pReader );

			_listMountedPack.push_back( std::move( mounted ) );

			// 우선순위 내림차순 정렬 (높은 priority가 앞쪽)
			std::stable_sort( _listMountedPack.begin(), _listMountedPack.end(), []( const MountedPack& a, const MountedPack& b )
			{
				return a._priority > b._priority;
			} );
		}

		SW_LOG_INFO( "Mounted resource pack: %# (Priority: %#)", normalizedPath, effectivePriority );
		return true;
	}

	bool ResourcePackManager::unmountPack( string_view packFilePath )
	{
		if ( packFilePath.empty() )
			return false;

		std::lock_guard<mutex> lock( _vfsMutex );
		for ( auto it = _listMountedPack.begin(); it != _listMountedPack.end(); ++it )
		{
			if ( it->_pReader != nullptr && FileUtil::pathsEqualNormalized( it->_pReader->getPackPath(), packFilePath ) )
			{
				it->_pReader->close();
				_listMountedPack.erase( it );
				SW_LOG_INFO( "Unmounted resource pack: %#", packFilePath );
				return true;
			}
		}
		return false;
	}

	void ResourcePackManager::unmountAll()
	{
		std::lock_guard<mutex> lock( _vfsMutex );
		for ( auto& mounted : _listMountedPack )
		{
			if ( mounted._pReader != nullptr )
			{
				mounted._pReader->close();
			}
		}
		_listMountedPack.clear();
	}

	bool ResourcePackManager::hasFile( string_view relativePath ) const
	{
		if ( relativePath.empty() )
			return false;

		const uint64 pathHash = StringUtil::computeHash64( relativePath );

		std::lock_guard<mutex> lock( _vfsMutex );
		for ( const auto& mounted : _listMountedPack )
		{
			if ( mounted._pReader != nullptr && mounted._pReader->hasFile( pathHash ) )
			{
				return true;
			}
		}
		return false;
	}

	bool ResourcePackManager::readFile( string_view relativePath, vector<uint8>& outBytes ) const
	{
		if ( relativePath.empty() )
			return false;

		const uint64 pathHash = StringUtil::computeHash64( relativePath );

		std::lock_guard<mutex> lock( _vfsMutex );
		for ( const auto& mounted : _listMountedPack )
		{
			if ( mounted._pReader != nullptr && mounted._pReader->hasFile( pathHash ) )
			{
				if ( mounted._pReader->readFile( pathHash, outBytes ) )
				{
					return true;
				}
			}
		}
		return false;
	}

	bool ResourcePackManager::readTextFile( string_view relativePath, string& outText, string* pOutMountedPackPath ) const
	{
		if ( relativePath.empty() )
			return false;

		const uint64 pathHash = StringUtil::computeHash64( relativePath );

		std::lock_guard<mutex> lock( _vfsMutex );
		for ( const auto& mounted : _listMountedPack )
		{
			if ( mounted._pReader != nullptr && mounted._pReader->hasFile( pathHash ) )
			{
				if ( mounted._pReader->readTextFile( relativePath, outText ) )
				{
					if ( pOutMountedPackPath != nullptr )
					{
						*pOutMountedPackPath = mounted._pReader->getPackPath();
					}
					return true;
				}
			}
		}
		return false;
	}

	void ResourcePackManager::setDlcEntitlementValidator( DlcEntitlementDelegate validator )
	{
		std::lock_guard<mutex> lock( _vfsMutex );
		_dlcValidator = std::move( validator );
	}

	void ResourcePackManager::setAllowLooseFiles( bool bAllow )
	{
		_bAllowLooseFiles = bAllow;
	}

	bool ResourcePackManager::isAllowLooseFiles() const
	{
		return _bAllowLooseFiles;
	}

	size_t ResourcePackManager::getMountedPackCount() const
	{
		std::lock_guard<mutex> lock( _vfsMutex );
		return _listMountedPack.size();
	}

	bool ResourcePackManager::scanAndMountPacks( string_view packsDirectory, const vector<string>& listPriority )
	{
		if ( packsDirectory.empty() || FileUtil::directoryExists( packsDirectory ) == false )
			return false;

		vector<string> listCandidateFile;
		FileUtil::collectFiles( packsDirectory, "", listCandidateFile, false, true );

		if ( listCandidateFile.empty() )
			return false;

		bool bAnyMounted = false;
		for ( const string& packPath : listCandidateFile )
		{
			if ( FileUtil::hasAnyExtension( packPath, { ".pack", ".swpk" } ) == false )
				continue;

			const int32 priority = ResourcePackManagerInternal::calculatePackDefaultPriority( packPath, listPriority );
			if ( mountPack( packPath, priority ) )
			{
				bAnyMounted = true;
			}
		}

		return bAnyMounted;
	}

} // namespace sw
