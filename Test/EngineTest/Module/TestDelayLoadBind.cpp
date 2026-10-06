/**
 * @file TestDelayLoadBind.cpp
 * @brief 지연 로드 훅을 넣은 모듈(키트 · 게임)의 지연 import 는 그 코드가 돌기 전에 묶여 있다.
 * @details lld 의 x64 지연 로드 썽크는 xmm0 을 `__delayLoadHelper2` 호출의 홈 공간에 저장해, 첫 호출이 묶으면 그 호출의 첫 float 인자가
 *          망가진다(`DamageMath::applyArmor` 의 첫 호출이 damage = 0 을 받았다). 엔진 기동 · 모듈을 올리는 자리가 미리 묶는다
 *          (`ModuleImageUtil::bindDelayLoadImports`).
 */
#include "pch.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Container/vector.h"
#include "Core/Module/ModuleImageUtil.h"
#include "Core/String/StringUtil.h"

#include "TestFramework/TestFramework.h"

// 모듈 지연 로드는 Windows 개발 빌드에만 있다(배포본은 키트 · 게임을 정적으로 링크한다) — 그 밖의 구성에는 볼 것이 없어 케이스를 두지 않는다
// (모두 건너뛰는 스위트는 실패로 친다).
#if defined( SW_PLATFORM_WINDOWS ) && !defined( SW_SHIPPING )
namespace
{
    struct DelayLoadBindTestInternal
    {
        /** @brief 지연 import 칸 가운데 아직 이미지 안(지연 로드 썽크)을 가리키는 것의 수 — 묶이지 않은 import 입니다. */
        static uint32 countUnboundDelayImports( HMODULE hModule )
        {
            const uint8*            pBase = reinterpret_cast<const uint8*>( hModule );
            const IMAGE_DOS_HEADER* pDos  = reinterpret_cast<const IMAGE_DOS_HEADER*>( pBase );
            const IMAGE_NT_HEADERS* pNt   = reinterpret_cast<const IMAGE_NT_HEADERS*>( pBase + pDos->e_lfanew );
            const uintptr_t         begin = reinterpret_cast<uintptr_t>( pBase );
            const uintptr_t         end   = begin + pNt->OptionalHeader.SizeOfImage;

            const IMAGE_DATA_DIRECTORY& delayDir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
            if ( delayDir.VirtualAddress == 0 )
                return 0;
            uint32 unboundCount{ 0 };
            for ( const IMAGE_DELAYLOAD_DESCRIPTOR* pDesc = reinterpret_cast<const IMAGE_DELAYLOAD_DESCRIPTOR*>( pBase + delayDir.VirtualAddress ); pDesc->DllNameRVA != 0;
                  ++pDesc )
            {
                if ( pDesc->Attributes.RvaBased == 0 )
                    continue;
                for ( const uintptr_t* pEntry = reinterpret_cast<const uintptr_t*>( pBase + pDesc->ImportAddressTableRVA ); *pEntry != 0; ++pEntry )
                {
                    if ( begin <= *pEntry && *pEntry < end )
                        ++unboundCount;
                }
            }
            return unboundCount;
        }
    };
} // namespace

/**
 * @brief [DelayLoadBindTest] 엔진 기동 뒤, 지연 로드 훅을 넣은 모든 모듈의 지연 import 가 묶여 있다 — 첫 호출이 썽크를 지나지 않는다
 * @details 시험 실행 파일은 키트를 링크해 OS 로더가 함께 올린다. 엔진 기동(`EngineBootstrap::initialize`)이 그런 모듈을 미리 묶는다.
 */
SW_TEST_CASE( DelayLoadBindTest, EveryDelayImportIsBoundAfterEngineStartup )
{
    sw::vector<void*> listHandle;
    sw::ModuleImageUtil::collectLoadedModuleHandles( listHandle );
    SW_ASSERT_TRUE( listHandle.empty() == false );

    uint32 hookedModuleCount{ 0 };
    for ( void* pHandle : listHandle )
    {
        const HMODULE hModule = static_cast<HMODULE>( pHandle );
        if ( GetProcAddress( hModule, sw::ModuleImageUtil::kBindDelayLoadImportsSymbol ) == nullptr )
            continue;
        ++hookedModuleCount;
        utf16 arrModulePath[MAX_PATH]{};
        (void)GetModuleFileNameW( hModule, arrModulePath, MAX_PATH );
        SW_EXPECT_TRUE_MSG( DelayLoadBindTestInternal::countUnboundDelayImports( hModule ) == 0u, sw::StringUtil::utf16ToUtf8( arrModulePath ).c_str() );
    }
    SW_EXPECT_TRUE_MSG( hookedModuleCount > 0u, "no module with the delay-load hook is loaded - this executable links the kits, so the hook TU went missing" );
}
#endif
