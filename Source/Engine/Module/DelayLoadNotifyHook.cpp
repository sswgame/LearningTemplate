#include "pch.h"

#if defined( SW_PLATFORM_WINDOWS )
    // 이 파일은 include 를 **전부 `#if` 안**에 두므로 `CheckIncludeOrder` 가 보지 못한다
    // (게이트는 첫 `#if` 를 경계로 삼는다). 순서는 손으로 지킨다: Core → Engine.
    #include "Core/Common/PlatformOsHeaders.h"
    #include "Core/File/FileUtil.h"
    #include "Core/Module/ModuleImageUtil.h"

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

                // 폴백: 제공자가 없거나(Shipping · 리로드 비활성) 그래프가 깨졌으면 실행 파일 디렉터리(Bin)에서 DLL 을 직접 로드한다
                const string binDir   = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );
                const string fullPath = FileUtil::joinPath( binDir, dllName );
                if ( FileUtil::exists( fullPath ) )
                {
                    void* pHandle = ModuleImageUtil::loadDynamicLibrary( fullPath );
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

extern "C" __declspec( dllexport ) uint32 bindDelayLoadImports();

/**
 * @brief 이 모듈 이미지의 지연 import 를 **전부 지금** 묶고, 묶지 못한 DLL 수를 반환합니다(`ModuleImageUtil::bindDelayLoadImports` 가 부른다).
 * @details **지연 import 는 첫 호출로 묶이게 두면 안 된다.** lld 의 x64 지연 로드 썽크(`__tailMerge_<dll>`)는 xmm0~3 을 `sub rsp,48h` 뒤
 *          `[rsp]` · `[rsp+10h]` · `[rsp+20h]` · `[rsp+30h]` 에 두고 `__delayLoadHelper2` 를 부르는데, `[rsp..rsp+1Fh]` 는 그 호출의 홈(shadow) 공간이라
 *          헬퍼가 rcx · rdx 를 흘려 놓으며 저장된 xmm0 을 덮는다 — 그 함수의 **첫 호출에서 첫 float 인자가 쓰레기**가 된다
 *          (`DamageMath::applyArmor( 25, 5, 0, 1 )` 의 첫 호출이 damage = 0 을 받았다). 모듈을 올린 직후 여기서 묶어 두면 썽크가 실제 인자를 들고 지나가는 일이 없다.
 *          묶기는 표준 `__HrLoadAllImportsForDll` 이 하고(같은 알림 훅을 지난다), 지연 import 표는 이 이미지의 것을 훑는다.
 */
extern "C" __declspec( dllexport ) uint32 bindDelayLoadImports()
{
    // 이 함수가 든 이미지 — 훅 TU 는 지연 로드를 쓰는 모듈마다 따로 들어가므로 그 모듈 자신이다.
    HMODULE hSelf = nullptr;
    if ( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCSTR>( &bindDelayLoadImports ), &hSelf ) == FALSE )
        return 1;
    const uint8*                pBase    = reinterpret_cast<const uint8*>( hSelf );
    const IMAGE_NT_HEADERS*     pNt      = reinterpret_cast<const IMAGE_NT_HEADERS*>( pBase + reinterpret_cast<const IMAGE_DOS_HEADER*>( pBase )->e_lfanew );
    const IMAGE_DATA_DIRECTORY& delayDir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if ( delayDir.VirtualAddress == 0 )
        return 0;

    uint32 failedCount{ 0 };
    for ( const IMAGE_DELAYLOAD_DESCRIPTOR* pDesc = reinterpret_cast<const IMAGE_DELAYLOAD_DESCRIPTOR*>( pBase + delayDir.VirtualAddress ); pDesc->DllNameRVA != 0; ++pDesc )
    {
        if ( pDesc->Attributes.RvaBased == 0 )
            continue;
        if ( FAILED( __HrLoadAllImportsForDll( reinterpret_cast<const utf8*>( pBase + pDesc->DllNameRVA ) ) ) )
            ++failedCount;
    }
    return failedCount;
}
#endif
