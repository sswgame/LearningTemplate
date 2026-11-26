// 셰이더 요청 기록(`ShaderRequestLog`) — 실행이 쓴 요청을 형식별로 중복 없이 모으고, 자식마다 따로 쓴 파일을 읽을 때 합치고, 소스가 없어진 요청은 뺀다(nogpu · 컴파일 없음).
// 기록대로 컴파일해 셰이더 DDC 가 차는지는 ctest `ShaderCache.WarmFromRequestLog`(Test/Shader/RunShaderWarm.cmake)가 본다 — DDC 폴더를 케이스마다 가를 수 없어 여기서 컴파일하지 않는다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/Shader/Compile/ShaderRequestLog.h"
#include "Engine/Graphics/Shader/Compile/ShaderSource.h"

using namespace sw;

namespace
{
    ShaderCompileDesc makeRequestDesc( const utf8* pSource, ShaderTargetFormat format )
    {
        ShaderCompileDesc desc;
        desc._source       = ShaderSourceID{ pSource };
        desc._entryPoint   = "PSMain";
        desc._stage        = ShaderStage::Pixel;
        desc._targetFormat = format;
        return desc;
    }

    ShaderMacroDefine makeDefine( const utf8* pName, const utf8* pValue )
    {
        return ShaderMacroDefine{ string( pName ), string( pValue ) };
    }
} // namespace

/**
 * @brief [ShaderRequestLogTest] 같은 요청은 한 번만 들고, 대상 형식이 다르거나 define 순서가 다르면 다른 요청이다
 * @details define 순서는 셰이더 DDC 키에 그대로 섞인다 — 기록이 순서를 바꾸면 데운 칸과 실행이 찾는 칸이 어긋난다.
 */
SW_TEST_CASE( ShaderRequestLogTest, DedupesPerFormatAndKeepsDefineOrder )
{
    ShaderRequestLog requestLog;
    SW_EXPECT_TRUE( requestLog.add( makeRequestDesc( "engine/shaders/forward_lit.hlsl", ShaderTargetFormat::SPIRV_Vulkan ) ) );
    SW_EXPECT_FALSE( requestLog.add( makeRequestDesc( "engine/shaders/forward_lit.hlsl", ShaderTargetFormat::SPIRV_Vulkan ) ) );
    SW_EXPECT_TRUE( requestLog.add( makeRequestDesc( "engine/shaders/forward_lit.hlsl", ShaderTargetFormat::DXBC_D3D11 ) ) );

    ShaderCompileDesc first   = makeRequestDesc( "engine/shaders/forward_lit.hlsl", ShaderTargetFormat::DXBC_D3D11 );
    first._listDefine         = { makeDefine( "SW_A", "1" ), makeDefine( "SW_B", "2" ) };
    ShaderCompileDesc swapped = first;
    swapped._listDefine       = { makeDefine( "SW_B", "2" ), makeDefine( "SW_A", "1" ) };
    SW_EXPECT_TRUE( requestLog.add( first ) );
    SW_EXPECT_TRUE( requestLog.add( swapped ) );
    SW_EXPECT_EQUAL( size_t( 4 ), requestLog.size() );
}

/**
 * @brief [ShaderRequestLogTest] 글로 적고 다시 읽으면 같은 요청이 같은 글로 돌아오고, 다른 기록의 글을 읽으면 합집합이 된다(자식마다 따로 쓴 파일을 합치는 길)
 */
SW_TEST_CASE( ShaderRequestLogTest, TextRoundTripsAndMerges )
{
    ShaderCompileDesc withDefine = makeRequestDesc( "engine/shaders/forward_lit.hlsl", ShaderTargetFormat::DXIL_D3D12 );
    withDefine._stage            = ShaderStage::Compute;
    withDefine._entryPoint       = "CSMain";
    withDefine._listDefine       = { makeDefine( "SW_SKINNED", "1" ), makeDefine( "SW_LAYERS", "4" ) };

    ShaderRequestLog first;
    (void)first.add( withDefine );
    (void)first.add( makeRequestDesc( "engine/shaders/decal.hlsl", ShaderTargetFormat::SPIRV_OpenGL ) );
    const string text = first.makeText();

    ShaderRequestLog reread;
    SW_REQUIRE_TRUE( reread.mergeText( text ).has_value() );
    SW_EXPECT_EQUAL( text, reread.makeText() );

    ShaderRequestLog other;
    (void)other.add( makeRequestDesc( "engine/shaders/decal.hlsl", ShaderTargetFormat::SPIRV_OpenGL ) );
    (void)other.add( makeRequestDesc( "engine/shaders/canvas.hlsl", ShaderTargetFormat::DXBC_D3D11 ) );
    SW_REQUIRE_TRUE( other.mergeText( text ).has_value() );
    SW_EXPECT_EQUAL( size_t( 3 ), other.size() );

    bool bFound = false;
    for ( const ShaderCompileDesc& desc : reread.collectDescs() )
    {
        if ( desc._targetFormat != ShaderTargetFormat::DXIL_D3D12 )
            continue;
        bFound = true;
        SW_EXPECT_EQUAL( withDefine._source.str(), desc._source.str() );
        SW_EXPECT_EQUAL( withDefine._entryPoint, desc._entryPoint );
        SW_EXPECT_TRUE( desc._stage == ShaderStage::Compute );
        SW_REQUIRE_TRUE( desc._listDefine.size() == 2 );
        SW_EXPECT_EQUAL( string( "SW_SKINNED" ), desc._listDefine[0]._name );
        SW_EXPECT_EQUAL( string( "4" ), desc._listDefine[1]._value );
        SW_EXPECT_FALSE( desc._enginePolicy.has_value() );
    }
    SW_EXPECT_TRUE( bFound );
}

/**
 * @brief [ShaderRequestLogTest] 머리 줄이 다르거나 줄 하나가 틀리면 오류이고, 그 글의 다른 줄도 더하지 않는다
 * @details 판이 다른 기록을 일부만 읽으면 데우기가 무엇을 빠뜨렸는지 알 수 없다.
 */
SW_TEST_CASE( ShaderRequestLogTest, RejectsWrongHeaderOrBadLineWithoutPartialAdd )
{
    ShaderRequestLog requestLog;
    (void)requestLog.add( makeRequestDesc( "engine/shaders/decal.hlsl", ShaderTargetFormat::SPIRV_Vulkan ) );

    string badLine{ ShaderRequestLog::kHeaderLine };
    badLine += "\nvk\tps\tengine/shaders/canvas.hlsl\tPSMain\nnot-a-format\tps\tengine/shaders/canvas.hlsl\tPSMain\n";
    SW_EXPECT_FALSE( requestLog.mergeText( badLine ).has_value() );
    SW_EXPECT_FALSE( requestLog.mergeText( "SW shader request log 0\n" ).has_value() );
    SW_EXPECT_FALSE( requestLog.mergeText( "" ).has_value() );
    SW_EXPECT_EQUAL( size_t( 1 ), requestLog.size() );
}

/**
 * @brief [ShaderRequestLogTest] 폴더 읽기는 그 폴더의 기록 파일(`*.shaderrequests`)만 합치고, 폴더가 없으면 빈 기록이다(첫 실행 — 데울 것이 없다)
 */
SW_TEST_CASE( ShaderRequestLogTest, ReadFolderMergesChildFiles )
{
    const AbsolutePath               folder  = sw::test::makeScratchPath( "ShaderRequests" );
    const expected<ShaderRequestLog> missing = ShaderRequestLog::readFolder( folder.join( "none" ) );
    SW_REQUIRE_TRUE( missing.has_value() );
    SW_EXPECT_EQUAL( size_t( 0 ), missing->size() );

    ShaderRequestLog dx11Child;
    (void)dx11Child.add( makeRequestDesc( "engine/shaders/decal.hlsl", ShaderTargetFormat::DXBC_D3D11 ) );
    ShaderRequestLog vkChild;
    (void)vkChild.add( makeRequestDesc( "engine/shaders/decal.hlsl", ShaderTargetFormat::SPIRV_Vulkan ) );
    (void)vkChild.add( makeRequestDesc( "engine/shaders/decal.hlsl", ShaderTargetFormat::DXBC_D3D11 ) ); // 백엔드를 바꾼 자식 — 다른 자식과 겹친다
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( folder.join( "Scenario.playground.dx11.shaderrequests" ), dx11Child.makeText() ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( folder.join( "Scenario.playground.vk.shaderrequests" ), vkChild.makeText() ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( folder.join( "notes.txt" ), "not a request log" ) );

    const expected<ShaderRequestLog> merged = ShaderRequestLog::readFolder( folder );
    SW_REQUIRE_TRUE( merged.has_value() );
    SW_EXPECT_EQUAL( size_t( 2 ), merged->size() );
}

/**
 * @brief [ShaderRequestLogTest] 소스가 없어진 요청은 빠지고 그 소스가 한 번 적힌다 — 기록은 지난 실행의 것이라 그 뒤에 지운 셰이더가 남을 수 있다
 */
SW_TEST_CASE( ShaderRequestLogTest, RemoveMissingSourcesSkipsGoneSources )
{
    const optional<ShaderSourceResolver> resolver = ShaderSourceResolver::make( engine::getSubsystem<AssetPaths>() );
    SW_REQUIRE_TRUE( resolver.has_value() );
    vector<ShaderCompileDesc> listDesc = {
        makeRequestDesc( "engine/shaders/gpu_nan_check.hlsl", ShaderTargetFormat::DXIL_D3D12 ),
        makeRequestDesc( "engine/shaders/removed_since_last_run.hlsl", ShaderTargetFormat::DXIL_D3D12 ),
        makeRequestDesc( "engine/shaders/removed_since_last_run.hlsl", ShaderTargetFormat::SPIRV_Vulkan ),
    };
    vector<string> listMissing;
    ShaderRequestLog::removeMissingSources( listDesc, *resolver, listMissing );
    SW_REQUIRE_TRUE( listDesc.size() == 1 );
    SW_EXPECT_EQUAL( string( "engine/shaders/gpu_nan_check.hlsl" ), listDesc[0]._source.str() );
    SW_REQUIRE_TRUE( listMissing.size() == 1 );
    SW_EXPECT_EQUAL( string( "engine/shaders/removed_since_last_run.hlsl" ), listMissing[0] );
}
