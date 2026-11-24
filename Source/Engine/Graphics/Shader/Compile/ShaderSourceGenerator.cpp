#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderSourceGenerator.h"

#include "Core/Foundation/BuildInfo.h"

namespace sw
{
    namespace
    {
        struct ShaderSourceGeneratorInternal
        {
            /** @brief 걸린 생성기들입니다(이름 순). 모듈 로더 스레드의 정적 등록과 쿠킹 스레드의 조회가 겹칠 수 있어 잠금을 든 표다. 함수 정적이라 등록자의 정적 초기화 순서와 무관하다. */
            static SW_DEFINE_LOCKED_REGISTRATION_LIST( getList, const IShaderSourceGenerator, "ShaderSourceGenerator", RegistrationOrder::ByOrderThenName, true )
        };
    } // namespace

    bool IShaderSourceGenerator::add( const IShaderSourceGenerator* pGenerator )
    {
        return ShaderSourceGeneratorInternal::getList().addLogged( pGenerator, "Shader source generator", pGenerator->getName() ) == RegistrationResult::Added;
    }

    void IShaderSourceGenerator::remove( const IShaderSourceGenerator* pGenerator )
    {
        (void)ShaderSourceGeneratorInternal::getList().remove( pGenerator ); // 올라 있지 않으면 할 일이 없다
    }

    bool IShaderSourceGenerator::refreshAllRegistered( const AssetPaths& assetPaths, const AbsolutePath& contentRoot, uint32& outWrittenCount )
    {
        const vector<const IShaderSourceGenerator*> listGenerator = ShaderSourceGeneratorInternal::getList().withLock( []( const RegistrationList<const IShaderSourceGenerator>& list )
        { return list.getItems(); } );
        return refreshGenerators( listGenerator, build::kShipping == false, assetPaths, contentRoot, outWrittenCount );
    }

    bool IShaderSourceGenerator::refreshGenerators( const vector<const IShaderSourceGenerator*>& listGenerator, const bool bRequireGenerator, const AssetPaths& assetPaths,
                                                    const AbsolutePath& contentRoot, uint32& outWrittenCount )
    {
        if ( listGenerator.empty() && bRequireGenerator )
        {
            // 개발 구성인데 생성기가 하나도 없다 — 생성물(머티리얼 그래프의 셰이더)을 맞추지 못한 채 낡은 것으로 굽게 된다. 개발 모듈이 빠진 링크다.
            SW_LOG_ERROR_ID( "ShaderCooker.NoSourceGenerator", "No shader source generator is registered - generated shaders (material graphs) would be cooked stale" );
            return false;
        }
        bool bAllRefreshed = true;
        for ( const IShaderSourceGenerator* pGenerator : listGenerator )
        {
            if ( pGenerator->refreshAll( assetPaths, contentRoot, outWrittenCount ) )
                continue;
            SW_LOG_ERROR_ID( "ShaderCooker.SourceGeneratorFailed", "Shader source generator '%#' failed - its generated shaders would be stale", pGenerator->getName() );
            bAllRefreshed = false;
        }
        return bAllRefreshed;
    }
} // namespace sw
