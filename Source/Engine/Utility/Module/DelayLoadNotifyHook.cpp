#include "pch.h"

#if defined( SW_PLATFORM_WINDOWS ) && defined( _MSC_VER )
	#include "Engine/Utility/Module/LiveReloadManager.h"

	#include <delayimp.h>

namespace sw
{
	namespace
	{
		string basenameNoExt( const utf8* pDllName )
		{
			string		 s	   = pDllName != nullptr ? pDllName : "";
			const size_t slash = s.find_last_of( "/\\" );
			if ( slash != string::npos )
				s = s.substr( slash + 1 );
			if ( s.size() > 4 )
			{
				const string ext = s.substr( s.size() - 4 );
				if ( ext == ".dll" || ext == ".DLL" )
					s = s.substr( 0, s.size() - 4 );
			}
			return s;
		}

		FARPROC WINAPI notifyHook( uint32 dliNotify, DelayLoadInfo* pdli )
		{
			if ( dliNotify != dliNotePreLoadLibrary || pdli == nullptr )
				return nullptr;
			LiveReloadManager* pMgr = LiveReloadManager::getDelayLoadManager();
			if ( pMgr == nullptr || pMgr->isGraphBroken() )
				return nullptr;
			void* pHandle = pMgr->getModuleHandle( basenameNoExt( pdli->szDll ) );
			if ( pHandle == nullptr )
				return nullptr;
			return reinterpret_cast<FARPROC>( pHandle );
		}

	} // namespace
} // namespace sw

extern "C" const PfnDliHook __pfnDliNotifyHook2 = sw::notifyHook;
#endif
