#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"

#include "DevTools/Client/Cook/ShaderCookDriver.h"
#include "DevTools/Cook/IShaderCookService.h"

#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"

namespace sw
{
    namespace
    {
        /** @brief 셰이더 쿠킹 창구(`IShaderCookService`)의 렌더러 구현입니다. 상태가 없다 — 쿠커(`ShaderCookDriver`)에 넘기기만 한다. */
        class ShaderCookService final : public IShaderCookService
        {
        public:
            bool cookAllShaders( const AbsolutePath& cookedDir, ShaderTargetFormat format, ShaderCookScope scope ) override
            {
                const ShaderCookSummary summary = ShaderCookDriver::cookAllShaders( cookedDir, {}, format, scope );
                return summary.isClean();
            }

            bool verifyCookedShaders( const AbsolutePath& cookedDir, ShaderTargetFormat format ) override
            {
                vector<string> listProblem;
                ShaderCookDriver::verifyCookedShaders( cookedDir, format, listProblem );
                for ( const string& problem : listProblem )
                {
                    SW_LOG_ERROR_ID( "Headless.ShaderCookProblem", "[%#] %#", ShaderCooker::getSubfolderForFormat( format ), problem );
                }
                return listProblem.empty();
            }

            bool collectRequestedBinaries( ShaderTargetFormat format, unordered_map<string, vector<vector<string>>>& outMapBinaryOrigin ) override
            {
                return ShaderCookDriver::collectRequestedBinaries( format, outMapBinaryOrigin );
            }
        };

        ShaderCookService                   s_shaderCookService;
        const IShaderCookService::Registrar s_shaderCookServiceRegistrar{ s_shaderCookService };
    } // namespace
} // namespace sw
