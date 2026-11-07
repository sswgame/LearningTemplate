#include "pch.h"

#include "Engine/Graphics/Shader/Compile/IShaderCookService.h"

namespace sw
{
    namespace
    {
        struct ShaderCookServiceInternal
        {
            /** @brief 등록된 창구입니다. 함수 안 정적 변수라 등록자의 정적 초기화 순서와 무관하다. */
            static IShaderCookService*& slot()
            {
                static IShaderCookService* s_pService = nullptr;
                return s_pService;
            }
        };
    } // namespace

    IShaderCookService* IShaderCookService::find()
    {
        return ShaderCookServiceInternal::slot();
    }

    IShaderCookService::Registration::Registration( IShaderCookService& service )
    {
        SW_ASSERT( ShaderCookServiceInternal::slot() == nullptr ); // 이미지에 하나다 — 둘이면 등록의 잘못이다
        ShaderCookServiceInternal::slot() = &service;
    }
} // namespace sw
