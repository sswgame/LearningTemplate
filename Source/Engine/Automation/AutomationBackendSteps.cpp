#include "pch.h"

#include "Engine/Automation/AutomationBackendSteps.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Engine/Automation/AutomationScenario.h"
    #include "Engine/Automation/AutomationStepValidation.h"

    #include "sw/config/CookContract.gen.h"

namespace sw
{
    bool AutomationBackendSteps::tryParseBackend( string_view text, RHIBackend& outBackend )
    {
        struct NameRow
        {
            const utf8* _pName;
            RHIBackend  _backend;
        };
        // 이름은 쿠킹 표(`SW_RHI_BACKEND_TABLE`)의 명령줄 이름 하나뿐이다(`-vk` 와 같은 철자) — 별칭(`vulkan` · `directx12`)과 대소문자 무시는 두지 않는다.
        static constexpr NameRow kArrName[] = {
    #define SW_SWAP_BACKEND_NAME_ROW( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) { CommandLineName, RHIBackend::Backend },
            SW_RHI_BACKEND_TABLE( SW_SWAP_BACKEND_NAME_ROW )
    #undef SW_SWAP_BACKEND_NAME_ROW
        };
        for ( const NameRow& row : kArrName )
        {
            if ( text == row._pName )
            {
                outBackend = row._backend;
                return true;
            }
        }
        return false;
    }

    bool AutomationBackendSteps::validateSwapBackend( const AutomationStep& step, string& outError )
    {
        for ( const AutomationAttribute& attribute : step._listAttribute )
        {
            if ( string_view{ attribute._name } != "backend" )
            {
                outError = step.describe() + ": unknown attribute '" + attribute._name + "'";
                return false;
            }
        }
        const string* pBackend = step.findAttribute( "backend" );
        RHIBackend    backend{};
        const bool    bRelative = pBackend != nullptr && ( *pBackend == kOtherBackend || *pBackend == kStartBackend );
        if ( pBackend == nullptr || ( bRelative == false && tryParseBackend( *pBackend, backend ) == false ) )
        {
            outError = step.describe() + ": needs backend=\"dx11|dx12|vk|gl|other|start\"";
            return false;
        }
        return true;
    }

    // App 이 올라오지 않은 프로세스(nogpu 테스트)도 이 단계의 속성을 검사할 수 있게, App 의 단계 등록이 받는 것과 같은 검사 함수를 따로 올린다.
    SW_AUTOMATION_STEP_VALIDATOR( swapBackend, AutomationBackendSteps::kSwapBackendKind, &AutomationBackendSteps::validateSwapBackend );
} // namespace sw

#endif
