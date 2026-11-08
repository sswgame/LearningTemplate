#include "pch.h"

#include "Engine/Graphics/Shader/Compile/IShaderCookService.h"

#include "Core/Registry/RegistrationSlot.h"

namespace sw
{
    namespace
    {
        struct ShaderCookServiceInternal
        {
            /** @brief 걸린 창구입니다(이미지에 하나 — 둘째는 거절). 함수 정적이라 등록자의 정적 초기화 순서와 무관하다. */
            static SW_DEFINE_REGISTRATION_SLOT( getSlot, IShaderCookService, "ShaderCookService" )
        };
    } // namespace

    IShaderCookService* IShaderCookService::find()
    {
        return ShaderCookServiceInternal::getSlot().get();
    }

    bool IShaderCookService::attach( IShaderCookService* pService )
    {
        return ShaderCookServiceInternal::getSlot().setLogged( pService, "Shader cook service", "ShaderCookService" ) == RegistrationResult::Added;
    }

    void IShaderCookService::detach( IShaderCookService* pService )
    {
        (void)ShaderCookServiceInternal::getSlot().reset( pService );
    }
} // namespace sw
