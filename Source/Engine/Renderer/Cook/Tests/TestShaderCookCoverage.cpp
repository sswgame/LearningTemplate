// ShaderCookCoverageTest — 엔진 기본 에셋의 셰이더 칸은 모두 쿠킹 요청에 든다(nogpu).
#include "pch.h"

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
        const string_view name = property._name.viewOrEmpty();
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
