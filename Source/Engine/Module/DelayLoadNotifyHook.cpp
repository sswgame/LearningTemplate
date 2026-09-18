#include "pch.h"

#if defined( SW_PLATFORM_WINDOWS ) && defined( _MSC_VER )
    // 이 파일은 include 를 **전부 `#if` 안**에 두므로 `CheckIncludeOrder` 가 보지 못한다
    // (게이트는 첫 `#if` 를 경계로 삼는다). 순서는 손으로 지킨다: Core → Engine.
    #include "Core/Common/PlatformOsHeaders.h"
    #include "Core/File/FileUtil.h"

    #include "Engine/Module/ModuleHandleProvider.h"

namespace sw
{
    namespace
    {
        FARPROC WINAPI notifyHook( uint32 dliNotify, DelayLoadInfo* pPdli )
        {
            if ( pPdli == nullptr || pPdli->szDll == nullptr )
                return nullptr;

            if ( dliNotify == dliNotePreLoadLibrary || dliNotify == dliFailLoadLib )
            {
                const string_view      dllName{ pPdli->szDll };
                IModuleHandleProvider* pProvider = engine::getModuleHandleProvider();
                if ( pProvider != nullptr && pProvider->isModuleGraphBroken() == false )
                {
                    string_view fileName;
                    FileUtil::getFileNamePart( dllName, fileName );
                    string_view stem;
                    FileUtil::removeExtension( fileName, stem );
                    void* pHandle = pProvider->findLoadedModuleHandle( stem );
                    if ( pHandle != nullptr )
                        return reinterpret_cast<FARPROC>( pHandle );
                }

                // Fallback: 제공자가 없거나(Shipping · 리로드 비활성) 그래프가 깨진 경우 실행 파일 디렉터리(Bin)에서 DLL 직접 로드
                const string binDir   = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );
                const string fullPath = FileUtil::joinPath( binDir, dllName );
                if ( FileUtil::fileExists( fullPath ) )
                {
                    void* pHandle = FileUtil::loadDynamicLibrary( fullPath );
                    if ( pHandle != nullptr )
                        return reinterpret_cast<FARPROC>( pHandle );
                }
                SW_LOG_ERROR( "DelayLoad failed to find/load DLL: '%#' (fullPath: '%#')", dllName, fullPath );
            }
            else if ( dliNotify == dliFailGetProc )
            {
                if ( pPdli->dlp.fImportByName )
                    SW_LOG_ERROR( "DelayLoad failed to find procedure '%#' in '%#'", pPdli->dlp.szProcName, pPdli->szDll );
                else
                    SW_LOG_ERROR( "DelayLoad failed to find procedure ordinal %# in '%#'", pPdli->dlp.dwOrdinal, pPdli->szDll );
            }
            return nullptr;
        }
    } // namespace

    extern "C" const PfnDliHook __pfnDliNotifyHook2  = notifyHook;
    extern "C" const PfnDliHook __pfnDliFailureHook2 = notifyHook;
} // namespace sw
#endif
