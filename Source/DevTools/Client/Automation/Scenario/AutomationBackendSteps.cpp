#include "pch.h"

#include "DevTools/Client/Automation/Scenario/AutomationBackendSteps.h"

#include "DevTools/Client/Automation/Scenario/AutomationStepValidation.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepParams.h"

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

    expected<void> AutomationBackendSteps::validateSwapBackend( const AutomationStep& step )
    {
        AutomationSwapBackendParams params;
        const expected<void>        read = step.readFields( params );
        if ( read.has_value() == false )
            return read;
        RHIBackend backend{};
        const bool bRelative = params._backend == kOtherBackend || params._backend == kStartBackend;
        if ( bRelative == false && tryParseBackend( params._backend, backend ) == false )
        {
            // 받는 이름 목록은 쿠킹 표에서 만든다(글자 리터럴을 이어 붙인다) — 손으로 적은 목록이 `wgpu` 를 빠뜨렸다.
#define SW_SWAP_BACKEND_HINT_ROW( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) CommandLineName "|"
            return unexpected( Error{ ErrorKind::InvalidArgument, "needs backend=\"" SW_RHI_BACKEND_TABLE( SW_SWAP_BACKEND_HINT_ROW ) "other|start\"", params._backend } );
#undef SW_SWAP_BACKEND_HINT_ROW
        }
        return {};
    }

    // App 이 올라오지 않은 프로세스(nogpu 테스트)도 이 단계의 속성을 검사할 수 있게, App 의 단계 등록이 받는 것과 같은 검사 함수를 따로 올린다.
    SW_AUTOMATION_STEP_VALIDATOR( swapBackend, AutomationBackendSteps::kSwapBackendKind, &AutomationBackendSteps::validateSwapBackend );
} // namespace sw
