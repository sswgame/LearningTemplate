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
        string_view        _arrAlias[4];
    };

    constexpr ContractBackendRow kArrContractBackend[] = {
#define SW_TEST_BACKEND_ROW( Backend, ShaderFolder, ShaderTarget, Argument, ... ) \
    { RHIBackend::Backend, ShaderFolder, ShaderTargetFormat::ShaderTarget, { __VA_ARGS__ } },
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

    /** @brief `-<alias>` 하나만 준 명령줄이 고르는 백엔드입니다. 고르지 않으면 false 입니다. */
    bool findBackendForFlag( string_view alias, RHIBackend& outBackend )
    {
        CommandLineManager commandLineManager;
        commandLineManager.initialize();
        string flag = "-";
        flag += alias;
        utf8* argv[] = {
            const_cast<utf8*>( "TestApp.exe" ),
            flag.data(),
        };
        commandLineManager.parse( 2, argv );
        return RHIBackendUtil::findCommandLineBackend( commandLineManager, outBackend );
    }
} // namespace

/**
 * @brief [CookContractTest] 쿠킹 표의 백엔드 별칭은 명령줄 플래그 · 셰이더 폴더 역산에서 모두 그 백엔드를 고른다
 * @details 별칭을 읽는 셋 — `ArgumentList.xxx`(명령줄), `ShaderCooker::getFormatForSubfolder`, `CookAssets.py` — 이 `Config/Engine/CookContract.json` 의
 *          같은 줄을 읽는다. 따로 들면 한쪽(예: 명령줄이 `-directx11` · `-directx12` · `-spirv` 를 모르는 식)이 어긋난다.
 */
SW_TEST_CASE( CookContractTest, EveryBackendAliasSelectsItsBackend )
{
    SW_EXPECT_EQUAL( static_cast<size_t>( ShaderTargetFormat::Count ), std::size( kArrContractBackend ) );
    for ( const ContractBackendRow& row : kArrContractBackend )
    {
        SW_EXPECT_EQUAL( row._shaderFolder, ShaderCooker::getSubfolderForFormat( row._shaderTarget ) );
        uint32 aliasCount = 0;
        for ( const string_view alias : row._arrAlias )
        {
            if ( alias.empty() )
                continue;
            ++aliasCount;
            SW_EXPECT_TRUE_MSG( ShaderCooker::getFormatForSubfolder( alias ) == row._shaderTarget, alias.data() );

            RHIBackend backend = RHIBackend::DirectX11;
            const bool bChosen = findBackendForFlag( alias, backend );
            SW_EXPECT_TRUE_MSG( bChosen, alias.data() );
            SW_EXPECT_TRUE_MSG( bChosen && backend == row._backend, alias.data() );
        }
        SW_EXPECT_TRUE( aliasCount > 0 );
    }

    // 표 밖의 이름은 아무 백엔드도 고르지 않는다.
    SW_EXPECT_TRUE( ShaderCooker::getFormatForSubfolder( "metal" ) == ShaderTargetFormat::Count );
    RHIBackend backend = RHIBackend::DirectX11;
    SW_EXPECT_FALSE( findBackendForFlag( "metal", backend ) );
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
