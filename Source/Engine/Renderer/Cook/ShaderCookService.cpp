#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"

#include "Engine/Graphics/Shader/Compile/IShaderCookService.h"
#include "Engine/Graphics/Shader/Compile/ShaderCookStamp.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Renderer/Cook/ShaderCookDriver.h"

namespace sw
{
    namespace
    {
        /** @brief 셰이더 쿠킹 창구(`IShaderCookService`)의 렌더러 구현입니다. 상태가 없다 — 쿠커(`ShaderCookDriver`)에 넘기기만 한다. */
        class ShaderCookService final : public IShaderCookService
        {
        public:
            bool cookAllShaders( string_view cookedDir, ShaderTargetFormat format, ShaderCookScope scope ) override
            {
                const ShaderCookSummary summary = ShaderCookDriver::cookAllShaders( cookedDir, {}, format, false, scope );
                ShaderCookStamp::invalidateSharedHeaderCache();
                return summary.isClean();
            }

            bool verifyCookedShaders( string_view cookedDir, ShaderTargetFormat format ) override
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

        ShaderCookService                      s_shaderCookService;
        const IShaderCookService::Registration s_shaderCookServiceRegistration{ s_shaderCookService };
    } // namespace
} // namespace sw
