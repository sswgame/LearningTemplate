// ShaderCookCoverageTest — 엔진 기본 에셋의 셰이더 칸은 모두 쿠킹 요청에 든다(nogpu).
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Renderer/Cook/ShaderCookRequestCollector.h"

using namespace sw;

/**
 * @brief [ShaderCookCoverageTest] `EngineDefaultAssets` 의 `_shader*` 칸(DataPath)이 가리키는 셰이더는 모두 쿠킹 요청에 있다.
 * @details 패스가 아닌 엔진 셰이더 목록은 손으로 적은 표라, 새 칸(`_shaderLayerBake`)을 더하고 표를 고치지 않으면 개발 빌드(런타임 컴파일)는 지나가고
 *          Shipping 만 "Precompiled shader binary not found" 로 진다. 칸을 리플렉션으로 훑어 빠짐을 여기서 잡는다.
 */
/**
 * @brief [ShaderCookCoverageTest] 파이프라인 XML 이 낸 요청은 그 파이프라인의 에셋 id(리소스 루트 기준)를 출처로, 패스 종류 표의 요청은 빈 출처(늘 필요)를 든다
 * @details 팩 쿠커는 출처가 닿은 요청만 "버리면 안 되는" 바이너리로 본다(`PackCooker::isRequestedByReachedOrigin`) — 개발용 GI 파이프라인의 GTAO 는 출처가 닿지 않으면 버려도 된다.
 */
SW_TEST_CASE( ShaderCookCoverageTest, RequestsCarryTheirAssetOrigin )
{
    vector<ShaderCookRequest> listRequest;
    const string              engineRoot = FileUtil::joinPath( engine::getSubsystem<AssetPaths>().getRootFolderPath(), "engine" );
    SW_REQUIRE_TRUE( ShaderCookRequestCollector::collectAllRequests( engineRoot, listRequest ) );
    bool bGTAOFromPipeline = false;
    bool bTableRequest     = false;
    for ( const ShaderCookRequest& request : listRequest )
    {
        SW_EXPECT_FALSE( request._listOrigin.empty() );
        if ( request._shaderPath == "engine/shaders/gtao.hlsl" )
            bGTAOFromPipeline = bGTAOFromPipeline || VectorUtil::contains( request._listOrigin, vector<string>{ "engine/pipeline/deferredpipeline_gi.xml" } );
        bTableRequest = bTableRequest || VectorUtil::contains( request._listOrigin, vector<string>{} );
    }
    SW_EXPECT_TRUE( bGTAOFromPipeline );
    SW_EXPECT_TRUE( bTableRequest );
}

SW_TEST_CASE( ShaderCookCoverageTest, EveryEngineShaderFieldIsCooked )
{
    const unique_ptr<EngineDefaultAssets> pAssets = EngineDefaultAssets::load();
    SW_REQUIRE_NOT_NULL( pAssets.get() );
    vector<ShaderCookRequest> listRequest;
    // 엔진 도메인만 훑는다(파이프라인 XML 의 전체 화면 패스 기본 셰이더까지 — 전체 콘텐츠의 머티리얼은 느리다)
    const string engineRoot = FileUtil::joinPath( engine::getSubsystem<AssetPaths>().getRootFolderPath(), "engine" );
    SW_REQUIRE_TRUE( ShaderCookRequestCollector::collectAllRequests( engineRoot, listRequest ) );

    uint32 shaderFieldCount = 0;
    for ( const PropertyInfo& property : EngineDefaultAssets::StaticType()->getPropertiesWithBase() )
    {
        const string_view name = property._name.view();
        if ( property._typeName != hashed_string( "DataPath" ) || name.substr( 0, 7 ) != "_shader" )
            continue;
        ++shaderFieldCount;
        const DataPath* pPath = property.getValuePtr<DataPath>( static_cast<const void*>( pAssets.get() ) );
        SW_REQUIRE_NOT_NULL( pPath );
        const string normPath = FileUtil::normalizeSeparators( pPath->get() );
        bool         bCooked  = false;
        for ( const ShaderCookRequest& request : listRequest )
        {
            bCooked = bCooked || request._shaderPath == normPath;
        }
        const string message = string( name ) + " (" + normPath + ") is not in the shader cook requests";
        SW_EXPECT_TRUE_MSG( bCooked, message.c_str() );
    }
    SW_EXPECT_TRUE( shaderFieldCount > 10u );
}
