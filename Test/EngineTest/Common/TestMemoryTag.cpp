/**
 * @file TestMemoryTag.cpp
 * @brief 메모리 용도 태그 — 기동 단계 · 서비스 생성 · 씬 로드(워커) · 메시 생성이 제 줄로 세이는지.
 * @details 태그는 진단 구성(`kMemoryTagScopesEnabled`)에서만 걸린다. 그 밖의 구성에서는 건너뛴다.
 */
#include "pch.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Compression/Lz4CompressionCodec.h"
#include "Engine/Compression/ZlibCompressionCodec.h"
#include "Engine/Compression/ZstdCompressionCodec.h"
#include "Engine/EngineInitSequence.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/Component/SceneTransformStorage.h"
#include "Engine/Object/GameObject/MeshInstanceBatch.h"
#include "Engine/Object/GameObject/PrimitiveRegistry.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/Json/JsonDocument.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief 태그 하나의 "지금 살아 있는 바이트" 입니다. */
    uint64 getLiveBytes( const sw::MemoryProfiler& profiler, sw::MemoryTag tag )
    {
        return profiler.getStats( tag )._currentAllocatedBytes.load();
    }

    /** @brief 태그 하나의 "지금까지 잡은 바이트" 누계입니다(잡았다 놓은 것도 남는다). */
    uint64 getTotalBytes( const sw::MemoryProfiler& profiler, sw::MemoryTag tag )
    {
        return profiler.getStats( tag )._totalAllocatedBytes.load();
    }
} // namespace

/**
 * @brief [MemoryTagTest] 기동 단계마다 Unknown 이 아닌 태그가 있다
 * @details 단계 초기화는 표의 둘째 칸 태그 아래에서 돈다. 새 단계를 Unknown 으로 두면 그 단계의 할당이 모두 Unknown 줄로 간다.
 */
SW_TEST_CASE( MemoryTagTest, EveryStartupStepHasATag )
{
    for ( uint32 stepIndex = 0; stepIndex < static_cast<uint32>( sw::EngineInitStep::Count ); ++stepIndex )
    {
        const sw::EngineInitStep step = static_cast<sw::EngineInitStep>( stepIndex );
        SW_EXPECT_TRUE_MSG( sw::EngineInitSequence::getStepMemoryTag( step ) != sw::MemoryTag::Unknown, sw::EngineInitSequence::getStepName( step ) );
    }
    SW_EXPECT_TRUE( sw::EngineInitSequence::getStepMemoryTag( sw::EngineInitStep::Task ) == sw::MemoryTag::Task );
    SW_EXPECT_TRUE( sw::EngineInitSequence::getStepMemoryTag( sw::EngineInitStep::Reflection ) == sw::MemoryTag::Reflection );
}

/**
 * @brief [MemoryTagTest] 하네스 기동 뒤 주요 태그(태스크 · 리플렉션 · 씬 · 에셋 · 셰이더)에 살아 있는 바이트가 있다
 * @details 하네스는 `EngineLoop` 와 같은 부트스트랩 · 서비스 생성 · 기동 단계 표를 지난다. 서비스 생성(`kServiceMemoryTag`)과 단계 초기화
 *          (`EngineInitStepList.xxx` 의 태그 칸)가 태그를 걸지 않으면 이 줄들이 0 이다.
 */
SW_TEST_CASE( MemoryTagTest, HarnessStartupIsAttributed )
{
    if constexpr ( sw::kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );
    const sw::MemoryProfiler* pProfiler = sw::MemoryProfiler::getActive();
    if ( pProfiler == nullptr )
        SW_TEST_SKIP( "no active memory profiler in this host" );

    SW_EXPECT_TRUE( getLiveBytes( *pProfiler, sw::MemoryTag::Task ) > 0 );
    SW_EXPECT_TRUE( getLiveBytes( *pProfiler, sw::MemoryTag::Reflection ) > 0 );
    SW_EXPECT_TRUE( getLiveBytes( *pProfiler, sw::MemoryTag::Scene ) > 0 );
    SW_EXPECT_TRUE( getLiveBytes( *pProfiler, sw::MemoryTag::Asset ) > 0 );
    SW_EXPECT_TRUE( getLiveBytes( *pProfiler, sw::MemoryTag::Shader ) > 0 );
    SW_EXPECT_TRUE( getLiveBytes( *pProfiler, sw::MemoryTag::EngineMisc ) > 0 );
}

/**
 * @brief [MemoryTagTest] 진단 구성의 부트스트랩이 플랫폼 누수 추적을 켜 둔다
 * @details `EngineBootstrap::initialize( owned, true )` 는 `EngineLoop`(Debug) 와 이 하네스가 함께 부른다. 그래서 이 프로세스의 CRT 상태가
 *          곧 Debug App 의 상태다. 할당 추적 플래그가 켜져 있고, 누수 덤프(`_CRT_WARN`) · CRT 오류(`_CRT_ERROR`)가 디버거 출력과 함께 stderr 로도
 *          나가야 `MemoryProfiler::reportMemoryLeaks` 의 결과가 콘솔 · CI 로그에 남는다.
 */
SW_TEST_CASE( MemoryTagTest, DiagnosticBootstrapEnablesPlatformLeakChecks )
{
#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_DEBUG ) && !defined( SW_SHIPPING ) && !defined( SW_SANITIZER_ADDRESS )
    const int32 debugFlags = _CrtSetDbgFlag( _CRTDBG_REPORT_FLAG );
    SW_EXPECT_TRUE( ( debugFlags & _CRTDBG_ALLOC_MEM_DF ) != 0 );
    SW_EXPECT_TRUE( ( debugFlags & _CRTDBG_LEAK_CHECK_DF ) == 0 );
    SW_EXPECT_EQUAL( _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG, _CrtSetReportMode( _CRT_WARN, _CRTDBG_REPORT_MODE ) );
    SW_EXPECT_EQUAL( _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG, _CrtSetReportMode( _CRT_ERROR, _CRTDBG_REPORT_MODE ) );
#else
    SW_TEST_SKIP( "platform leak tracking state is the Windows Debug CRT (no ASan); other configurations have nothing to query" );
#endif
}

/**
 * @brief [MemoryTagTest] 워커에서 짓는 씬(비동기 로드)의 할당이 Scene 으로 세인다
 * @details 로드는 `SceneManager::dispatchLoad` 가 만든 태스크가 워커에서 한다(문서 읽기 · GameObject 생성). 태스크는 만든 쪽의 태그를
 *          상속하므로 진입점(`dispatchLoad`)의 Scene 이 워커까지 간다. 씬 채택(`tickTransitions`)을 부르기 **전에** 재서 워커 몫만 본다.
 *          이 시험 스레드의 태그는 Unknown 이라, 기다리며 이 스레드가 대신 실행해도 진입점이 없으면 Scene 이 늘지 않는다.
 */
SW_TEST_CASE( MemoryTagTest, AsyncSceneLoadOnWorkerIsTaggedScene )
{
    if constexpr ( sw::kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );
    const sw::MemoryProfiler* pProfiler = sw::MemoryProfiler::getActive();
    if ( pProfiler == nullptr || pProfiler->isTrackingEnabled() == false )
        SW_TEST_SKIP( "no tracking memory profiler in this host" );

    const sw::string xmlPath = test::makeTempPath( "sw_test_memtag_scene.scene.xml" );
    sw::string       xmlText = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Scene formatVersion=\"1\" name=\"TagTown\">\n  <entities>\n";
    for ( uint32 entityIndex = 0; entityIndex < 64; ++entityIndex )
    {
        xmlText += "    <entity id=\"" + sw::to_string( entityIndex + 1 ) + "\" name=\"Villager" + sw::to_string( entityIndex ) + "\"/>\n";
    }
    xmlText += "  </entities>\n</Scene>\n";
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath, reinterpret_cast<const uint8*>( xmlText.data() ), static_cast<uint64>( xmlText.size() ) ) );

    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    const sw::MemoryTag outerTag = sw::MemoryProfiler::getCurrentMemoryTag();
    SW_ASSERT_TRUE( outerTag == sw::MemoryTag::Unknown );

    const uint64               sceneBefore = getTotalBytes( *pProfiler, sw::MemoryTag::Scene );
    sw::TaskFuture<sw::Scene*> future      = manager.requestLoadFuture( xmlPath );
    SW_ASSERT_TRUE( future.isValid() );
    SW_EXPECT_TRUE( sw::engine::getTaskManager().waitAll( 5000 ) );
    const uint64 sceneAfterWorker = getTotalBytes( *pProfiler, sw::MemoryTag::Scene );

    for ( int32 stepIndex = 0; stepIndex < 200 && manager.isTransitioning(); ++stepIndex )
    {
        sw::engine::getTaskManager().waitAll();
        manager.tickTransitions();
    }
    SW_EXPECT_TRUE( future.isReady() );
    SW_EXPECT_NOT_NULL( future.get() );

    // GameObject 64 개 + 문서 · 이름표만으로도 수 KB 다. 진입점이 빠지면 워커 몫은 Unknown 으로 가고 Scene 은 그대로다.
    SW_EXPECT_TRUE_MSG( sceneAfterWorker > sceneBefore + 4096, ( sw::string( "Scene bytes grew by " ) + sw::to_string( sceneAfterWorker - sceneBefore ) ).c_str() );
    SW_EXPECT_TRUE( sw::MemoryProfiler::getCurrentMemoryTag() == outerTag );

    manager.shutdown();
}

/**
 * @brief [MemoryTagTest] 메시 생성(`MeshUtil::createPrimitive`)의 정점 · 인덱스가 Mesh 로 세이고, 놓으면 빠진다
 */
SW_TEST_CASE( MemoryTagTest, PrimitiveMeshIsTaggedMesh )
{
#if defined( SW_ENABLE_STL_CONTAINER )
    SW_TEST_SKIP( "mesh vertex and index storage uses std::allocator in the STL container build, which the memory profiler does not see" );
#else
    if constexpr ( sw::kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );
    const sw::MemoryProfiler* pProfiler = sw::MemoryProfiler::getActive();
    if ( pProfiler == nullptr || pProfiler->isTrackingEnabled() == false )
        SW_TEST_SKIP( "no tracking memory profiler in this host" );

    const uint64 meshBefore = getLiveBytes( *pProfiler, sw::MemoryTag::Mesh );
    uint64       meshHeld{ 0 };
    {
        sw::shared_ptr<sw::Mesh> sphere = sw::MeshUtil::createPrimitive( "Sphere" );
        SW_ASSERT_NOT_NULL( sphere.get() );
        meshHeld = getLiveBytes( *pProfiler, sw::MemoryTag::Mesh );
        SW_EXPECT_TRUE( meshHeld > meshBefore + 1024 );
    }
    // 정점 · 인덱스는 돌려준다. 렌더 자원 등록부처럼 한 번 자란 표는 남을 수 있어 "거의 다" 로 본다.
    const uint64 meshAfter = getLiveBytes( *pProfiler, sw::MemoryTag::Mesh );
    SW_EXPECT_TRUE( meshAfter < meshBefore + ( meshHeld - meshBefore ) / 4 );
#endif
}

/**
 * @brief [MemoryTagTest] 씬 트랜스폼 페이지는 sw 할당자로 잡혀 그때의 태그로 세인다
 * @details 페이지는 칸 번호가 새 페이지에 처음 닿을 때 만들어진다. 앞선 시험이 놓은 칸(빈 칸 목록)이 먼저 나가므로, 새 페이지가 생길 때까지
 *          칸을 받는다. 페이지를 CRT `new` 로 잡으면 태그 줄이 늘지 않아 상한까지 돌고 진다.
 */
SW_TEST_CASE( MemoryTagTest, SceneTransformPageIsTagged )
{
    if constexpr ( sw::kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );
    const sw::MemoryProfiler* pProfiler = sw::MemoryProfiler::getActive();
    if ( pProfiler == nullptr || pProfiler->isTrackingEnabled() == false )
        SW_TEST_SKIP( "no tracking memory profiler in this host" );

    constexpr uint32           kMaxSlotTaken = 1u << 18;
    sw::SceneTransformStorage& storage       = sw::SceneTransformStorage::get();
    sw::vector<uint32>         listSlot;
    listSlot.reserve( kMaxSlotTaken );

    const uint64 animationBefore = getLiveBytes( *pProfiler, sw::MemoryTag::Animation );
    uint64       animationGrowth{ 0 };
    {
        SW_MEMORY_SCOPE( Animation );
        while ( listSlot.size() < kMaxSlotTaken && animationGrowth < sizeof( sw::SceneTransformPage ) )
        {
            sw::SceneTransformPage* pPage{ nullptr };
            listSlot.push_back( storage.allocateSlot( nullptr, pPage ) );
            animationGrowth = getLiveBytes( *pProfiler, sw::MemoryTag::Animation ) - animationBefore;
        }
    }
    for ( const uint32 slot : listSlot )
    {
        storage.freeSlot( slot );
    }

    SW_EXPECT_TRUE_MSG( animationGrowth >= sizeof( sw::SceneTransformPage ),
                        ( sw::string( "Animation bytes grew by " ) + sw::to_string( animationGrowth ) ).c_str() );
}

/**
 * @brief [MemoryTagTest] 경로 캐시를 비우면 캐시 표(Asset)의 저장소까지 돌아간다
 * @details 엔진 종료 끝이 `ResourceUtil::clearPathCache` 를 부른다. 맵의 `clear()` 는 버킷 · 밀집 배열을 남겨, 기동 뒤 자란 표가 종료 누수 보고에 남는다.
 */
SW_TEST_CASE( MemoryTagTest, ClearedPathCacheReturnsItsTable )
{
    if constexpr ( sw::kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );
    const sw::MemoryProfiler* pProfiler = sw::MemoryProfiler::getActive();
    if ( pProfiler == nullptr || pProfiler->isTrackingEnabled() == false )
        SW_TEST_SKIP( "no tracking memory profiler in this host" );
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    sw::ResourceUtil::clearPathCache();
    const uint64     assetBefore = getLiveBytes( *pProfiler, sw::MemoryTag::Asset );
    const sw::string resolved    = sw::ResourceUtil::getResourcePath( "engine/pipeline/forwardpipeline.xml" );
    SW_ASSERT_FALSE( resolved.empty() );
    // 캐시 표가 실제로 자랐어야 아래 비교가 뜻이 있다.
    SW_ASSERT_TRUE( getLiveBytes( *pProfiler, sw::MemoryTag::Asset ) > assetBefore );

    sw::ResourceUtil::clearPathCache();
    SW_EXPECT_EQUAL( assetBefore, getLiveBytes( *pProfiler, sw::MemoryTag::Asset ) );
}

/**
 * @brief [MemoryTagTest] 프리미티브 등록부의 더티 비트 배열은 sw 할당자로 잡혀 그때의 태그로 세인다
 * @details 항목 4096 개 배치를 등록하면 항목 표(16 B × 4096)와 더티 비트 배열 둘(워드 64 개 × 8 B)이 그 태그로 잡힌다. 비트 배열을 CRT `new` 로
 *          잡으면 항목 표 몫만 늘어 하한에 못 미친다.
 */
SW_TEST_CASE( MemoryTagTest, PrimitiveDirtyFlagsAreTagged )
{
    if constexpr ( sw::kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );
    const sw::MemoryProfiler* pProfiler = sw::MemoryProfiler::getActive();
    if ( pProfiler == nullptr || pProfiler->isTrackingEnabled() == false )
        SW_TEST_SKIP( "no tracking memory profiler in this host" );

    constexpr uint32      kEntryCount = 4096;
    sw::PrimitiveRegistry registry;
    sw::MeshInstanceBatch batch( nullptr, nullptr, nullptr, kEntryCount );
    const uint64          animationBefore = getLiveBytes( *pProfiler, sw::MemoryTag::Animation );
    {
        SW_MEMORY_SCOPE( Animation );
        registry.addInstanceBatch( &batch );
    }
    const uint64 animationGrowth = getLiveBytes( *pProfiler, sw::MemoryTag::Animation ) - animationBefore;
    const uint64 entryBytes      = kEntryCount * sizeof( sw::PrimitiveInstanceEntry );
    const uint64 dirtyWordBytes  = 2 * ( kEntryCount / 64 ) * sizeof( sw::atomic<uint64> );
    SW_EXPECT_TRUE_MSG( animationGrowth >= entryBytes + dirtyWordBytes,
                        ( sw::string( "Animation bytes grew by " ) + sw::to_string( animationGrowth ) ).c_str() );
    registry.removeInstanceBatch( &batch );
}

/**
 * @brief [MemoryTagTest] XML 문서(pugixml)의 버퍼 · 노드 페이지는 sw 할당자로 잡혀 그때의 태그로 세인다
 * @details pugixml 은 읽은 글을 자기 버퍼로 복사하고 노드를 페이지에 담는다. 할당 함수를 sw 할당자로 바꾸지 않으면 그 둘이 CRT 에서 잡혀
 *          태그 줄에는 `XmlDocument::Impl` 몇백 바이트만 늘어난다.
 */
SW_TEST_CASE( MemoryTagTest, XmlDocumentParseIsTagged )
{
    if constexpr ( sw::kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );
    const sw::MemoryProfiler* pProfiler = sw::MemoryProfiler::getActive();
    if ( pProfiler == nullptr || pProfiler->isTrackingEnabled() == false )
        SW_TEST_SKIP( "no tracking memory profiler in this host" );

    sw::string xmlText{ "<root>" };
    for ( uint32 index = 0; index < 512; ++index )
    {
        xmlText += "<item name=\"entry\" value=\"0123456789\"/>";
    }
    xmlText += "</root>";

    const uint64 animationBefore = getLiveBytes( *pProfiler, sw::MemoryTag::Animation );
    uint64       animationHeld{ 0 };
    {
        SW_MEMORY_SCOPE( Animation );
        sw::XmlDocument document;
        SW_ASSERT_TRUE( document.parse( xmlText, "memorytag.xml" ) );
        animationHeld = getLiveBytes( *pProfiler, sw::MemoryTag::Animation ) - animationBefore;
    }
    SW_EXPECT_TRUE_MSG( animationHeld >= xmlText.size(), ( sw::string( "Animation bytes held by the document: " ) + sw::to_string( animationHeld ) ).c_str() );
    SW_EXPECT_TRUE( getLiveBytes( *pProfiler, sw::MemoryTag::Animation ) < animationBefore + xmlText.size() );
}

/**
 * @brief [MemoryTagTest] JSON 문서(nlohmann)의 객체 · 문자열은 sw 할당자로 잡혀 그때의 태그로 세인다
 * @details JsonDocument 의 json 타입은 문자열 · 배열 · 객체 할당자로 sw 할당자를 받는다. 표준 할당자면 태그 줄에는 `JsonDocument::Impl` 만 늘어난다.
 */
SW_TEST_CASE( MemoryTagTest, JsonDocumentParseIsTagged )
{
    if constexpr ( sw::kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );
    const sw::MemoryProfiler* pProfiler = sw::MemoryProfiler::getActive();
    if ( pProfiler == nullptr || pProfiler->isTrackingEnabled() == false )
        SW_TEST_SKIP( "no tracking memory profiler in this host" );

    sw::string jsonText{ "{" };
    for ( uint32 index = 0; index < 512; ++index )
    {
        if ( index > 0 )
            jsonText += ",";
        jsonText += "\"key" + sw::to_string( index ) + "\":\"a value longer than the small string buffer\"";
    }
    jsonText += "}";

    const uint64 animationBefore = getLiveBytes( *pProfiler, sw::MemoryTag::Animation );
    uint64       animationHeld{ 0 };
    {
        SW_MEMORY_SCOPE( Animation );
        sw::JsonDocument document;
        SW_ASSERT_TRUE( document.parse( jsonText, "memorytag.json" ) );
        animationHeld = getLiveBytes( *pProfiler, sw::MemoryTag::Animation ) - animationBefore;
    }
    SW_EXPECT_TRUE_MSG( animationHeld >= jsonText.size(), ( sw::string( "Animation bytes held by the document: " ) + sw::to_string( animationHeld ) ).c_str() );
    SW_EXPECT_TRUE( getLiveBytes( *pProfiler, sw::MemoryTag::Animation ) < animationBefore + jsonText.size() );
}

/**
 * @brief [MemoryTagTest] 압축 코덱(zlib · zstd · LZ4 HC)의 작업 공간은 sw 할당자로 잡혀 그때의 태그로 세인다
 * @details deflate · inflate 상태와 창, zstd 압축 · 해제 문맥, LZ4 HC 상태는 모두 수십~수백 KB 다. `compress2` · `ZSTD_compress` · `LZ4_compress_HC` 처럼
 *          라이브러리가 CRT malloc 으로 잡게 두면 태그 줄은 움직이지 않는다.
 */
SW_TEST_CASE( MemoryTagTest, CompressionWorkspacesAreTagged )
{
    if constexpr ( sw::kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );
    const sw::MemoryProfiler* pProfiler = sw::MemoryProfiler::getActive();
    if ( pProfiler == nullptr || pProfiler->isTrackingEnabled() == false )
        SW_TEST_SKIP( "no tracking memory profiler in this host" );

    sw::vector<uint8> listSource( 64 * 1024 );
    for ( size_t index = 0; index < listSource.size(); ++index )
    {
        listSource[index] = static_cast<uint8>( ( index * 7 ) % 251 );
    }

    sw::ZlibCompressionCodec zlibCodec;
    sw::ZstdCompressionCodec zstdCodec;
    sw::Lz4CompressionCodec  lz4Codec;
    struct CodecCase
    {
        sw::ICompressionCodec* _pCodec;
        int32                  _level;
    };
    const CodecCase arrCase[] = {
        {&zlibCodec, 0},
        {&zstdCodec, 0},
        { &lz4Codec, 9}
    };
    for ( const CodecCase& codecCase : arrCase )
    {
        sw::vector<uint8> listCompressed( codecCase._pCodec->compressBound( listSource.size() ) );
        sw::vector<uint8> listRestored( listSource.size() );
        const uint64      totalBefore = getTotalBytes( *pProfiler, sw::MemoryTag::Animation );
        size_t            compressedSize{ 0 };
        size_t            restoredSize{ 0 };
        {
            SW_MEMORY_SCOPE( Animation );
            SW_ASSERT_TRUE( codecCase._pCodec->compress( listSource.data(), listSource.size(), listCompressed.data(), listCompressed.size(), compressedSize,
                                                         codecCase._level ) );
            SW_ASSERT_TRUE( codecCase._pCodec->decompress( listCompressed.data(), compressedSize, listRestored.data(), listRestored.size(), restoredSize ) );
        }
        const uint64 totalGrowth = getTotalBytes( *pProfiler, sw::MemoryTag::Animation ) - totalBefore;
        SW_EXPECT_EQUAL( listSource.size(), restoredSize );
        SW_EXPECT_TRUE( sw::Memory::compare( listSource.data(), listRestored.data(), listSource.size() ) == 0 );
        SW_EXPECT_TRUE_MSG( totalGrowth >= 32 * 1024, ( sw::string( codecCase._pCodec->getCodecName() ) + " workspace bytes: " + sw::to_string( totalGrowth ) ).c_str() );
    }
}
