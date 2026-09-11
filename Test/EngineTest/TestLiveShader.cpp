#include "pch.h"

#include "Engine/Graphics/Shader/Compile/LiveShaderManager.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) LiveShaderTest — 등록과 수동 리로드
// ------------------------------------------------------------------------------
/**
 * @brief [LiveShaderTest] 초기화·등록과 수동 리로드 한 바퀴
 * @details 파일 감시로 **자동** 재컴파일하던 경로는 없앴다. 남은 것은 `ReloadShaders`(Ctrl+F8) 가
 *          부르는 수동 경로뿐이라, 여기서도 `triggerReloadAll` → `update` 로 그 경로를 돈다.
 */

SW_TEST_CASE( LiveShaderTest, InitializationAndManualReload )
{
    const sw::string shaderRelPath = "shaders/testlive.hlsl";
    const sw::string engineFolder  = sw::ResourceUtil::getDomainFolderPath( "engine" );
    const sw::string fullPath      = engineFolder.empty() ? shaderRelPath : sw::FileUtil::joinPath( engineFolder, shaderRelPath );

    sw::FileUtil::createParentDirectory( fullPath );
    sw::FileUtil::writeTextFile( fullPath, "// dummy shader" );

    sw::LiveShaderManager manager;
    manager.initialize();

    sw::ShaderCompileDesc vsDesc{};
    vsDesc._filePath     = shaderRelPath;
    vsDesc._entryPoint   = "main";
    vsDesc._stage        = sw::ShaderStage::Vertex;
    vsDesc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;

    bool                         callbackTriggered{ false };
    sw::ShaderRecompiledDelegate delegate = SW_DELEGATE_LAMBDA( sw::ShaderRecompiledDelegate, [&callbackTriggered]( const std::string_view path, const sw::ShaderCompileResult& result )
    {
        (void)path;
        (void)result;
        callbackTriggered = true;
    } );

    manager.watchShader( vsDesc, delegate );

    // 수동 리로드 경로 — 큐에 넣고(triggerReloadAll) 실제 컴파일은 update 가 한다.
    manager.triggerReloadAll();
    manager.update();

    manager.shutdown();

    sw::FileUtil::removeFile( fullPath );
}
