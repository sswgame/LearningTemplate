#include "pch.h"

#if defined( SW_PLATFORM_WINDOWS ) && defined( _MSC_VER )
	#include "Engine/Utility/Module/LiveReloadManager.h"

	#include "Core/File/FileUtil.h"
	#include "Core/Common/PlatformOsHeaders.h"

namespace sw
{
	namespace
	{
		FARPROC WINAPI notifyHook( uint32 dliNotify, DelayLoadInfo* pdli )
		{
			if ( pdli == nullptr || pdli->szDll == nullptr )
				return nullptr;

			if ( dliNotify == dliNotePreLoadLibrary || dliNotify == dliFailLoadLib )
			{
				const string_view  dllName{ pdli->szDll };
				LiveReloadManager* pMgr = LiveReloadManager::getDelayLoadManager();
				if ( pMgr != nullptr && pMgr->isGraphBroken() == false )
				{
					string_view fileName;
					FileUtil::getFileNamePart( dllName, fileName );
					string_view stem;
					FileUtil::removeExtension( fileName, stem );
					void* pHandle = pMgr->getModuleHandle( stem );
					if ( pHandle != nullptr )
						return reinterpret_cast<FARPROC>( pHandle );
				}

				// Fallback: LiveReloadManager가 비활성 상태인 경우 실행 파일 디렉터리(Bin)에서 DLL 직접 로드
				const string binDir	  = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );
				const string fullPath = FileUtil::joinPath( binDir, dllName );
				if ( FileUtil::fileExists( fullPath ) )
				{
					void* pHandle = FileUtil::loadDynamicLibrary( fullPath );
					if ( pHandle != nullptr )
						return reinterpret_cast<FARPROC>( pHandle );
				}
			}
			return nullptr;
		}
	} // namespace

	extern "C" const PfnDliHook __pfnDliNotifyHook2	 = notifyHook;
	extern "C" const PfnDliHook __pfnDliFailureHook2 = notifyHook;
} // namespace sw
#endif
