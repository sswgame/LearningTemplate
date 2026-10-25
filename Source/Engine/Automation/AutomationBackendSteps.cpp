#include "pch.h"

#include "Engine/Automation/AutomationBackendSteps.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Core/Container/StringUtil.h"

    #include "Engine/Automation/AutomationScenario.h"
    #include "Engine/Automation/AutomationStepValidation.h"

namespace sw
{
    bool AutomationBackendSteps::tryParseBackend( string_view text, RHIBackend& outBackend )
    {
        struct NameRow
        {
            const utf8* _pName;
            RHIBackend  _backend;
        };
        static constexpr NameRow kArrName[] = {
            {     "dx11", RHIBackend::DirectX11},
            {"directx11", RHIBackend::DirectX11},
            {     "dx12", RHIBackend::DirectX12},
            {"directx12", RHIBackend::DirectX12},
            {   "vulkan",    RHIBackend::Vulkan},
            {       "vk",    RHIBackend::Vulkan},
            {   "opengl",    RHIBackend::OpenGL},
            {       "gl",    RHIBackend::OpenGL},
        };
        for ( const NameRow& row : kArrName )
        {
            if ( StringUtil::equals( text, row._pName, true ) )
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
            outError = step.describe() + ": needs backend=\"dx11|dx12|vulkan|opengl|other|start\"";
            return false;
        }
        return true;
    }

    // App 이 올라오지 않은 프로세스(nogpu 테스트)도 이 단계의 속성을 검사할 수 있게, App 의 단계 등록이 받는 것과 같은 검사 함수를 따로 올린다.
    SW_AUTOMATION_STEP_VALIDATOR( swapBackend, AutomationBackendSteps::kSwapBackendKind, &AutomationBackendSteps::validateSwapBackend );
} // namespace sw

#endif
