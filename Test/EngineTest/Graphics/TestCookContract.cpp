#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "Engine/Config/RHIBackendType.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Resource/AssetFormat.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/CookContract.gen.h"

using namespace sw;

namespace
{
    /** @brief 생성 헤더(`Config/Engine/CookContract.json`)의 백엔드 한 줄입니다. */
    struct ContractBackendRow
    {
        RHIBackend         _backend;
        string_view        _shaderFolder;
        ShaderTargetFormat _shaderTarget;
        string_view        _commandLineName;
    };

    constexpr ContractBackendRow kArrContractBackend[] = {
#define SW_TEST_BACKEND_ROW( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) \
    { RHIBackend::Backend, ShaderFolder, ShaderTargetFormat::ShaderTarget, CommandLineName },
        SW_RHI_BACKEND_TABLE( SW_TEST_BACKEND_ROW )
#undef SW_TEST_BACKEND_ROW
    };

    /** @brief 생성 헤더의 쿡 접미사 한 줄입니다. */
    struct ContractCookSuffixRow
    {
        string_view _source;
        string_view _cooked;
        AssetKind   _kind;
        bool        _bAuthoringSource;
    };

    constexpr ContractCookSuffixRow kArrContractCookSuffix[] = {
#define SW_TEST_COOK_SUFFIX_ROW( SourceSuffix, CookedSuffix, Kind, bAuthoringSource ) { SourceSuffix, CookedSuffix, AssetKind::Kind, bAuthoringSource },
        SW_COOK_SUFFIX_TABLE( SW_TEST_COOK_SUFFIX_ROW )
#undef SW_TEST_COOK_SUFFIX_ROW
    };

    /** @brief `-<name>` 하나만 준 명령줄이 고르는 백엔드입니다. 고르지 않으면 false 입니다. */
    bool findBackendForFlag( string_view name, RHIBackend& outBackend )
    {
        CommandLineManager commandLineManager;
        commandLineManager.initialize();
        string flag = "-";
        flag += name;
        utf8* argv[] = {
            const_cast<utf8*>( "TestApp.exe" ),
            flag.data(),
        };
        commandLineManager.parse( 2, argv );
        return RHIBackendUtil::findCommandLineBackend( commandLineManager, outBackend );
    }
} // namespace

/**
 * @brief [CookContractTest] 쿠킹 표의 백엔드마다 명령줄 이름 하나가 그 백엔드를 고르고, 셰이더 폴더 이름이 그 타깃으로 풀린다
 * @details 명령줄(`ArgumentList.xxx`) · 셰이더 폴더 역산(`ShaderCooker::getFormatForSubfolder`) · `CookAssets.py` 가 `Config/Engine/CookContract.json` 의
 *          같은 줄을 읽는다. 철자는 백엔드마다 하나다 — 옛 철자(`-vulkan` · `-directx12` · `-spirv`)는 아무 백엔드도 고르지 않는다.
 */
SW_TEST_CASE( CookContractTest, EveryBackendCommandLineNameSelectsItsBackend )
{
    SW_EXPECT_EQUAL( static_cast<size_t>( ShaderTargetFormat::Count ), std::size( kArrContractBackend ) );
    for ( const ContractBackendRow& row : kArrContractBackend )
    {
        SW_EXPECT_EQUAL( row._shaderFolder, ShaderCooker::getSubfolderForFormat( row._shaderTarget ) );
        SW_EXPECT_TRUE_MSG( ShaderCooker::getFormatForSubfolder( row._shaderFolder ) == row._shaderTarget, row._shaderFolder.data() );
        SW_EXPECT_FALSE( row._commandLineName.empty() );

        RHIBackend backend = RHIBackend::DirectX11;
        const bool bChosen = findBackendForFlag( row._commandLineName, backend );
        SW_EXPECT_TRUE_MSG( bChosen, row._commandLineName.data() );
        SW_EXPECT_TRUE_MSG( bChosen && backend == row._backend, row._commandLineName.data() );
    }

    // 표 밖의 이름 · 옛 철자는 아무 백엔드도 고르지 않는다.
    SW_EXPECT_TRUE( ShaderCooker::getFormatForSubfolder( "metal" ) == ShaderTargetFormat::Count );
    for ( const string_view name : { "metal", "vulkan", "directx12", "d3d11", "spirv", "opengl" } )
    {
        RHIBackend backend = RHIBackend::DirectX11;
        SW_EXPECT_FALSE_MSG( findBackendForFlag( name, backend ), name.data() );
    }
}

/**
 * @brief [CookContractTest] 쿠킹 표의 접미사마다 쿠킹본 이름과 저작 소스 판정이 표와 같다
 * @details Python 쿠커는 같은 표로 소스 트리에 남은 산출물을 알아본다(`isCookedArtifact`). 엔진이 만드는 이름과 쿠커가 알아보는 이름이
 *          한 표에서 나온다.
 */
SW_TEST_CASE( CookContractTest, EveryCookSuffixMapsToItsCookedName )
{
    for ( const ContractCookSuffixRow& row : kArrContractCookSuffix )
    {
        string sourcePath = "maps/probe";
        sourcePath += row._source;
        string cookedPath = "maps/probe";
        cookedPath += row._cooked;

        SW_EXPECT_EQUAL( cookedPath, AssetCookPath::toCookedPath( sourcePath ) );
        SW_EXPECT_TRUE_MSG( AssetCookPath::isCookableSource( sourcePath ) == row._bAuthoringSource, sourcePath.c_str() );
        SW_EXPECT_TRUE_MSG( AssetCookPath::isCookableSource( sourcePath, row._kind ) == row._bAuthoringSource, sourcePath.c_str() );
    }
}
