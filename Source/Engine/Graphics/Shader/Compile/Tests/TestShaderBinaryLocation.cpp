#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Compile/ShaderSource.h"

using namespace sw;

namespace
{
    /** @brief @p sourceID 의 DX12 바이너리 폴더입니다. `shaders` 칸이 없으면 빈 글입니다. */
    string makeDX12BinDirectory( string_view sourceID )
    {
        const std::optional<ShaderBinaryLocation> location = ShaderBinaryLocation::make( ShaderSourceID{ sourceID }, ShaderTargetFormat::DXIL_D3D12 );
        return location.has_value() ? location->_binDirectory : string{};
    }
} // namespace

SW_TEST_CASE( ShaderBinaryLocationTest, BinFolderSitsUnderTheFirstShadersComponent )
{
    const string rhi = string( ShaderCooker::getSubfolderForFormat( ShaderTargetFormat::DXIL_D3D12 ) );
    SW_EXPECT_EQUAL( makeDX12BinDirectory( "engine/shaders/forward_lit.hlsl" ), string( "engine/shaders/bin/" + rhi ) );
    SW_EXPECT_EQUAL( makeDX12BinDirectory( "engine/shaders/sub/x.hlsl" ), string( "engine/shaders/bin/" + rhi ) );
    SW_EXPECT_EQUAL( makeDX12BinDirectory( "game/shooter3d/shaders/x.hlsl" ), string( "game/shooter3d/shaders/bin/" + rhi ) );
    SW_EXPECT_EQUAL( makeDX12BinDirectory( "mounted/myshaders/shaders/x.hlsl" ), string( "mounted/myshaders/shaders/bin/" + rhi ) ); // `myshaders` 는 shaders 칸이 아니다
}

SW_TEST_CASE( ShaderBinaryLocationTest, IdWithoutADomainShadersFolderHasNoLocation )
{
    SW_EXPECT_TRUE( makeDX12BinDirectory( "shaders/x.hlsl" ).empty() );          // 도메인 없음
    SW_EXPECT_TRUE( makeDX12BinDirectory( "engine/myshaders/x.hlsl" ).empty() ); // shaders 칸 없음
    SW_EXPECT_TRUE( makeDX12BinDirectory( "engine/shaders" ).empty() );          // 파일 없음
    SW_EXPECT_TRUE( makeDX12BinDirectory( "" ).empty() );
}
