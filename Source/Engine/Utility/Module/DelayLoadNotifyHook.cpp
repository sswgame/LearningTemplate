#include "pch.h"

#if defined( SW_PLATFORM_WINDOWS ) && defined( _MSC_VER )
	#include "Engine/Utility/Module/LiveReloadManager.h"

	#include "Core/File/FileUtil.h"

	#include <delayimp.h>

namespace sw
{
	namespace
	{
		FARPROC WINAPI notifyHook( uint32 dliNotify, DelayLoadInfo* pdli )
		{
			if ( dliNotify != dliNotePreLoadLibrary || pdli == nullptr )
				return nullptr;
			LiveReloadManager* pMgr = LiveReloadManager::getDelayLoadManager();
			if ( pMgr == nullptr || pMgr->isGraphBroken() )
				return nullptr;

			const string_view dllName = pdli->szDll != nullptr ? string_view{ pdli->szDll } : string_view{};
			void*			  pHandle = pMgr->getModuleHandle( FileUtil::removeExtension( FileUtil::getFileNamePart( dllName ) ) );
			if ( pHandle == nullptr )
				return nullptr;
			return reinterpret_cast<FARPROC>( pHandle );
		}
	} // namespace

	extern "C" const PfnDliHook __pfnDliNotifyHook2 = notifyHook;
} // namespace sw
#endif
