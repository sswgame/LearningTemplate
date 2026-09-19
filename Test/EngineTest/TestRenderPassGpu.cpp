#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/FrameArenaAllocator.h"
#include "Core/String/hashed_string.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/Renderer/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/RenderFramePacket.h"
#include "Engine/Graphics/Renderer/Graph/RenderGraph.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassManager.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassResource.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineResource.h"
#include "Engine/Graphics/Renderer/Scene/GpuScene.h"
#include "Engine/Graphics/Renderer/Scene/GpuSceneBuilder.h"
#include "Engine/Graphics/Upload/GpuUploadQueue.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Window/IWindow.h"

#include "TestFramework/TestFramework.h"

// 프레임 렌더러를 실제 디바이스 위에서 돌린다 — 패스 · 파이프라인 · 백엔드 패리티를 픽셀로 본다.
//
// SW_TEST_REQUIRES_HOST( RenderPassGpuTest ): 실제 GPU 디바이스를 만든다. **디바이스가 필요한
// 케이스는 전부 이 스위트에 넣는다** — 2026-09-08 에 새 케이스 여섯이 이 규칙을 비켜 CI 로 들어갔고,
// Windows 러너의 WARP 가 초기화에 성공해서 픽셀 검증이 실제로 돌고 졌다.
// RenderPassGpuTest — 실제 RHI 디바이스로 FrameRenderer 를 돌려 픽셀을 읽는다.
// 스위트 이름이 곧 CTest 의 NoGPU 필터다(EngineTest/CMakeLists.txt) — 디바이스가 필요한 케이스는 여기에 둔다.

namespace
{
    bool tryInitDeviceForFrameRenderer( sw::RHIBackend backend, sw::unique_ptr<sw::IWindow>& outWindow,
                                        sw::shared_ptr<sw::IRHIDevice>& outDevice )
    {
        if ( sw::RHIAvailability::isAvailable( backend ) == false )
            return false;
        outWindow = sw::IWindow::createPlatformWindow();
        if ( outWindow == nullptr || outWindow->initializeWindow( "FrameRendererGolden", 320, 240 ) == false )
        {
            outWindow.reset();
            return false;
        }
        outDevice = sw::RHI::createDevice( backend );
        if ( outDevice == nullptr )
        {
            outWindow->destroy();
            outWindow.reset();
            return false;
        }
        outDevice->setInitialWindow( outWindow.get() );
        if ( outDevice->initialize() == false )
        {
            outDevice.reset();
            outWindow->destroy();
            outWindow.reset();
            return false;
        }
        return true;
    }

    /**
     * @brief 첨부 하나의 R 채널 평균(0~255)을 되읽습니다. 실패하면 0.
     * @details 반정밀도(R16G16B16A16_FLOAT) 첨부는 [0,1] 로 클램프해 환산한다 — 두 판을 비교하는 데만
     *          쓰므로 정확한 휘도가 아니라 **같은 규칙으로 잰 같은 수**이면 된다.
     */
    float64 readMeanChannel( sw::FrameRenderer& renderer, const utf8* pAttachment )
    {
        sw::vector<uint8>     bytes;
        sw::RHITextureMipSpan layout{};
        sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
        if ( renderer.readbackTransient( pAttachment, bytes, layout, format ) == false )
            return 0.0;
        const uint32 bytesPerPixel = sw::getRhiFormatBytesPerPixel( format );
        const bool   bHalf         = ( format == sw::RHIFormat::R16G16B16A16_FLOAT );
        uint64       sum{ 0 };
        uint64       count{ 0 };
        for ( uint32 row = 0; row < layout._height; ++row )
        {
            const uint8* pRow = bytes.data() + static_cast<size_t>( row ) * layout._rowBytes;
            for ( uint32 col = 0; col < layout._width; ++col )
            {
                const uint8* pPixel = pRow + static_cast<size_t>( col ) * bytesPerPixel;
                uint32       red{ 0 };
                if ( bHalf )
                {
                    const uint16 half     = static_cast<uint16>( pPixel[0] | ( pPixel[1] << 8 ) );
                    const uint32 exponent = ( half >> 10 ) & 0x1Fu;
                    const uint32 mantissa = half & 0x3FFu;
                    float32      value    = 0.0f;
                    if ( exponent != 0 && exponent != 0x1Fu )
                        value = ( 1.0f + static_cast<float32>( mantissa ) / 1024.0f ) * std::ldexp( 1.0f, static_cast<int32>( exponent ) - 15 );
                    if ( ( half & 0x8000u ) != 0 )
                        value = 0.0f;
                    red = static_cast<uint32>( sw::MathUtil::clamp( value, 0.0f, 1.0f ) * 255.0f );
                }
                else
                    red = pPixel[0];
                sum += red;
                ++count;
            }
        }
        return count > 0 ? static_cast<float64>( sum ) / static_cast<float64>( count ) : 0.0;
    }

    /**
     * @brief 패킷 경로로 몇 프레임 그리고(프레젠트 포함) SceneColor 에서 배경이 아닌 픽셀 수를 셉니다. 실패면 -1.
     * @details 패킷은 프레임마다 새로 만든다 — `executePacket` 이 스냅샷을 **옮겨 가므로** 같은 패킷을 두 번 내면 두 번째는
     *          빈 스냅샷이다(GT 도 프레임마다 export 한다).
     */
    int64 renderPacketFramesAndCountDrawn( sw::FrameRenderer& renderer, sw::IRHIDevice* pDevice, sw::Scene& scene, sw::GpuSceneBuilder& gtGpuScene )
    {
        constexpr uint32 kFrameCount = 3;
        const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
        for ( uint32 frame = 0; frame < kFrameCount; ++frame )
        {
            sw::RenderFramePacket packet{};
            packet._bValid = 1;
            gtGpuScene.buildFromScene( &scene, packet._cameraPos );
            gtGpuScene.exportCpuSnapshot( packet._gpuScene );
            if ( packet._gpuScene._listInstance.empty() )
                return -1;
            pDevice->beginFrame( clear );
            const bool bOk = renderer.executePacket( pDevice, packet );
            pDevice->endFrame( true, false );
            if ( bOk == false )
                return -1;
        }
        pDevice->waitIdle();

        sw::vector<uint8>     bytes;
        sw::RHITextureMipSpan layout{};
        sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
        if ( renderer.readbackTransient( "SceneColor", bytes, layout, format ) == false )
            return -1;
        int64 drawnCount{ 0 };
        for ( uint32 y = 0; y < layout._height; ++y )
        {
            const uint8* pRow = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes;
            for ( uint32 x = 0; x < layout._width; ++x )
            {
                const uint8* pPixel = pRow + static_cast<size_t>( x ) * 4;
                const uint8  r      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[2] : pPixel[0];
                const uint8  b      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[0] : pPixel[2];
                if ( r > 40 || pPixel[1] > 48 || b > 56 || r < 22 || pPixel[1] < 28 || b < 36 )
                    ++drawnCount;
            }
        }
        return drawnCount;
    }
} // namespace

/**
 * @brief FrameRenderer 파이프라인 로드 + 씬 execute 스모크 (RenderThread와 동일 begin/execute/end)
 */
SW_TEST_CASE( RenderPassGpuTest, FrameRendererInitializeAndExecuteSmoke )
{
    sw::unique_ptr<sw::IWindow>    window;
    sw::shared_ptr<sw::IRHIDevice> device;
    const sw::RHIBackend           backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL, sw::RHIBackend::DirectX12 };
    bool bOk{ false };
    for ( sw::RHIBackend backend : backends )
    {
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) )
        {
            bOk = true;
            break;
        }
    }
    if ( bOk == false )
        SW_TEST_SKIP( "No RHI backend for FrameRenderer smoke" );

    sw::FrameRenderer renderer;
    SW_EXPECT_TRUE( renderer.initialize( device.get() ) );
    SW_EXPECT_TRUE( renderer.isReady() );
    SW_EXPECT_TRUE( renderer.getGraph().getNodeCount() > 0 );

    sw::Scene scene( "FrameRendererSmokeScene" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    sw::GameObject*          go   = scene.getObjectManager()->createGameObject( sw::hashed_string( "Cube" ) );
    SW_ASSERT_NOT_NULL( go );
    sw::MeshComponent* mesh = go->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( mesh );
    mesh->setMesh( cube );

    sw::float4 clear = { 0.02f, 0.02f, 0.05f, 1.0f };
    device->beginFrame( clear );
    SW_EXPECT_TRUE( renderer.execute( device.get(), &scene ) );
    device->endFrame( false, false );
    device->waitIdle();

    // static Mesh 캐시가 죽은 디바이스를 붙잡지 않도록 디바이스 종료 전에 GPU 해제.
    if ( cube != nullptr )
        cube->releaseRhi( device.get() );

    renderer.shutdown();
    device->shutdown();
    device.reset();
    window->destroy();
    window.reset();
}

/**
 * @brief [RenderPassTest] 셰이더 핫리로드가 PSO 를 **실제로 다시 만드는지** 검증.
 * @details PSO 는 바이트코드를 구워 넣은 객체다. onShaderRecompiled 가 바인딩 레이아웃만
 *          새로 만들던 시절에는 셰이더를 고쳐도 화면이 시작 시 컴파일된 그대로였다 —
 *          로그는 "Recompilation Succeeded" 를 찍는데 그림은 안 바뀌니 눈치채기 어려웠다.
 *          LiveShaderTest 는 등록과 리로드 큐만 보므로 이 경로를 잡지 못한다.
 *
 *          네 백엔드 모두 PSO 를 RHIHandleTable(generation 팩드)로 발급하므로, 다시 만들면
 *          핸들 값이 반드시 달라진다. 재생성 여부를 핸들로 판정하는 근거다.
 */
SW_TEST_CASE( RenderPassGpuTest, ShaderRecompileRebuildsPipelineStates )
{
    sw::unique_ptr<sw::IWindow>    window;
    sw::shared_ptr<sw::IRHIDevice> device;
    const sw::RHIBackend           backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL, sw::RHIBackend::DirectX12 };
    bool bOk{ false };
    for ( sw::RHIBackend backend : backends )
    {
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) )
        {
            bOk = true;
            break;
        }
    }
    if ( bOk == false )
        SW_TEST_SKIP( "No RHI backend for shader recompile test" );

    sw::FrameRenderer renderer;
    SW_EXPECT_TRUE( renderer.initialize( device.get() ) );
    SW_EXPECT_TRUE( renderer.isReady() );

    const sw::RHIPipelineStateHandle beforeForward = renderer.getEnginePso( sw::RenderPassType::ForwardOpaque );
    SW_EXPECT_TRUE_MSG( beforeForward != 0, "리로드 전 ForwardOpaque PSO 가 있어야 한다" );

    sw::ShaderCompileResult result{};
    result._bSuccess = true;
    renderer.onShaderRecompiled( "engine/shaders/forwardlit.hlsl", result );

    const sw::RHIPipelineStateHandle afterForward = renderer.getEnginePso( sw::RenderPassType::ForwardOpaque );
    SW_EXPECT_TRUE_MSG( afterForward != 0, "리로드 후 ForwardOpaque PSO 가 다시 만들어져야 한다" );
    SW_EXPECT_TRUE_MSG( afterForward != beforeForward,
                        "PSO 핸들이 그대로다 — 레이아웃만 갱신하고 파이프라인은 예전 바이트코드를 들고 있다" );

    // 재생성 뒤에도 여전히 그릴 수 있어야 한다 (레이아웃·폴백 버퍼·콜백이 같이 재구축됐는지).
    sw::Scene scene( "ShaderRecompileScene" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    sw::GameObject*          pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( "Cube" ) );
    SW_ASSERT_NOT_NULL( pObj );
    sw::MeshComponent* pMesh = pObj->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    pMesh->setMesh( cube );

    const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
    device->beginFrame( clear );
    SW_EXPECT_TRUE_MSG( renderer.execute( device.get(), &scene ), "PSO 재생성 후 프레임 실행" );
    device->endFrame( false, false );
    device->waitIdle();

    if ( cube != nullptr )
        cube->releaseRhi( device.get() );
    renderer.shutdown();
    device->shutdown();
    device.reset();
    window->destroy();
    window.reset();
}

/**
 * @brief executePacket()이 프레임마다 GpuScene GPU 버퍼를 재생성하지 않고 재사용하는지 검증.
 * @details GT/RT 소유권 분리(exportCpuSnapshot/adoptCpuSnapshot) 회귀 테스트 — 고치기 전에는
 *          FrameRenderer::_gpuScene이 매 프레임 통째로 덮어써져서 인스턴스 버퍼 핸들이 매번 바뀌었다
 *          (직전 프레임 버퍼/디스크립터는 releaseGpu() 없이 버려지는 누수였음).
 */
SW_TEST_CASE( RenderPassGpuTest, GpuSceneBufferReusedAcrossPackets )
{
    sw::unique_ptr<sw::IWindow>    window;
    sw::shared_ptr<sw::IRHIDevice> device;
    const sw::RHIBackend           backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL, sw::RHIBackend::DirectX12 };
    bool bOk{ false };
    for ( sw::RHIBackend backend : backends )
    {
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) )
        {
            bOk = true;
            break;
        }
    }
    if ( bOk == false )
        SW_TEST_SKIP( "No RHI backend for GpuScene buffer reuse test" );

    sw::FrameRenderer renderer;
    SW_EXPECT_TRUE( renderer.initialize( device.get() ) );

    sw::Scene scene( "GpuSceneReuseScene" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    sw::GameObject*          go   = scene.getObjectManager()->createGameObject( sw::hashed_string( "Cube" ) );
    SW_ASSERT_NOT_NULL( go );
    sw::MeshComponent* mesh = go->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( mesh );
    mesh->setMesh( cube );

    sw::GpuSceneBuilder gtGpuScene; // EngineLoop::_gpuSceneBuilder 역할 — 여기서는 테스트 로컬로 흉내
    sw::float4          clear{ 0.02f, 0.02f, 0.05f, 1.0f };
    sw::RHIBufferHandle instanceBufferAfterFrame1{ 0 };

    for ( uint32 frameIndex = 0; frameIndex < 8; ++frameIndex )
    {
        sw::RenderFramePacket packet{};
        packet._bValid = 1;
        gtGpuScene.buildFromScene( &scene, packet._cameraPos );
        gtGpuScene.exportCpuSnapshot( packet._gpuScene );

        device->beginFrame( clear );
        SW_EXPECT_TRUE( renderer.executePacket( device.get(), packet ) );
        device->endFrame( false, false );

        const sw::RHIBufferHandle instanceBuffer = renderer.getGpuScene().getInstanceBuffer();
        SW_EXPECT_TRUE( instanceBuffer != 0 );
        if ( frameIndex == 0 )
            instanceBufferAfterFrame1 = instanceBuffer;
        else
            SW_EXPECT_EQUAL( instanceBufferAfterFrame1, instanceBuffer );
    }

    device->waitIdle();
    if ( cube != nullptr )
        cube->releaseRhi( device.get() );

    renderer.shutdown();
    device->shutdown();
    device.reset();
    window->destroy();
    window.reset();
}

/**
 * @brief [RenderPassGpuTest] 패킷에 실린 머티리얼·인스턴스는 GT 가 소유를 놓아도 RT 가 그 패킷을 다 쓸 때까지 산다.
 * @details 렌더 스레드는 씬을 못 보고 스냅샷만 받는다. 스냅샷이 생포인터만 들고 있으면 GT 가 오브젝트를
 *          지우거나 인스턴스를 바꾼 직후 ≤ 패킷 링 깊이 프레임 동안 RT 가 해제된 메모리를 읽는다
 *          (`applyInstanceCbsVal` 의 updateRhi, `uploadMaterialGroups` 의 getBuffer). 여기서는 패킷을
 *          내보낸 **뒤에** GT 쪽 소유를 전부 놓고 그 패킷을 실행한다 — ASAN 빌드에서 use-after-free 로
 *          잡히던 순서다. 스냅샷이 소유를 함께 실어야만 통과한다.
 */
SW_TEST_CASE( RenderPassGpuTest, MaterialLifetimeFollowsPacket )
{
    sw::unique_ptr<sw::IWindow>    window;
    sw::shared_ptr<sw::IRHIDevice> device;
    const sw::RHIBackend           backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL, sw::RHIBackend::DirectX12 };
    bool bOk{ false };
    for ( sw::RHIBackend backend : backends )
    {
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) )
        {
            bOk = true;
            break;
        }
    }
    if ( bOk == false )
        SW_TEST_SKIP( "No RHI backend for material lifetime test" );

    sw::FrameRenderer renderer;
    SW_EXPECT_TRUE( renderer.initialize( device.get() ) );

    sw::Scene scene( "MaterialLifetimeScene" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    sw::GameObject*          go   = scene.getObjectManager()->createGameObject( sw::hashed_string( "Cube" ) );
    SW_ASSERT_NOT_NULL( go );
    sw::MeshComponent* mesh = go->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( mesh );
    mesh->setMesh( cube );

    // GT 소유: 머티리얼 하나 + 그 인스턴스 하나. 아래에서 둘 다 놓는다.
    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_ASSERT_TRUE( material->loadFromFile( "engine/materials/defaultmaterial.material" ) );
    sw::shared_ptr<sw::MaterialInstance> instance = sw::MaterialInstance::create( material.get() );
    mesh->setMaterial( material.get() );
    mesh->setMaterialInstance( instance );

    sw::GpuSceneBuilder gtGpuScene;
    sw::float4          clear{ 0.02f, 0.02f, 0.05f, 1.0f };

    // 1 프레임: 정상 경로로 한 번 올린다 (버퍼·CB 가 만들어진다).
    {
        sw::RenderFramePacket packet{};
        packet._bValid = 1;
        gtGpuScene.buildFromScene( &scene, packet._cameraPos );
        gtGpuScene.exportCpuSnapshot( packet._gpuScene );
        device->beginFrame( clear );
        SW_EXPECT_TRUE( renderer.executePacket( device.get(), packet ) );
        device->endFrame( false, false );
    }

    // 2 프레임: 패킷을 먼저 내보내고, **그 다음** GT 가 소유를 전부 놓는다 — 실제 에디터에서
    // 오브젝트 삭제·인스턴스 교체가 RT 보다 먼저 일어나는 순서다.
    sw::RenderFramePacket lateePacket{};
    lateePacket._bValid = 1;
    gtGpuScene.buildFromScene( &scene, lateePacket._cameraPos );
    gtGpuScene.exportCpuSnapshot( lateePacket._gpuScene );
    SW_ASSERT_FALSE( lateePacket._gpuScene._listInstance.empty() );

    mesh->setMaterialInstance( nullptr );
    mesh->setMaterial( nullptr );
    gtGpuScene.clear(); // GT 쪽 빌드 캐시도 놓는다 — 이제 살아 있는 참조는 패킷 안의 것뿐이어야 한다
    instance.reset();
    material.reset();

    device->beginFrame( clear );
    SW_EXPECT_TRUE( renderer.executePacket( device.get(), lateePacket ) );
    device->endFrame( false, false );

    device->waitIdle();
    if ( cube != nullptr )
        cube->releaseRhi( device.get() );
    renderer.shutdown();
    device->shutdown();
    device.reset();
    window->destroy();
    window.reset();
}

/**
 * @brief 실제 RHI 디바이스로 RenderGraph::executeParallel을 웨이브 단위로 끝까지 실행해 본다.
 * @details 독립 브랜치(DepthPass/ShadowPass) + 합류 패스(ForwardPass) 구조로 웨이브 경계를 넘나드는
 *          제출 순서(웨이브마다 먼저 제출 후 다음 웨이브)까지 실제로 동작하는지 확인한다.
 * @note **병렬 기록 여부는 디바이스에 묻는다 — 백엔드 이름으로 고르지 않는다.** 예전엔 이 테스트가
 *       "DX12만 _bParallelCommandRecording=1"이라고 적고 DX12 를 하드코딩했는데, 그 전제는 틀렸다:
 *       `D3D11RHIDevice::getCapabilities` 가 드라이버 조회 결과(`D3D11_FEATURE_THREADING`)로 이 항목을
 *       런타임에 덮으므로 DX11 도 병렬로 기록한다. 그래서 DX11 의 병렬 경로는 **테스트가 한 번도 닿은
 *       적이 없었고**, 거기 살던 레이스 둘(즉시 컨텍스트 동시 Map, 리스트가 공유하던 기록 상태
 *       캐시)이 `AmbientOcclusionReachesBloom` 을 플래키하게 만든 뒤에야 드러났다.
 */
SW_TEST_CASE( RenderPassGpuTest, RenderGraphExecuteParallelRunsOnRealDevice )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    uint32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        if ( device->getCapabilities()._bParallelCommandRecording == SW_FALSE )
        {
            device->shutdown();
            device.reset();
            window->destroy();
            window.reset();
            continue;
        }
        ++attemptedCount;
        const sw::string label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( backend ) );

        sw::TaskManager taskManager;
        SW_ASSERT_TRUE( taskManager.initialize( 2 ) );

        sw::RenderGraph    graph;
        sw::atomic<uint32> executeCount{ 0 };

        auto makeCb = [&executeCount]( const utf8* pExpectedName ) -> sw::RenderGraphPassExecuteFn
        {
            return sw::RenderGraphPassExecuteFn(
                SW_DELEGATE_LAMBDA( sw::RenderGraphPassExecuteFn, [&executeCount, pExpectedName]( const sw::RenderGraphPassContext& ctx )
            {
                SW_EXPECT_STREQ( pExpectedName, ctx._passName.c_str() );
                SW_EXPECT_TRUE( ctx._pCmdList != nullptr );
                executeCount.fetch_add( 1, std::memory_order_relaxed );
            } ) );
        };

        graph.addPass( sw::hashed_string( "DepthPass" ), {}, { sw::hashed_string( "DepthBuffer" ) }, makeCb( "DepthPass" ) );
        graph.addPass( sw::hashed_string( "ShadowPass" ), {}, { sw::hashed_string( "ShadowMap" ) }, makeCb( "ShadowPass" ) );
        graph.addPass( sw::hashed_string( "ForwardPass" ), { sw::hashed_string( "DepthBuffer" ), sw::hashed_string( "ShadowMap" ) }, { sw::hashed_string( "SceneColor" ) }, makeCb( "ForwardPass" ) );

        sw::RenderGraphExecutionContext context;
        SW_EXPECT_TRUE_MSG( graph.executeParallel( context, &taskManager, device.get() ), ( label + ": executeParallel 실패" ).c_str() );
        SW_EXPECT_EQUAL( 3u, executeCount.load() );

        device->waitIdle();
        taskManager.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No backend reports parallel command recording" );
}

/**
 * @brief Deferred 파이프라인을 실제 디바이스에서 돌린다 — 같은 웨이브의 패스가 병렬로 기록되는 유일한 구성.
 * @details forwardpipeline 은 Shadow→ForwardOpaque→…→Present 완전 체인이라 웨이브가 전부 1개다.
 *          즉 병렬 기록 경로가 있어도 실제로 동시에 도는 패스가 없었고, 그래서 패스 콜백이 만지는
 *          FrameRenderer 공유 상태(_listClearedThisFrame, 프레임 래치 플래그)의 레이스가 드러나지
 *          않았다. deferredpipeline 은 웨이브0 = {Shadow, GBuffer}, 이후 {Transparent, SSAO} 가
 *          동시에 기록된다. 메시가 있어야 드로우 경로까지 들어가므로 큐브를 넣고 여러 프레임 돌린다.
 *
 *          이 테스트를 처음 넣었을 때 곧바로 DX12 GPU 행(3번째 프레임에서 fence wait timeout →
 *          DEVICE_HUNG → 크래시)을 잡아냈다. 원인은 파이프라인 XML 이 선언한 포맷과 PSO/보조
 *          텍스처가 어긋난 것이었다(Shading 별칭 미해석, 풀스크린 PSO 의 뎁스 포맷, TAA 히스토리
 *          포맷 하드코딩). 검증 레이어 오류가 0 인지도 같이 봐야 의미가 있다.
 */
SW_TEST_CASE( RenderPassGpuTest, FrameRendererDeferredPipelineParallelWaves )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    uint32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        // **병렬로 기록하는 백엔드는 전부 돈다 — 하나 찾고 멈추지 않는다.** 예전엔 목록이 {DX12, Vulkan}
        // 이고 첫 성공에서 break 였다. 레이스를 잡으려고 만든 테스트가 정작 레이스가 있던 DX11 을
        // 건너뛰고 있었다.
        if ( device->getCapabilities()._bParallelCommandRecording == SW_FALSE )
        {
            device->shutdown();
            device.reset();
            window->destroy();
            window.reset();
            continue;
        }
        ++attemptedCount;
        const sw::string label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( backend ) );

        sw::TaskManager taskManager;
        SW_ASSERT_TRUE( taskManager.initialize( 4 ) );

        sw::FrameRenderer renderer;
        SW_EXPECT_TRUE_MSG( renderer.initialize( device.get(), &taskManager, "engine/pipeline/deferredpipeline.xml" ),
                            ( label + ": FrameRenderer 초기화 실패" ).c_str() );
        SW_EXPECT_TRUE( renderer.isReady() );

        sw::Scene scene( "DeferredParallelScene" );
        SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
        sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
        for ( uint32 objectIndex = 0; objectIndex < 4; ++objectIndex )
        {
            sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( "DeferredCube" ) );
            SW_ASSERT_NOT_NULL( pObj );
            sw::MeshComponent* pMesh = pObj->addComponent<sw::MeshComponent>();
            SW_ASSERT_NOT_NULL( pMesh );
            pMesh->setMesh( cube );
            // 인스턴스를 서로 다른 월드 행렬로 흩어 놓아야 드로우 루프의 월드 갱신 분기까지 탄다.
            pMesh->setLocalPosition( sw::float3{ static_cast<float32>( objectIndex ) * 1.5f, 0.0f, 0.0f } );
        }

        // **직렬로 한 판, 병렬로 한 판 — 그림이 같아야 한다.** 여러 프레임을 도는 것만으로는
        // 부족하다: 기록 레이스는 예외도 실패 코드도 내지 않고 **픽셀만** 바꾼다. 실제로 이 테스트는
        // "돌았다" 만 보던 시절 DX11 의 레이스 둘을 그대로 통과시켰다(되돌려 놓고 5/5 통과를 확인했다).
        // 직렬 경로가 기준이고, 병렬이 같은 값을 내야 한다는 것이 이 기능의 유일한 계약이다.
        const sw::float4 clear               = { 0.02f, 0.02f, 0.05f, 1.0f };
        auto             renderAndMeasureLit = [&]( sw::TaskManager* pTaskManager ) -> float64
        {
            renderer.bindServices( pTaskManager );
            for ( uint32 frameIndex = 0; frameIndex < 8; ++frameIndex )
            {
                device->beginFrame( clear );
                SW_EXPECT_TRUE_MSG( renderer.execute( device.get(), &scene ), ( label + ": execute 실패" ).c_str() );
                device->endFrame( false, false );
            }
            device->waitIdle();
            return readMeanChannel( renderer, "LitColor" );
        };

        const float64 serialMean = renderAndMeasureLit( nullptr );

        // 드로우 경로까지 실제로 들어갔는지 — GpuScene 이 비어 있으면 이 테스트는 클리어만 검증한 셈이다.
        SW_EXPECT_TRUE_MSG( renderer.getGpuScene().getInstances().empty() == false, ( label + ": GpuScene 이 비었다" ).c_str() );
        SW_EXPECT_TRUE_MSG( serialMean > 0.0, ( label + ": 직렬 판의 LitColor 를 되읽지 못했다" ).c_str() );

        // **병렬 판은 세 번 재서 각각 기준과 대조한다.** 다만 기대만큼 이롭지는 않다 — 실측으로
        // 레이스는 **프로세스마다 굳는** 경향이 있어(한 번 어긋나면 그 프로세스의 세 판이 모두 어긋났다)
        // 한 판이 다섯 번에 네 번, 세 판이 여덟 번에 일곱 번을 잡았다. 반복은 굳지 않는 경우를 위한
        // 보험이고, **코드가 옳으면 언제나 통과한다**(직렬 == 병렬은 결정적이다). 재도입을 확실히
        // 잡아 주는 것은 CI 가 이 테스트를 커밋마다 돌린다는 사실이다.
        constexpr uint32 kParallelRunCount = 3;
        for ( uint32 runIndex = 0; runIndex < kParallelRunCount; ++runIndex )
        {
            const float64 parallelMean = renderAndMeasureLit( &taskManager );
            // 허용 오차는 1% 다. 실제 레이스는 LitColor 를 38% 넘게 흔들었으므로 한참 아래고,
            // 백엔드의 사소한 비결정성은 이 안에 들어온다.
            const float64 drift    = ( parallelMean - serialMean ) / serialMean;
            const float64 absDrift = drift < 0.0 ? -drift : drift;
            SW_EXPECT_TRUE_MSG( absDrift < 0.01,
                                ( label + ": 병렬 기록이 그림을 바꿨다 (직렬 " + sw::to_string( static_cast<float32>( serialMean ) ) +
                                  " vs 병렬 " + sw::to_string( static_cast<float32>( parallelMean ) ) + ", " +
                                  sw::to_string( runIndex ) + "번째 판)" )
                                    .c_str() );
        }

        if ( cube != nullptr )
            cube->releaseRhi( device.get() );
        renderer.shutdown();
        taskManager.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No backend reports parallel command recording" );
}

/**
 * @brief [RenderPassTest] 머티리얼의 퍼뮤테이션이 실제로 그 배치의 PSO 가 되는지 (4 백엔드).
 * @details 머티리얼은 자기 셰이더 변형을 선언한다(유리는 MATERIAL_BLEND_TRANSLUCENT 를 always-define 으로
 *          들고 있다). 그런데 드로우가 **패스 PSO 하나로** 전부 그리면 그 선언은 구워지기만 하고 한 번도
 *          걸리지 않는다. 예전엔 반투명 패스 PSO 에 그 define 을 직접 박아 두어 가려져 있었다 —
 *          "반투명 패스에 들어온 것은 무조건 반투명" 이었고, 머티리얼이 뭘 선언했는지는 상관이 없었다.
 *
 *          픽셀로는 잡기 어렵다. 알파 경로가 컴파일됐는지 여부는 겹치는 곳의 색만 바꾸는데, 그 색은
 *          조명·톤매핑을 타고 흔들린다. 그래서 **드로우가 실제로 고른 PSO 의 디스크립터**를 본다.
 */
SW_TEST_CASE( RenderPassGpuTest, MaterialPermutationDrivesBatchPso )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    auto hasDefine = []( const sw::RHIPipelineStateDesc& desc, const utf8* pDefine ) -> bool
    {
        for ( const sw::string& defineStr : desc._listShaderDefine )
        {
            if ( defineStr == pDefine )
                return true;
        }
        return false;
    };

    uint32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        // 실제 에셋을 쓴다 — 손으로 지은 XML 은 _permutations 가 빠져 검증하려던 것과 다른 걸 재게 된다.
        sw::shared_ptr<sw::Material> materialGlass = sw::Material::create();
        if ( bOk )
            bOk = materialGlass->loadFromFile( "engine/materials/glassmaterial.material" );

        sw::Scene scene( "MaterialPermutationPsoScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        sw::shared_ptr<sw::Mesh> mesh;
        if ( bOk )
        {
            mesh = sw::MeshUtil::createUnitCube();
            bOk  = mesh != nullptr;
        }
        if ( bOk )
        {
            sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( "GlassCube" ) );
            bOk                  = pObj != nullptr;
            if ( bOk )
            {
                sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
                bOk                          = pMeshComp != nullptr;
                if ( bOk )
                {
                    pMeshComp->setMesh( mesh );
                    pMeshComp->setMaterial( materialGlass.get() );
                }
            }
        }

        if ( bOk )
        {
            // 한 프레임을 돌려야 배치가 서고 ensureMaterialPsos 가 퍼뮤테이션 PSO 를 만든다.
            const sw::float4 clear{ 0.0f, 0.0f, 0.0f, 1.0f };
            device->beginFrame( clear );
            bOk = renderer.execute( device.get(), &scene );
            device->endFrame( false, false );
            device->waitIdle();
        }

        const sw::string label = sw::string( device->getBackendName() );
        if ( bOk )
        {
            const sw::vector<sw::GpuMeshBatch>& batches = renderer.getGpuScene().getTransparentBatches();
            SW_EXPECT_TRUE_MSG( batches.empty() == false,
                                ( label + ": 반투명 머티리얼인데 반투명 배치가 없다" ).c_str() );

            const sw::RHIPipelineStateHandle passPso = renderer.getEnginePso( sw::RenderPassType::Transparent );
            if ( batches.empty() == false && passPso != 0 )
            {
                // 패스 PSO 자체에는 이제 그 define 이 없다 — 반투명 패스가 정하는 것은 블렌드·뎁스지 셰이더가 아니다.
                sw::RHIPipelineStateDesc passDesc{};
                if ( renderer.findPsoDesc( passPso, passDesc ) )
                {
                    SW_EXPECT_TRUE_MSG( hasDefine( passDesc, "MATERIAL_BLEND_TRANSLUCENT" ) == false,
                                        ( label + ": 반투명 패스 PSO 에 퍼뮤테이션이 박혀 있다 — 머티리얼이 뭘 선언하든 상관없어진다" )
                                            .c_str() );
                    SW_EXPECT_TRUE_MSG( passDesc._bEnableBlend != 0,
                                        ( label + ": 반투명 패스인데 블렌드가 꺼져 있다" ).c_str() );
                }

                // 배치가 고르는 PSO 는 패스 PSO 와 **달라야** 하고, 그 안에 머티리얼의 define 이 있어야 한다.
                const sw::RHIPipelineStateHandle batchPso = renderer.psoForBatch( passPso, batches[0] );
                SW_EXPECT_TRUE_MSG( batchPso != passPso,
                                    ( label + ": 유리 배치가 패스 PSO 를 그대로 쓴다 — 머티리얼 퍼뮤테이션이 안 걸렸다" ).c_str() );

                sw::RHIPipelineStateDesc batchDesc{};
                if ( renderer.findPsoDesc( batchPso, batchDesc ) )
                {
                    SW_EXPECT_TRUE_MSG( hasDefine( batchDesc, "MATERIAL_BLEND_TRANSLUCENT" ),
                                        ( label + ": 배치 PSO 에 MATERIAL_BLEND_TRANSLUCENT 가 없다 — 알파 경로가 컴파일되지 않는다" )
                                            .c_str() );
                    // 렌더 상태는 **패스가 정한다** — 머티리얼이 블렌드를 끄거나 켤 수는 없다.
                    SW_EXPECT_TRUE_MSG( batchDesc._bEnableBlend == passDesc._bEnableBlend,
                                        ( label + ": 퍼뮤테이션 변형이 패스의 블렌드 상태를 바꿨다" ).c_str() );
                }
                else
                {
                    SW_EXPECT_TRUE_MSG( false, ( label + ": 배치 PSO 의 디스크립터를 찾을 수 없다" ).c_str() );
                }

                // 그림자 패스는 자기 지오메트리 셰이더가 정본이다 — 머티리얼 셰이더로 갈아타면 안 된다.
                const sw::RHIPipelineStateHandle shadowPso = renderer.getEnginePso( sw::RenderPassType::Shadow );
                sw::RHIPipelineStateDesc         shadowDesc{};
                if ( shadowPso != 0 && renderer.findPsoDesc( shadowPso, shadowDesc ) )
                {
                    const sw::RHIPipelineStateHandle shadowBatchPso = renderer.psoForBatch( shadowPso, batches[0] );
                    sw::RHIPipelineStateDesc         shadowBatchDesc{};
                    if ( renderer.findPsoDesc( shadowBatchPso, shadowBatchDesc ) )
                    {
                        SW_EXPECT_TRUE_MSG( shadowBatchDesc._vertexShaderPath == shadowDesc._vertexShaderPath,
                                            ( label + ": 그림자 패스가 머티리얼 셰이더로 갈아탔다" ).c_str() );
                        // 컬러 출력이 없는 패스는 픽셀 스테이지가 없고, 머티리얼 변형도 그대로 물려받아야 한다.
                        // 여기에 PS 가 남으면 (shadowdepth · PSMain · 머티리얼 define) 조합을 베이커는 굽지 않으므로
                        // Shipping 이 매니페스트 미스를 낸다 — 실제로 났던 [Error] 다.
                        SW_EXPECT_TRUE_MSG( shadowDesc._numRenderTargets == 0 && shadowDesc._pixelShaderPath.empty(),
                                            ( label + ": 그림자 패스 PSO 에 픽셀 스테이지가 있다" ).c_str() );
                        SW_EXPECT_TRUE_MSG( shadowBatchDesc._pixelShaderPath.empty() && shadowBatchDesc._pixelEntryPoint.empty(),
                                            ( label + ": 그림자 패스의 머티리얼 변형에 픽셀 스테이지가 있다" ).c_str() );
                    }
                }
            }
        }
        else
        {
            SW_EXPECT_TRUE_MSG( false, ( label + ": 프레임 실행 실패" ).c_str() );
        }

        renderer.shutdown();
        if ( mesh != nullptr )
            mesh->releaseRhi( device.get() );
    }

    // 형제 여덟(카메라 컬링·투명 정렬·뷰 모드 등)과 같은 규칙으로 빠진다. 예전엔 여기만 단언이라
    // **디스플레이가 없는 CI 러너에서 이 테스트 하나만 졌다** — X11 디스플레이가 없으면 창이 안 열려
    // 네 백엔드가 전부 초기화에 실패하고, 그건 결함이 아니라 그 환경에 GPU 가 없다는 뜻이다.
    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend available for material permutation PSO test" );
}

/**
 * @brief [RenderPassTest] 메인 패스가 **카메라 절두체**로 컬링되는지 — 라이트 절두체가 아니라 (4 백엔드).
 * @details 컬링은 뷰마다 돈다(메인 카메라 / 그림자 라이트). 그런데 상수버퍼를 **하나만** 두고 두 뷰가
 *          나눠 쓰면, 두 번째 업로드가 첫 번째 디스패치가 읽을 내용을 덮어쓴다 — CPU 는 디스패치 사이에
 *          쓰지만 GPU 는 제출 뒤에 읽기 때문이다. 실제로 그렇게 돼서 메인 뷰가 **그림자 라이트의 좁은
 *          직교 절두체**로 걸러졌고, 화면에서 격자의 절반이 사라졌다.
 *
 *          그래서 큐브를 **라이트 상자 밖, 카메라 시야 안**에 둔다. 라이트는 원점 근처 2.2 폭 상자만
 *          비추므로 x = ±2.5 는 확실히 밖이고, 카메라는 z 를 뒤로 물리면 그만큼 넓게 본다.
 *          뷰를 잘못 쓰면 이 큐브들이 통째로 사라진다.
 */
SW_TEST_CASE( RenderPassGpuTest, MainPassCullsWithCameraFrustumNotLight )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    // 라이트 직교 상자는 원점 중심 폭 2.22 — 반폭 1.11 에 바운드 반지름 0.87 을 더해도 2.5 는 밖이다.
    constexpr float32 kSideX = 2.5f;
    // 카메라(0, 1.2, 3.2)에서 뒤로 물려 시야 폭을 넓힌다 — 그래야 ±2.5 가 화면 안에 들어온다.
    constexpr float32 kDepthZ = -6.0f;

    uint32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "CameraFrustumCullScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        sw::shared_ptr<sw::Mesh> sharedMesh;
        if ( bOk )
        {
            sharedMesh = sw::MeshUtil::createUnitCube();
            bOk        = sharedMesh != nullptr;
        }
        if ( bOk )
        {
            const float32 arrX[] = { -kSideX, kSideX };
            for ( uint32 sideIndex = 0; sideIndex < 2 && bOk; ++sideIndex )
            {
                sw::string      name = sw::string( "FarCube" ) + sw::to_string( sideIndex );
                sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( name.c_str(), static_cast<uint32>( name.size() ) ) );
                bOk                  = pObj != nullptr;
                if ( bOk == false )
                    break;
                sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
                bOk                          = pMeshComp != nullptr;
                if ( bOk == false )
                    break;
                pMeshComp->setMesh( sharedMesh );
                pMeshComp->setLocalPosition( sw::float3{ arrX[sideIndex], 0.0f, kDepthZ } );
            }
        }

        if ( bOk )
        {
            const sw::float4 clear{ 0.0f, 0.0f, 0.0f, 1.0f };
            device->beginFrame( clear );
            bOk = renderer.execute( device.get(), &scene );
            device->endFrame( false, false );
            device->waitIdle();
        }

        sw::vector<uint8>     bytes;
        sw::RHITextureMipSpan layout{};
        sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
        if ( bOk && renderer.readbackTransient( "SceneColor", bytes, layout, format ) )
        {
            const bool   bBgra   = format == sw::RHIFormat::B8G8R8A8_UNORM;
            const uint8* pCorner = bytes.data();
            const int32  bgR     = bBgra ? pCorner[2] : pCorner[0];
            const int32  bgG     = pCorner[1];
            const int32  bgB     = bBgra ? pCorner[0] : pCorner[2];

            uint32 arrDrawn[2]{};
            for ( uint32 y = 0; y < layout._height; ++y )
            {
                const uint8* pRow = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes;
                for ( uint32 x = 0; x < layout._width; ++x )
                {
                    const uint8* pPixel = pRow + static_cast<size_t>( x ) * 4;
                    const int32  r      = bBgra ? pPixel[2] : pPixel[0];
                    const int32  g      = pPixel[1];
                    const int32  b      = bBgra ? pPixel[0] : pPixel[2];
                    if ( sw::MathUtil::abs( r - bgR ) + sw::MathUtil::abs( g - bgG ) + sw::MathUtil::abs( b - bgB ) < 24 )
                        continue;
                    ++arrDrawn[( x < layout._width / 2 ) ? 0u : 1u];
                }
            }

            const sw::string label    = sw::string( device->getBackendName() );
            const uint32     minDrawn = ( layout._width * layout._height ) / 3000;
            SW_EXPECT_TRUE_MSG( arrDrawn[0] > minDrawn && arrDrawn[1] > minDrawn,
                                ( label + ": 라이트 상자 밖의 큐브가 사라졌다 (좌 " + sw::to_string( arrDrawn[0] ) + ", 우 " +
                                  sw::to_string( arrDrawn[1] ) + ", 최소 " + sw::to_string( minDrawn ) +
                                  ") — 메인 패스가 카메라가 아니라 라이트 절두체로 걸러지고 있다" )
                                    .c_str() );
        }
        SW_EXPECT_TRUE_MSG( bOk, device->getBackendName() );

        if ( sharedMesh != nullptr )
            sharedMesh->releaseRhi( device.get() );
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend available for camera frustum cull test" );
}

/**
 * @brief [RenderPassTest] 한 배치 안의 투명 인스턴스가 백엔드마다 같은 순서로 섞이는지 (4 백엔드).
 * @details 컬링이 압축을 하면 자리 번호가 원자 연산의 **완료 순서**로 정해진다. 투명은 그 순서가 곧
 *          블렌딩 순서라 그대로 두면 그림이 틀린다. 그래서 컬링 뒤에 instancesort 가 깊이순으로 되돌린다.
 *
 *          DX11 은 간접 인자 제약으로 컬링을 아예 돌리지 않아 **CPU 가 정렬한 순서 그대로** 그린다.
 *          그래서 이 테스트에서 DX11 은 정답지 노릇을 한다 — 나머지 셋(압축 + GPU 정렬)이 DX11 과 같은
 *          그림을 내면 정렬이 순서를 제대로 되돌린 것이다.
 *
 *          씬은 **완전히 정적**이어야 한다(회전 시드 없음, 시간에 의존하는 것 없음). 안 그러면 백엔드마다
 *          측정 시각이 달라 비교 자체가 성립하지 않는다.
 */
SW_TEST_CASE( RenderPassGpuTest, TransparentOrderMatchesAcrossBackends )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    bool    bHasReference{ false };
    float32 referenceMean[3]{};
    uint32  referenceDrawn{ 0 };
    uint32  attemptedCount{ 0 };

    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "TransparentOrderScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        // 메시와 머티리얼 인스턴스를 **공유**한다 — 그래야 한 배치에 투명 인스턴스가 여럿 들어가고,
        // 배치 안의 정렬이 실제로 검사된다. 따로 주면 배치가 하나씩 갈려 검사할 순서가 없다.
        // 로드하지 않은 씬은 기본 머티리얼이 없다(getMaterial 이 null) — 에셋을 직접 읽고 알파만 낮춘다.
        sw::shared_ptr<sw::Mesh>     sharedMesh;
        sw::shared_ptr<sw::Material> glassMaterial;
        if ( bOk )
        {
            sharedMesh    = sw::MeshUtil::createUnitCube();
            glassMaterial = sw::Material::create();
            // 반투명 전용 에셋 — blendMode 와 퍼뮤테이션이 불투명과 다르다. 예전처럼 불투명 에셋에
            // 알파만 낮춰 쓰면 "머티리얼은 불투명인데 블렌딩으로 그린다"는 어긋난 상태를 검증하게 된다.
            bOk = sharedMesh != nullptr && glassMaterial->loadFromFile( "engine/materials/glassmaterial.material" );
        }

        if ( bOk )
        {
            // 카메라(0, 1.2, 3.2)에서 원점을 본다. 깊이를 어긋나게 겹쳐 놓아 순서가 그림을 바꾸게 한다.
            constexpr uint32 kCubeCount = 6;
            for ( uint32 cubeIndex = 0; cubeIndex < kCubeCount && bOk; ++cubeIndex )
            {
                sw::string      name = sw::string( "Glass" ) + sw::to_string( cubeIndex );
                sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( name.c_str(), static_cast<uint32>( name.size() ) ) );
                bOk                  = pObj != nullptr;
                if ( bOk == false )
                    break;
                sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
                bOk                          = pMeshComp != nullptr;
                if ( bOk == false )
                    break;
                pMeshComp->setMesh( sharedMesh );
                pMeshComp->setMaterial( glassMaterial.get() ); // 블렌드 모드는 이 머티리얼이 정한다
                const float32 offset = static_cast<float32>( cubeIndex ) * 0.30f;
                pMeshComp->setLocalPosition( sw::float3{ offset - 0.75f, 0.0f, offset - 0.75f } );
                // **큐브마다 다른 고정 회전**을 준다. 같은 색·같은 알파 레이어를 겹치면 블렌딩이 순서에
                // 무관해져(모든 src 가 같으면 결과가 교환법칙을 따른다) 정렬이 뒤집혀도 그림이 안 변한다.
                // 회전을 달리하면 보이는 면의 정점 색이 달라져 순서가 그림에 남는다. 시간이 아니라
                // 인덱스로 정하므로 백엔드·실행이 달라도 같다.
                const float32 yaw = static_cast<float32>( cubeIndex ) * 37.0f;
                pMeshComp->setLocalRotation( sw::float3{ 17.0f * static_cast<float32>( cubeIndex % 3 ), yaw, 0.0f } );
            }
        }

        if ( bOk )
        {
            const sw::float4 clear{ 0.0f, 0.0f, 0.0f, 1.0f };
            device->beginFrame( clear );
            bOk = renderer.execute( device.get(), &scene );
            device->endFrame( false, false );
            device->waitIdle();
        }

        sw::vector<uint8>     bytes;
        sw::RHITextureMipSpan layout{};
        sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
        if ( bOk && renderer.readbackTransient( "SceneColor", bytes, layout, format ) )
        {
            const bool   bBgra   = format == sw::RHIFormat::B8G8R8A8_UNORM;
            const uint8* pCorner = bytes.data();
            const int32  bgR     = bBgra ? pCorner[2] : pCorner[0];
            const int32  bgG     = pCorner[1];
            const int32  bgB     = bBgra ? pCorner[0] : pCorner[2];

            uint64 arrSum[3]{};
            uint32 drawn{ 0 };
            for ( uint32 y = 0; y < layout._height; ++y )
            {
                const uint8* pRow = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes;
                for ( uint32 x = 0; x < layout._width; ++x )
                {
                    const uint8* pPixel = pRow + static_cast<size_t>( x ) * 4;
                    const int32  r      = bBgra ? pPixel[2] : pPixel[0];
                    const int32  g      = pPixel[1];
                    const int32  b      = bBgra ? pPixel[0] : pPixel[2];
                    if ( sw::MathUtil::abs( r - bgR ) + sw::MathUtil::abs( g - bgG ) + sw::MathUtil::abs( b - bgB ) < 24 )
                        continue;
                    arrSum[0] += static_cast<uint64>( r );
                    arrSum[1] += static_cast<uint64>( g );
                    arrSum[2] += static_cast<uint64>( b );
                    ++drawn;
                }
            }

            const sw::string label = sw::string( device->getBackendName() );
            SW_EXPECT_TRUE_MSG( drawn > ( layout._width * layout._height ) / 400,
                                ( label + ": 투명 큐브가 그려지지 않았다 (" + sw::to_string( drawn ) + " px)" ).c_str() );
            if ( drawn > 0 )
            {
                const float32 mean[3] = { static_cast<float32>( arrSum[0] ) / static_cast<float32>( drawn ),
                                          static_cast<float32>( arrSum[1] ) / static_cast<float32>( drawn ),
                                          static_cast<float32>( arrSum[2] ) / static_cast<float32>( drawn ) };
                if ( bHasReference == false )
                {
                    bHasReference    = true;
                    referenceMean[0] = mean[0];
                    referenceMean[1] = mean[1];
                    referenceMean[2] = mean[2];
                    referenceDrawn   = drawn;
                }
                else
                {
                    // 정적 씬이라 백엔드끼리 그림이 같아야 한다. 블렌딩 순서가 어긋나면 겹친 자리의
                    // 색이 달라져 평균이 움직인다.
                    for ( uint32 channel = 0; channel < 3; ++channel )
                    {
                        const float32 diff = sw::MathUtil::abs( mean[channel] - referenceMean[channel] );
                        SW_EXPECT_TRUE_MSG( diff < 6.0f,
                                            ( label + ": 투명 블렌딩 결과가 기준 백엔드와 다르다 (채널 " +
                                              sw::to_string( channel ) + ", " + sw::to_string( mean[channel] ) + " vs " +
                                              sw::to_string( referenceMean[channel] ) + ") — 배치 안 정렬 순서가 어긋난다" )
                                                .c_str() );
                    }
                    const int32 drawnDiff = static_cast<int32>( drawn ) - static_cast<int32>( referenceDrawn );
                    SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( drawnDiff ) < static_cast<int32>( referenceDrawn / 8 + 64 ),
                                        ( label + ": 그려진 픽셀 수가 기준과 크게 다르다 (" + sw::to_string( drawn ) + " vs " +
                                          sw::to_string( referenceDrawn ) + ")" )
                                            .c_str() );
                }
            }
        }
        SW_EXPECT_TRUE_MSG( bOk, device->getBackendName() );

        if ( sharedMesh != nullptr )
            sharedMesh->releaseRhi( device.get() );
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend available for transparent order test" );
}

/**
 * @brief [RenderPassTest] 컴퓨트가 만든 드로우 커맨드가 **보이는 인스턴스만** 고르는지 (4 백엔드).
 * @details 컬링 컴퓨트는 배치의 개수를 줄이는 데서 끝나지 않고, 살아남은 인스턴스 번호를 압축 목록
 *          (g_SwVisibleInstanceIds)에 적는다. 정점 셰이더는 그 목록으로 자기 인스턴스를 찾는다 —
 *          언리얼 FInstanceCullingContext 와 같은 구조다.
 *
 *          개수만 줄이던 예전 방식은 "배치 앞쪽 N 개"를 그렸다. 한 배치 안에서 앞이 안 보이고 뒤가
 *          보이면 **보이는 쪽이 사라지고 안 보이는 쪽이 그려졌다**. 개수만으로는 무엇을 그릴지 고를 수가
 *          없기 때문이다.
 *
 *          그래서 **메시 하나를 여럿이 공유해 한 배치에 인스턴스를 여러 개** 만들고, 그중 절반을 카메라
 *          뒤로 보낸다. 화면에 남아야 할 둘이 좌우에 제대로 찍히는지 본다.
 */
SW_TEST_CASE( RenderPassGpuTest, GpuGeneratedCommandsDrawOnlyVisibleInstances )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    // 기본 카메라는 (0, 1.2, 3.2) 에서 원점을 본다 — -Z 를 보므로 월드 +X 는 화면 왼쪽이다.
    constexpr float32 kSideOffset = 1.2f;
    // 카메라 뒤(+Z 쪽 멀리)로 보내 절두체 밖에 둔다.
    constexpr float32 kBehindCameraZ = 40.0f;

    uint32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "GpuCullVisibleScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        // **메시 하나를 모두가 공유한다** — 배치 키에 메시가 들어가므로 배치가 하나로 묶이고, 그래야
        // 한 배치 안에서 일부만 컬링되는 상황이 만들어진다(배치마다 인스턴스가 하나면 검사할 게 없다).
        sw::shared_ptr<sw::Mesh> sharedMesh;
        if ( bOk )
        {
            sharedMesh = sw::MeshUtil::createUnitCube();
            bOk        = sharedMesh != nullptr;
        }

        if ( bOk )
        {
            // 앞의 넷은 카메라 뒤(안 보임), 뒤의 둘은 화면 좌우(보임).
            const sw::float3 arrPosition[] = {
                sw::float3{       -3.0f, 0.0f, kBehindCameraZ},
                sw::float3{       -1.0f, 0.0f, kBehindCameraZ},
                sw::float3{        1.0f, 0.0f, kBehindCameraZ},
                sw::float3{        3.0f, 0.0f, kBehindCameraZ},
                sw::float3{-kSideOffset, 0.0f,           0.0f},
                sw::float3{ kSideOffset, 0.0f,           0.0f},
            };
            constexpr uint32 kObjectCount = static_cast<uint32>( sizeof( arrPosition ) / sizeof( arrPosition[0] ) );
            for ( uint32 objectIndex = 0; objectIndex < kObjectCount && bOk; ++objectIndex )
            {
                sw::string      name = sw::string( "CullCube" ) + sw::to_string( objectIndex );
                sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( name.c_str(), static_cast<uint32>( name.size() ) ) );
                bOk                  = pObj != nullptr;
                if ( bOk )
                {
                    sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
                    bOk                          = pMeshComp != nullptr;
                    if ( bOk )
                    {
                        pMeshComp->setMesh( sharedMesh );
                        pMeshComp->setLocalPosition( arrPosition[objectIndex] );
                    }
                }
            }
        }

        if ( bOk )
        {
            const sw::float4 clear{ 0.0f, 0.0f, 0.0f, 1.0f };
            device->beginFrame( clear );
            bOk = renderer.execute( device.get(), &scene );
            device->endFrame( false, false );
            device->waitIdle();
        }

        sw::vector<uint8>     bytes;
        sw::RHITextureMipSpan layout{};
        sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
        if ( bOk && renderer.readbackTransient( "SceneColor", bytes, layout, format ) )
        {
            const bool   bBgra   = format == sw::RHIFormat::B8G8R8A8_UNORM;
            const uint8* pCorner = bytes.data();
            const int32  bgR     = bBgra ? pCorner[2] : pCorner[0];
            const int32  bgG     = pCorner[1];
            const int32  bgB     = bBgra ? pCorner[0] : pCorner[2];

            uint32 arrDrawn[2]{};
            for ( uint32 y = 0; y < layout._height; ++y )
            {
                const uint8* pRow = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes;
                for ( uint32 x = 0; x < layout._width; ++x )
                {
                    const uint8* pPixel = pRow + static_cast<size_t>( x ) * 4;
                    const int32  r      = bBgra ? pPixel[2] : pPixel[0];
                    const int32  g      = pPixel[1];
                    const int32  b      = bBgra ? pPixel[0] : pPixel[2];
                    if ( sw::MathUtil::abs( r - bgR ) + sw::MathUtil::abs( g - bgG ) + sw::MathUtil::abs( b - bgB ) < 24 )
                        continue;
                    ++arrDrawn[( x < layout._width / 2 ) ? 0u : 1u];
                }
            }

            const sw::string label    = sw::string( device->getBackendName() );
            const uint32     minDrawn = ( layout._width * layout._height ) / 400;
            SW_EXPECT_TRUE_MSG( arrDrawn[0] > minDrawn && arrDrawn[1] > minDrawn,
                                ( label + ": 보이는 큐브 둘이 화면 좌우에 남지 않았다 (좌 " + sw::to_string( arrDrawn[0] ) +
                                  ", 우 " + sw::to_string( arrDrawn[1] ) + ", 최소 " + sw::to_string( minDrawn ) +
                                  ") — 컬링이 보이는 인스턴스를 고르지 못한다" )
                                    .c_str() );
        }
        SW_EXPECT_TRUE_MSG( bOk, device->getBackendName() );

        if ( sharedMesh != nullptr )
            sharedMesh->releaseRhi( device.get() );
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend available for GPU-generated command test" );
}

/**
 * @brief [RenderPassTest] 배치마다 자기 머티리얼 **색**으로 그려지는지 (4 백엔드).
 * @details GpuSceneTest.PerBatchMaterialElementsAreDistinct 는 CPU 쪽 원소 선택까지만 본다. 여기서는 그
 *          원소가 실제로 셰이더까지 도달하는지를 픽셀로 본다 — 붉은 머티리얼과 푸른 머티리얼을 좌우에 두고
 *          그린다.
 *
 *          판정은 "붉은 픽셀 수" 가 아니라 **그려진 픽셀의 평균 (R - B)** 로 한다. 절대 색은 조명·톤매핑·
 *          백엔드 색공간에 따라 흔들리지만, 같은 조명을 받는 두 큐브 사이의 R-B 대소는 흔들리지 않는다.
 *          픽셀 수로 세었을 때는 백엔드마다 값이 널뛰어 판정이 되지 않았다.
 */
SW_TEST_CASE( RenderPassGpuTest, PerBatchMaterialColorsReachShader )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    // 기본 카메라는 -Z 를 본다 — 월드 +X 가 화면 **왼쪽**으로 간다. 그래서 붉은 큐브를 -X 에 두면
    // 화면 오른쪽이 붉어진다.
    constexpr float32 kSideOffset = 1.2f;

    uint32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "PerBatchMaterialColorScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        // 머티리얼은 **실제 에셋**을 읽어 색만 바꾼다 — 손으로 지은 XML 은 퍼뮤테이션 선언이 빠져 구워둔
        // 셰이더 변형과 맞지 않는다(그러면 드로우가 통째로 사라져 검증이 무의미해진다).
        // initialize 가 아니라 loadFromFile 을 쓴다 — initialize 는 텍스처 에셋 해석까지 하므로 에셋
        // 시스템이 없는 테스트 프로세스에서는 못 쓴다. GPUScene 은 머티리얼 상수버퍼가 아니라
        // getBuffer() 를 구조버퍼 원소로 패킹하므로 여기까지면 충분하다.
        auto makeMaterial = []( const utf8* pColor ) -> sw::shared_ptr<sw::Material>
        {
            // 반환 대상을 하나로 둔다 — nullptr 과 material 을 섞어 돌려주면 NRVO 가 걸리지 않는다.
            sw::shared_ptr<sw::Material> material = sw::Material::create();
            if ( material->loadFromFile( "engine/materials/defaultmaterial.material" ) == false ||
                 material->setPropertyValue( nullptr, sw::hashed_string( "color" ), pColor ) == false )
                material.reset();
            return material;
        };

        sw::shared_ptr<sw::Material> materialRed;
        sw::shared_ptr<sw::Material> materialBlue;
        if ( bOk )
        {
            materialRed  = makeMaterial( "1.0 0.02 0.02 1.0" );
            materialBlue = makeMaterial( "0.02 0.02 1.0 1.0" );
            bOk          = materialRed != nullptr && materialBlue != nullptr;
            SW_EXPECT_TRUE_MSG( bOk, "테스트 머티리얼 준비" );
        }

        // 메시도 따로 만든다 — 배치 키에 메시가 들어가므로 배치가 갈려 드로우가 둘이 된다.
        sw::shared_ptr<sw::Mesh> meshRed;
        sw::shared_ptr<sw::Mesh> meshBlue;
        if ( bOk )
        {
            meshRed  = sw::MeshUtil::createUnitCube();
            meshBlue = sw::MeshUtil::createUnitCube();
            bOk      = meshRed != nullptr && meshBlue != nullptr;
        }
        if ( bOk )
        {
            const sw::shared_ptr<sw::Mesh> arrMesh[2]     = { meshRed, meshBlue };
            sw::Material*                  arrMaterial[2] = { materialRed.get(), materialBlue.get() };
            const float32                  arrOffsetX[2]  = { -kSideOffset, kSideOffset };
            const utf8*                    arrName[2]     = { "CubeRed", "CubeBlue" };
            for ( uint32 sideIndex = 0; sideIndex < 2 && bOk; ++sideIndex )
            {
                sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( arrName[sideIndex] ) );
                bOk                  = pObj != nullptr;
                if ( bOk )
                {
                    sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
                    bOk                          = pMeshComp != nullptr;
                    if ( bOk )
                    {
                        pMeshComp->setMesh( arrMesh[sideIndex] );
                        pMeshComp->setMaterial( arrMaterial[sideIndex] );
                        pMeshComp->setLocalPosition( sw::float3{ arrOffsetX[sideIndex], 0.0f, 0.0f } );
                    }
                }
            }
        }

        if ( bOk )
        {
            const sw::float4 clear{ 0.0f, 0.0f, 0.0f, 1.0f };
            device->beginFrame( clear );
            bOk = renderer.execute( device.get(), &scene );
            device->endFrame( false, false );
            device->waitIdle();
        }

        sw::vector<uint8>     bytes;
        sw::RHITextureMipSpan layout{};
        sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
        if ( bOk && renderer.readbackTransient( "SceneColor", bytes, layout, format ) )
        {
            // 그려진 픽셀(검은 배경이 아닌 곳)만 모아 좌/우 절반의 평균 (R - B) 를 낸다.
            int64  arrSumDiff[2]{};
            uint32 arrDrawn[2]{};

            // 배경은 검지 않다 — 패스 리소스가 정한 클리어 색과 톤매핑이 섞여 회색빛이 깔린다. 그래서
            // "검지 않은 픽셀" 이 아니라 **모서리 픽셀과 확연히 다른 픽셀** 을 큐브로 본다.
            const bool   bBgra   = format == sw::RHIFormat::B8G8R8A8_UNORM;
            const uint8* pCorner = bytes.data();
            const int32  bgR     = bBgra ? pCorner[2] : pCorner[0];
            const int32  bgG     = pCorner[1];
            const int32  bgB     = bBgra ? pCorner[0] : pCorner[2];
            for ( uint32 y = 0; y < layout._height; ++y )
            {
                const uint8* pRow = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes;
                for ( uint32 x = 0; x < layout._width; ++x )
                {
                    const uint8* pPixel = pRow + static_cast<size_t>( x ) * 4;
                    const int32  r      = bBgra ? pPixel[2] : pPixel[0];
                    const int32  g      = pPixel[1];
                    const int32  b      = bBgra ? pPixel[0] : pPixel[2];
                    if ( sw::MathUtil::abs( r - bgR ) + sw::MathUtil::abs( g - bgG ) + sw::MathUtil::abs( b - bgB ) < 24 )
                        continue; // 배경
                    const uint32 side = ( x < layout._width / 2 ) ? 0u : 1u;
                    arrSumDiff[side] += ( r - b );
                    ++arrDrawn[side];
                }
            }

            const sw::string label    = sw::string( device->getBackendName() );
            const uint32     minDrawn = ( layout._width * layout._height ) / 400;
            const bool       bEnough  = arrDrawn[0] > minDrawn && arrDrawn[1] > minDrawn;
            SW_EXPECT_TRUE_MSG( bEnough, ( label + ": 큐브가 화면 양쪽에 그려지지 않았다 (좌 " + sw::to_string( arrDrawn[0] ) +
                                           ", 우 " + sw::to_string( arrDrawn[1] ) + ", 최소 " + sw::to_string( minDrawn ) + ")" )
                                             .c_str() );
            if ( bEnough )
            {
                const int64 leftDiff  = arrSumDiff[0] / static_cast<int64>( arrDrawn[0] );
                const int64 rightDiff = arrSumDiff[1] / static_cast<int64>( arrDrawn[1] );
                SW_EXPECT_TRUE_MSG( rightDiff > leftDiff + 16,
                                    ( label + ": 좌우가 같은 색으로 그려졌다 (좌 R-B " + sw::to_string( leftDiff ) +
                                      ", 우 R-B " + sw::to_string( rightDiff ) +
                                      ") — 배치가 자기 머티리얼 원소를 못 읽는다" )
                                        .c_str() );
            }
        }
        SW_EXPECT_TRUE_MSG( bOk, device->getBackendName() );

        if ( meshRed != nullptr )
            meshRed->releaseRhi( device.get() );
        if ( meshBlue != nullptr )
            meshBlue->releaseRhi( device.get() );
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend available for per-batch material color test" );
}

/**
 * @brief [RenderPassTest] 한 패스에 드로우가 둘일 때 배치마다 다른 상수가 유지되는지 (4 백엔드).
 * @details 배치 키에 메시 포인터가 들어가므로 **메시가 다르면 배치가 갈린다**. 그러면 한 패스가 드로우를
 *          두 번 하는데, 패스 상수버퍼는 `acquirePassCb` 가 패스당 하나만 잡고 `bindGraphics` 는 드로우마다
 *          거기에 덮어쓴다. GPU 는 제출 뒤에 읽으므로 두 드로우가 **마지막 배치의 `g_InstanceBase`** 를 보게 되고,
 *          앞 배치의 메시가 뒤 배치의 인스턴스 자리에 그려진다(= 한쪽이 비어 보인다).
 *
 *          지금까지 이 경로가 한 번도 검증되지 않았다 — 벤치 씬도 패리티 테스트도 메시를 하나만 쓴다.
 *          그래서 같은 큐브를 **두 번 따로 만들어** 포인터를 다르게 하고(기하는 동일해 가시성 변수를 없앤다)
 *          좌우로 떨어뜨린 뒤, 화면 좌우 양쪽에 모두 그려졌는지 본다.
 */
SW_TEST_CASE( RenderPassGpuTest, MultiBatchPassKeepsPerBatchConstants )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    /// @brief 두 큐브를 카메라가 보는 원점에서 좌우로 이만큼 떼어 놓는다.
    constexpr float32 kSideOffset = 1.1f;

    uint32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "MultiBatchScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        // 같은 기하지만 **다른 Mesh 객체** 두 개 — 배치 키가 갈려 한 패스에서 드로우가 둘이 된다.
        sw::shared_ptr<sw::Mesh> meshLeft;
        sw::shared_ptr<sw::Mesh> meshRight;
        if ( bOk )
        {
            meshLeft  = sw::MeshUtil::createUnitCube();
            meshRight = sw::MeshUtil::createUnitCube();
            bOk       = meshLeft != nullptr && meshRight != nullptr && meshLeft != meshRight;
        }
        if ( bOk )
        {
            const sw::shared_ptr<sw::Mesh> arrMesh[2]   = { meshLeft, meshRight };
            const float32                  arrOffset[2] = { -kSideOffset, kSideOffset };
            for ( uint32 sideIndex = 0; sideIndex < 2 && bOk; ++sideIndex )
            {
                sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( sideIndex == 0 ? "CubeLeft" : "CubeRight" ) );
                bOk                  = pObj != nullptr;
                if ( bOk )
                {
                    sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
                    bOk                          = pMeshComp != nullptr;
                    if ( bOk )
                    {
                        pMeshComp->setMesh( arrMesh[sideIndex] );
                        pMeshComp->setLocalPosition( sw::float3{ arrOffset[sideIndex], 0.0f, 0.0f } );
                    }
                }
            }
        }

        if ( bOk )
        {
            const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
            device->beginFrame( clear );
            bOk = renderer.execute( device.get(), &scene );
            device->endFrame( false, false );
            device->waitIdle();
        }
        SW_EXPECT_TRUE_MSG( bOk, device->getBackendName() );

        if ( bOk )
        {
            sw::vector<uint8>     bytes;
            sw::RHITextureMipSpan layout{};
            sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
            if ( renderer.readbackTransient( "SceneColor", bytes, layout, format ) )
            {
                // 화면을 좌/우로 나눠 각각 그려진 픽셀을 센다. 한 배치가 다른 배치의 인스턴스를 읽으면
                // 두 큐브가 같은 자리에 겹쳐 그려져 한쪽이 비어 버린다.
                uint32 arrSideCount[2]{};
                for ( uint32 y = 0; y < layout._height; ++y )
                {
                    const uint8* pRow = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes;
                    for ( uint32 x = 0; x < layout._width; ++x )
                    {
                        const uint8* pPixel = pRow + static_cast<size_t>( x ) * 4;
                        const uint8  r      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[2] : pPixel[0];
                        const uint8  b      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[0] : pPixel[2];
                        if ( r > 40 || pPixel[1] > 48 || b > 56 || r < 22 || pPixel[1] < 28 || b < 36 )
                            ++arrSideCount[x < layout._width / 2 ? 0 : 1];
                    }
                }

                const sw::string label      = sw::string( device->getBackendName() );
                const uint32     minPerSide = ( layout._width * layout._height ) / 400;
                SW_EXPECT_TRUE_MSG( arrSideCount[0] > minPerSide,
                                    ( label + ": 왼쪽 큐브가 없다 (left " + sw::to_string( arrSideCount[0] ) + ", right " +
                                      sw::to_string( arrSideCount[1] ) + ") — 배치마다 다른 상수가 유지되지 않는다" )
                                        .c_str() );
                SW_EXPECT_TRUE_MSG( arrSideCount[1] > minPerSide,
                                    ( label + ": 오른쪽 큐브가 없다 (left " + sw::to_string( arrSideCount[0] ) + ", right " +
                                      sw::to_string( arrSideCount[1] ) + ") — 배치마다 다른 상수가 유지되지 않는다" )
                                        .c_str() );
            }
        }

        if ( meshLeft != nullptr )
            meshLeft->releaseRhi( device.get() );
        if ( meshRight != nullptr )
            meshRight->releaseRhi( device.get() );
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend available for multi-batch pass test" );
}

/**
 * @brief 인스턴스 애니메이션 컴퓨트가 돈 뒤에도 인스턴스 버퍼를 정점 셰이더가 읽을 수 있어야 한다.
 * @details 이 프리패스는 인스턴스 버퍼를 **UAV 로 쓴 다음** 같은 프레임에 정점 셰이더가 SRV 로 읽는다.
 *          D3D11 은 같은 리소스를 출력과 입력에 동시에 걸 수 없어서, UAV 를 안 떼면 런타임이 SRV 를
 *          조용히 NULL 로 강제한다 — 경고만 나오고 화면에서는 전부 사라진다. 실제로 DX11 앱이 그랬다.
 *
 *          패리티 테스트가 이걸 못 잡았던 이유는 그 씬에 spinSeed 를 세운 인스턴스가 하나도 없어서
 *          디스패치 자체가 건너뛰어졌기 때문이다. 여기서는 **반드시 세운다**.
 *
 *          회전각은 시간에 따라 달라지므로 백엔드 사이 픽셀 수를 비교하지 않는다. 각 백엔드가
 *          "무언가를 그렸는지" 만 본다 — 이 버그의 증상이 정확히 "아무것도 안 그린다" 였다.
 */
SW_TEST_CASE( RenderPassGpuTest, InstanceAnimationKeepsInstancesReadable )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    uint32 attemptedCount{ 0 };

    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;

        ++attemptedCount;

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "InstanceAnimScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        constexpr uint32         kAnimMeshCount = 3;
        sw::shared_ptr<sw::Mesh> arrMesh[kAnimMeshCount];
        for ( uint32 meshIndex = 0; meshIndex < kAnimMeshCount && bOk; ++meshIndex )
        {
            arrMesh[meshIndex] = sw::MeshUtil::createUnitCube();
            bOk                = arrMesh[meshIndex] != nullptr;
            if ( bOk == false )
                break;

            sw::string      objectName = sw::string( "SpinCube" ) + sw::to_string( meshIndex );
            sw::GameObject* go         = scene.getObjectManager()->createGameObject( sw::hashed_string( objectName.c_str(), static_cast<uint32>( objectName.size() ) ) );
            bOk                        = go != nullptr;
            if ( bOk )
            {
                sw::MeshComponent* meshComp = go->addComponent<sw::MeshComponent>();
                bOk                         = meshComp != nullptr;
                if ( bOk )
                {
                    meshComp->setMesh( arrMesh[meshIndex] );
                    meshComp->setLocalPosition( sw::float3{ ( static_cast<float32>( meshIndex ) - 1.0f ) * 1.2f, 1.0f, 0.0f } );
                    // **이게 핵심이다.** 0 이 아니어야 dispatchInstanceAnimation 이 실제로 돈다.
                    meshComp->setGpuSpinSeed( meshIndex + 1u );
                }
            }
        }

        // 두 프레임 돌린다 — 첫 프레임에 UAV 로 쓰고 두 번째 프레임이 그걸 SRV 로 읽는 순서까지 태운다.
        for ( uint32 frame = 0; frame < 2 && bOk; ++frame )
        {
            const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
            device->beginFrame( clear );
            bOk = renderer.execute( device.get(), &scene );
            device->endFrame( false, false );
            device->waitIdle();
        }

        if ( bOk )
        {
            sw::vector<uint8>     bytes;
            sw::RHITextureMipSpan layout{};
            sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
            const bool            bRead  = renderer.readbackTransient( "SceneColor", bytes, layout, format );
            SW_EXPECT_TRUE_MSG( bRead, "SceneColor readback" );
            if ( bRead )
            {
                const uint32 pixelCount = layout._width * layout._height;
                uint32       drawnCount{ 0 };
                for ( uint32 y = 0; y < layout._height; ++y )
                {
                    const uint8* pRow = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes;
                    for ( uint32 x = 0; x < layout._width; ++x )
                    {
                        const uint8* pPixel = pRow + static_cast<size_t>( x ) * 4;
                        const uint8  r      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[2] : pPixel[0];
                        const uint8  b      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[0] : pPixel[2];
                        if ( r > 40 || pPixel[1] > 48 || b > 56 || r < 22 || pPixel[1] < 28 || b < 36 )
                            ++drawnCount;
                    }
                }
                const sw::string label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( backend ) );
                SW_EXPECT_TRUE_MSG( pixelCount > 0 && drawnCount > pixelCount / 200,
                                    ( label + ": 인스턴스 애니메이션 뒤 SceneColor 가 비었다 (drawn " + sw::to_string( drawnCount ) + "/" +
                                      sw::to_string( pixelCount ) + ") — UAV 를 떼지 않아 정점 셰이더가 인스턴스를 못 읽는지 의심하라" )
                                        .c_str() );
            }
        }

        for ( sw::shared_ptr<sw::Mesh>& mesh : arrMesh )
        {
            if ( mesh != nullptr )
                mesh->releaseRhi( device.get() );
        }
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();

        SW_EXPECT_TRUE_MSG( bOk, ( sw::string( "backend " ) + sw::to_string( static_cast<uint32>( backend ) ) + " execute" ).c_str() );
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend available for instance animation test" );
}

/**
 * @brief FrameRenderer 패리티 스모크 — DX11 / DX12 / Vulkan / OpenGL 각각 begin→execute→end(no present)
 * @details Present 없이 waitIdle까지. 가용 백엔드는 전부 성공해야 한다.
 */
SW_TEST_CASE( RenderPassGpuTest, FrameRendererParityAllBackends )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    /// @brief 큐브를 원점(카메라가 보는 지점)보다 이만큼 위에 둔다 — 그림을 세로로 비대칭하게 만들어 방향을 검사할 수 있게.
    constexpr float32 kParityCubeHeight = 1.0f;

    uint32  attemptedCount{ 0 };
    uint32  okCount{ 0 };
    bool    bHasReferenceMean{ false };
    float32 referenceMean[3]{};
    uint32  referenceDrawnCount{ 0 };

    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;

        ++attemptedCount;

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "FrameRendererParityScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        // 메시를 **여러 개** 만든다 — 배치 키에 메시가 들어가므로 곧 배치 수이고, 배치가 하나뿐이면
        // 인스턴스 시작 오프셋이 늘 0 이라 인다이렉트 드로우의 백엔드 차이를 전혀 재지 못한다
        // (Vulkan 의 gl_InstanceIndex 가 firstInstance 를 포함하는 문제가 그래서 오래 숨어 있었다).
        constexpr uint32         kParityMeshCount = 3;
        sw::shared_ptr<sw::Mesh> arrMesh[kParityMeshCount];
        sw::shared_ptr<sw::Mesh> cube;
        for ( uint32 meshIndex = 0; meshIndex < kParityMeshCount && bOk; ++meshIndex )
        {
            arrMesh[meshIndex] = sw::MeshUtil::createUnitCube();
            bOk                = arrMesh[meshIndex] != nullptr;
            if ( bOk == false )
                break;

            sw::string      objectName = sw::string( "Cube" ) + sw::to_string( meshIndex );
            sw::GameObject* go         = scene.getObjectManager()->createGameObject( sw::hashed_string( objectName.c_str(), static_cast<uint32>( objectName.size() ) ) );
            bOk                        = go != nullptr;
            if ( bOk )
            {
                sw::MeshComponent* meshComp = go->addComponent<sw::MeshComponent>();
                bOk                         = meshComp != nullptr;
                if ( bOk )
                {
                    meshComp->setMesh( arrMesh[meshIndex] );
                    meshComp->setLocalPosition( sw::float3{ ( static_cast<float32>( meshIndex ) - 1.0f ) * 1.2f, kParityCubeHeight, 0.0f } );
                    // 큐브를 카메라가 보는 원점보다 **위로** 올린다. 원점에 두면 화면 정중앙이라 그림이 세로로 대칭이고,
                    // 그러면 상하 반전을 평균으로도 무게중심으로도 잡을 수 없다 — OpenGL 이 실제로 뒤집혀 있었는데
                    // 이 테스트가 통과하던 이유다(평균·픽셀 수만 봤다).
                }
            }
        }
        cube = arrMesh[0];

        if ( bOk )
        {
            const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
            device->beginFrame( clear );
            bOk = renderer.execute( device.get(), &scene );
            device->endFrame( false, false );
            device->waitIdle();
        }

        // 실행 성공만으로는 부족하다 — 실제로 큐브가 찍혔는지, 백엔드끼리 같은 그림인지 SceneColor 픽셀로 본다.
        // (예전엔 여기가 비어 있어서 Vulkan 이 아무것도 안 그리고 GL 이 큐브를 한 자리에 겹쳐 그려도 통과했다.)
        if ( bOk )
        {
            sw::vector<uint8>     bytes;
            sw::RHITextureMipSpan layout{};
            sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
            const bool            bRead  = renderer.readbackTransient( "SceneColor", bytes, layout, format );
            SW_EXPECT_TRUE_MSG( bRead, "SceneColor readback" );
            if ( bRead )
            {
                const uint32 pixelCount = layout._width * layout._height;
                uint64       arrSum[3]{};
                uint32       drawnCount{ 0 };
                uint64       drawnSumY{ 0 };
                for ( uint32 y = 0; y < layout._height; ++y )
                {
                    const uint8* pRow = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes;
                    for ( uint32 x = 0; x < layout._width; ++x )
                    {
                        const uint8* pPixel = pRow + static_cast<size_t>( x ) * 4;
                        const uint8  r      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[2] : pPixel[0];
                        const uint8  b      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[0] : pPixel[2];
                        arrSum[0] += r;
                        arrSum[1] += pPixel[1];
                        arrSum[2] += b;
                        // 파이프라인 클리어 색(0.12, 0.15, 0.18 → 31, 38, 46) 이 아니면 무언가 그려진 픽셀이다.
                        if ( r > 40 || pPixel[1] > 48 || b > 56 || r < 22 || pPixel[1] < 28 || b < 36 )
                        {
                            ++drawnCount;
                            drawnSumY += y;
                        }
                    }
                }
                const sw::string label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( backend ) );
                SW_EXPECT_TRUE_MSG( pixelCount > 0 && drawnCount > pixelCount / 200,
                                    ( label + ": SceneColor 에 큐브가 없다 (drawn " + sw::to_string( drawnCount ) + "/" + sw::to_string( pixelCount ) + ")" ).c_str() );
                // **방향 검사** — 평균과 픽셀 수는 상하 반전에 무관하다. 큐브를 원점 위에 두었으므로 올바른 방향이면
                // 그려진 픽셀의 무게중심이 이미지 위쪽(행 번호가 작은 쪽)에 있어야 한다. 뒤집히면 아래쪽으로 간다.
                // OpenGL 이 glClipControl 없이 좌하단 원점으로 그리던 시절 이 단언이 잡는다.
                if ( drawnCount > 0 )
                {
                    const float32 centroidY = static_cast<float32>( drawnSumY ) / static_cast<float32>( drawnCount );
                    const float32 centerY   = static_cast<float32>( layout._height ) * 0.5f;
                    SW_EXPECT_TRUE_MSG( centroidY < centerY,
                                        ( label + ": 그림이 상하로 뒤집혔다 — 큐브 무게중심 y=" + sw::to_string( static_cast<int32>( centroidY ) ) +
                                          " 가 중앙 " + sw::to_string( static_cast<int32>( centerY ) ) + " 보다 아래다" )
                                            .c_str() );
                }

                float32 arrMean[3]{};
                for ( uint32 channel = 0; channel < 3; ++channel )
                    arrMean[channel] = pixelCount > 0 ? static_cast<float32>( arrSum[channel] ) / static_cast<float32>( pixelCount ) : 0.0f;
                if ( bHasReferenceMean == false )
                {
                    bHasReferenceMean   = true;
                    referenceDrawnCount = drawnCount;
                    for ( uint32 channel = 0; channel < 3; ++channel )
                        referenceMean[channel] = arrMean[channel];
                }
                else
                {
                    for ( uint32 channel = 0; channel < 3; ++channel )
                    {
                        const float32 diff = arrMean[channel] > referenceMean[channel] ? arrMean[channel] - referenceMean[channel] : referenceMean[channel] - arrMean[channel];
                        SW_EXPECT_TRUE_MSG( diff <= 3.0f, ( label + ": SceneColor 평균이 첫 백엔드와 다르다 (채널 " + sw::to_string( channel ) + ")" ).c_str() );
                    }

                    // **그려진 픽셀 수**도 맞춘다. 평균은 화면 전체로 나눈 값이라 큐브 몇 개가 겹쳐 사라져도
                    // 거의 안 움직인다 — 인다이렉트 드로우의 인스턴스 오프셋이 백엔드마다 다르게 먹던 버그가
                    // 그래서 이 테스트를 통과했다. 배치가 여럿일 때 한 백엔드만 큐브를 잃으면 여기서 걸린다.
                    const uint32 lowerBound = referenceDrawnCount - referenceDrawnCount / 8;
                    const uint32 upperBound = referenceDrawnCount + referenceDrawnCount / 8;
                    SW_EXPECT_TRUE_MSG( lowerBound <= drawnCount && drawnCount <= upperBound,
                                        ( label + ": 그려진 픽셀 수가 첫 백엔드와 다르다 (" + sw::to_string( drawnCount ) + " vs " +
                                          sw::to_string( referenceDrawnCount ) + ") — 배치별 인스턴스 오프셋을 의심하라" )
                                            .c_str() );
                }
            }
        }

        for ( sw::shared_ptr<sw::Mesh>& mesh : arrMesh )
        {
            if ( mesh != nullptr )
                mesh->releaseRhi( device.get() );
        }
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();

        if ( bOk )
            ++okCount;
        else
            SW_LOG_ERROR( "backend %# failed", static_cast<uint32>( backend ) );
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend available for FrameRenderer parity" );

    SW_EXPECT_TRUE( okCount >= 1 );
    SW_EXPECT_EQUAL( okCount, attemptedCount );
}

/**
 * @brief [RenderPassTest] 뷰 모드(Lit/Unlit/Wireframe)가 **PSO 를 실제로 가르는지** 검증.
 * @details 이 기능이 조용히 죽는 방식은 하나다 — 값은 바뀌는데 드로우가 고르는 PSO 는 그대로인 것.
 *          예전 툴바 콤보가 정확히 그 상태였다(값만 바뀌고 화면은 그대로). 그래서 여기서는 화면이
 *          아니라 **드로우가 고른 PSO 의 디스크립터**를 본다 — 픽셀 비교는 인스턴스 애니메이션이
 *          벽시계로 도는 탓에 프레임마다 달라져 판정 근거가 못 된다.
 *
 *          함께 보는 것: 그림자 패스는 뷰 모드를 **받지 않아야** 한다(와이어프레임 그림자를 구우면
 *          그림자가 선 몇 개로 남는다), 모드를 되돌리면 캐시에서 같은 PSO 가 다시 나와야 한다.
 */
SW_TEST_CASE( RenderPassGpuTest, ViewModeSelectsDistinctPipelineStates )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    auto hasDefine = []( const sw::RHIPipelineStateDesc& desc, const utf8* pDefine ) -> bool
    {
        for ( const sw::string& defineStr : desc._listShaderDefine )
        {
            if ( defineStr == pDefine )
                return true;
        }
        return false;
    };

    uint32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;

        const sw::string  label = sw::string( device->getBackendName() );
        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "ViewModeScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        sw::shared_ptr<sw::Mesh> mesh;
        if ( bOk )
        {
            mesh = sw::MeshUtil::createUnitCube();
            bOk  = mesh != nullptr;
        }
        if ( bOk )
        {
            sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( "ViewModeCube" ) );
            bOk                  = pObj != nullptr;
            if ( bOk )
            {
                sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
                bOk                          = pMeshComp != nullptr;
                if ( bOk )
                    pMeshComp->setMesh( mesh );
            }
        }

        // 모드를 바꾸고 한 프레임 돌리면 그 모드의 PSO 변형이 만들어진다. 그 뒤 드로우가 고를 PSO 를
        // 조회한다 — drawGpuBatches 가 배치마다 부르는 것과 같은 함수다.
        auto renderOneFrame = [&]() -> bool
        {
            const sw::float4 clear{ 0.0f, 0.0f, 0.0f, 1.0f };
            device->beginFrame( clear );
            const bool bFrameOk = renderer.execute( device.get(), &scene );
            device->endFrame( false, false );
            device->waitIdle();
            return bFrameOk;
        };

        if ( bOk )
            bOk = renderOneFrame();

        if ( bOk )
        {
            const sw::vector<sw::GpuMeshBatch>& batches = renderer.getGpuScene().getOpaqueBatches();
            SW_EXPECT_TRUE_MSG( batches.empty() == false, ( label + ": 불투명 배치가 없다" ).c_str() );

            const sw::RHIPipelineStateHandle passPso   = renderer.getEnginePso( sw::RenderPassType::ForwardOpaque );
            const sw::RHIPipelineStateHandle shadowPso = renderer.getEnginePso( sw::RenderPassType::Shadow );
            if ( batches.empty() == false && passPso != 0 )
            {
                const sw::GpuMeshBatch& batch = batches[0];

                // ── Lit: 패스 PSO 그대로, Solid ──────────────────────────────
                const sw::RHIPipelineStateHandle litPso = renderer.psoForBatch( passPso, batch );
                sw::RHIPipelineStateDesc         litDesc{};
                SW_EXPECT_TRUE_MSG( renderer.findPsoDesc( litPso, litDesc ),
                                    ( label + ": Lit PSO 의 디스크립터를 찾을 수 없다" ).c_str() );
                SW_EXPECT_TRUE_MSG( litDesc._fillMode == sw::RHIFillMode::Solid,
                                    ( label + ": Lit 인데 채우기 모드가 Solid 가 아니다" ).c_str() );
                SW_EXPECT_TRUE_MSG( hasDefine( litDesc, "SW_VIEWMODE_UNLIT=1" ) == false,
                                    ( label + ": Lit 인데 Unlit define 이 들어 있다" ).c_str() );

                // ── Wireframe ────────────────────────────────────────────────
                renderer.setViewMode( sw::RenderViewMode::Wireframe );
                SW_EXPECT_TRUE_MSG( renderOneFrame(), ( label + ": 와이어프레임 프레임 실행 실패" ).c_str() );

                const sw::RHIPipelineStateHandle wirePso = renderer.psoForBatch( passPso, batch );
                SW_EXPECT_TRUE_MSG( wirePso != litPso,
                                    ( label + ": 와이어프레임인데 드로우가 Lit 과 같은 PSO 를 고른다 — 모드가 화면에 닿지 않는다" )
                                        .c_str() );
                sw::RHIPipelineStateDesc wireDesc{};
                if ( renderer.findPsoDesc( wirePso, wireDesc ) )
                {
                    SW_EXPECT_TRUE_MSG( wireDesc._fillMode == sw::RHIFillMode::Wireframe,
                                        ( label + ": 와이어프레임 PSO 의 채우기 모드가 Wireframe 이 아니다" ).c_str() );
                    SW_EXPECT_TRUE_MSG( wireDesc._cullMode == sw::RHICullMode::None,
                                        ( label + ": 와이어프레임인데 컬링이 남아 뒷면 선이 사라진다" ).c_str() );
                    // 렌더 상태 중 **패스가 정하는 것**은 그대로여야 한다.
                    SW_EXPECT_TRUE_MSG( wireDesc._bEnableDepthTest == litDesc._bEnableDepthTest,
                                        ( label + ": 뷰 모드 변형이 패스의 뎁스 테스트를 바꿨다" ).c_str() );
                }
                else
                {
                    SW_EXPECT_TRUE_MSG( false, ( label + ": 와이어프레임 PSO 의 디스크립터를 찾을 수 없다" ).c_str() );
                }

                // 그림자 패스는 뷰 모드를 받지 않는다.
                sw::RHIPipelineStateDesc shadowDesc{};
                if ( shadowPso != 0 && renderer.findPsoDesc( renderer.psoForBatch( shadowPso, batch ), shadowDesc ) )
                {
                    SW_EXPECT_TRUE_MSG( shadowDesc._fillMode == sw::RHIFillMode::Solid,
                                        ( label + ": 그림자 패스가 와이어프레임으로 구워진다" ).c_str() );
                }

                // ── Unlit ────────────────────────────────────────────────────
                renderer.setViewMode( sw::RenderViewMode::Unlit );
                SW_EXPECT_TRUE_MSG( renderOneFrame(), ( label + ": Unlit 프레임 실행 실패" ).c_str() );

                const sw::RHIPipelineStateHandle unlitPso = renderer.psoForBatch( passPso, batch );
                SW_EXPECT_TRUE_MSG( unlitPso != litPso && unlitPso != wirePso,
                                    ( label + ": Unlit 이 다른 모드와 같은 PSO 를 고른다" ).c_str() );
                sw::RHIPipelineStateDesc unlitDesc{};
                if ( renderer.findPsoDesc( unlitPso, unlitDesc ) )
                {
                    SW_EXPECT_TRUE_MSG( hasDefine( unlitDesc, "SW_VIEWMODE_UNLIT=1" ),
                                        ( label + ": Unlit PSO 에 define 이 없다 — 조명이 그대로 컴파일된다" ).c_str() );
                    SW_EXPECT_TRUE_MSG( unlitDesc._fillMode == sw::RHIFillMode::Solid,
                                        ( label + ": Unlit 인데 채우기 모드가 Solid 가 아니다" ).c_str() );
                }
                else
                {
                    SW_EXPECT_TRUE_MSG( false, ( label + ": Unlit PSO 의 디스크립터를 찾을 수 없다" ).c_str() );
                }

                // ── 되돌리기: 캐시에서 같은 PSO 가 나와야 한다 ────────────────
                renderer.setViewMode( sw::RenderViewMode::Lit );
                SW_EXPECT_TRUE_MSG( renderOneFrame(), ( label + ": Lit 복귀 프레임 실행 실패" ).c_str() );
                SW_EXPECT_TRUE_MSG( renderer.psoForBatch( passPso, batch ) == litPso,
                                    ( label + ": Lit 로 돌아왔는데 다른 PSO 가 나온다 — 캐시가 모드를 구분하지 못한다" ).c_str() );
            }
        }
        else
        {
            SW_EXPECT_TRUE_MSG( false, ( label + ": 뷰 모드 씬 준비 실패" ).c_str() );
        }

        if ( mesh != nullptr )
            mesh->releaseRhi( device.get() );
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend for view mode test" );
}

/**
 * @brief 디바이스를 다시 만든 뒤에도 **같은 FrameRenderer** 가 다시 그리는지 — 백엔드 교체의 재현.
 * @details 앱의 교체 경로는 GT GpuScene 을 비우고, FrameRenderer 를 shutdown → 새 디바이스로 initialize 한다.
 *          **투명(유리) 머티리얼**로 잰다 — 불투명은 머티리얼 버퍼가 빠져도(폴백 0) 보이지만, 투명은 알파 0 이라
 *          사라진다. 실제로 그렇게 빈 화면이었다: `GpuScene::clear()` 가 그룹 목록만 지우고 경로→인덱스 맵을 남겨
 *          `materialGroupFor` 가 범위 밖 인덱스를 돌려줬다. 세 번 잰다: 첫 디바이스, 재생성 뒤 같은 렌더러, 재생성 뒤
 *          새 렌더러(대조군 — 렌더러에 남은 상태인지 디바이스 쪽인지 가른다).
 */
SW_TEST_CASE( RenderPassGpuTest, RendererSurvivesDeviceRecreate )
{
    sw::unique_ptr<sw::IWindow>    window;
    sw::shared_ptr<sw::IRHIDevice> device;
    const sw::RHIBackend           backends[] = { sw::RHIBackend::DirectX12, sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };
    sw::RHIBackend                 backend    = sw::RHIBackend::DirectX12;
    bool                           bOk{ false };
    for ( sw::RHIBackend candidate : backends )
    {
        if ( tryInitDeviceForFrameRenderer( candidate, window, device ) )
        {
            backend = candidate;
            bOk     = true;
            break;
        }
    }
    if ( bOk == false )
        SW_TEST_SKIP( "No RHI backend for device recreate test" );

    constexpr const utf8* kGlassMaterial = "engine/materials/glassmaterial.material";
    sw::Scene             scene( "DeviceRecreateScene" );
    SW_ASSERT_TRUE( scene.ensureDefaultCameras() );
    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    sw::GameObject*          go   = scene.getObjectManager()->createGameObject( sw::hashed_string( "Cube" ) );
    SW_ASSERT_NOT_NULL( go );
    sw::MeshComponent* mesh = go->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( mesh );
    mesh->setMesh( cube );
    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_ASSERT_TRUE( material->loadFromFile( kGlassMaterial ) );
    mesh->setMaterial( material.get() );

    sw::GpuSceneBuilder gtGpuScene;
    sw::FrameRenderer   renderer;
    SW_ASSERT_TRUE( renderer.initialize( device.get() ) );
    const int64 drawnFirst = renderPacketFramesAndCountDrawn( renderer, device.get(), scene, gtGpuScene );
    SW_EXPECT_TRUE_MSG( drawnFirst > 0, ( "첫 디바이스에서 유리 큐브가 안 그려진다 (drawn " + sw::to_string( drawnFirst ) + ")" ).c_str() );

    // ---- 앱의 교체 경로와 같은 순서로 내린다 ----
    renderer.shutdown();
    material->releaseRhi( device.get() );
    cube->releaseRhi( device.get() );
    gtGpuScene.clear();
    device->waitIdle();
    device->shutdown();
    device.reset();

    device = sw::RHI::createDevice( backend );
    SW_ASSERT_NOT_NULL( device.get() );
    device->setInitialWindow( window.get() );
    SW_ASSERT_TRUE( device->initialize() );
    SW_ASSERT_TRUE( material->initialize( device.get(), kGlassMaterial ) );

    // 같은 렌더러 객체를 새 디바이스로 다시 세운다 — 앱이 하는 그대로.
    SW_ASSERT_TRUE( renderer.initialize( device.get() ) );
    const int64 drawnReused = renderPacketFramesAndCountDrawn( renderer, device.get(), scene, gtGpuScene );
    SW_EXPECT_TRUE_MSG( drawnReused > 0, ( "재생성 뒤 같은 렌더러가 빈 화면을 낸다 (drawn " + sw::to_string( drawnReused ) + ")" ).c_str() );

    // 대조군: 새 렌더러 객체라면 그려지는가.
    renderer.shutdown();
    cube->releaseRhi( device.get() );
    gtGpuScene.clear();
    sw::FrameRenderer freshRenderer;
    SW_ASSERT_TRUE( freshRenderer.initialize( device.get() ) );
    const int64 drawnFresh = renderPacketFramesAndCountDrawn( freshRenderer, device.get(), scene, gtGpuScene );
    SW_EXPECT_TRUE_MSG( drawnFresh > 0, ( "재생성 뒤 새 렌더러도 빈 화면이다 (drawn " + sw::to_string( drawnFresh ) + ")" ).c_str() );

    freshRenderer.shutdown();
    material->releaseRhi( device.get() );
    cube->releaseRhi( device.get() );
    device->waitIdle();
    device->shutdown();
    device.reset();
    window->destroy();
    window.reset();
}

/**
 * @brief 업로드 큐가 그리기 **전에** 정점 버퍼를 만들어 두는지 — 그리고 두 번 만들지 않는지.
 * @details 렌더 스레드는 그리기만 해야 한다. 큐가 먼저 만들어 두면 RT 의 `Mesh::initRhi` 는 핸들을 읽는 일이 된다.
 *          여기서는 (1) flush 뒤에 상주하는지, (2) 같은 메시를 여러 배치가 써도 한 번만 만드는지(중복 요청이
 *          워커 둘을 돌려 버퍼 하나를 새게 하면 안 된다), (3) 이미 상주하면 요청 자체가 쌓이지 않는지를 본다.
 */
SW_TEST_CASE( RenderPassGpuTest, UploadQueueMakesMeshesResidentBeforeDraw )
{
    sw::unique_ptr<sw::IWindow>    window;
    sw::shared_ptr<sw::IRHIDevice> device;
    const sw::RHIBackend           backends[] = { sw::RHIBackend::DirectX12, sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };
    bool                           bOk{ false };
    for ( sw::RHIBackend candidate : backends )
    {
        if ( tryInitDeviceForFrameRenderer( candidate, window, device ) )
        {
            bOk = true;
            break;
        }
    }
    if ( bOk == false )
        SW_TEST_SKIP( "No RHI backend for upload queue test" );

    sw::GpuUploadQueue queue;
    queue.bindDevice( device.get(), &sw::engine::getTaskManager() );

    constexpr uint32         kMeshCount = 8;
    sw::shared_ptr<sw::Mesh> arrMesh[kMeshCount];
    for ( uint32 meshIndex = 0; meshIndex < kMeshCount; ++meshIndex )
    {
        arrMesh[meshIndex] = sw::MeshUtil::createUnitCube();
        SW_ASSERT_NOT_NULL( arrMesh[meshIndex].get() );
        SW_EXPECT_FALSE( arrMesh[meshIndex]->isRhiValid() );
        queue.requestMesh( arrMesh[meshIndex] );
        // 같은 메시를 한 번 더 요청해도 대기열은 늘지 않는다 — 워커 둘이 같은 메시를 만들면 버퍼 하나가 샌다.
        queue.requestMesh( arrMesh[meshIndex] );
    }
    SW_EXPECT_EQUAL( kMeshCount, queue.getPendingCount() );

    SW_EXPECT_EQUAL( kMeshCount, queue.flush() );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );

    for ( uint32 meshIndex = 0; meshIndex < kMeshCount; ++meshIndex )
    {
        SW_EXPECT_TRUE_MSG( arrMesh[meshIndex]->isRhiValid(),
                            ( "flush 뒤에도 상주하지 않는다 (index " + sw::to_string( meshIndex ) + ")" ).c_str() );
        SW_EXPECT_TRUE( arrMesh[meshIndex]->getVertexBuffer() != 0 );
    }

    // 이미 상주하면 요청이 쌓이지 않는다 — 매 프레임 GT 가 전부 요청해도 값이 싸야 한다.
    for ( uint32 meshIndex = 0; meshIndex < kMeshCount; ++meshIndex )
        queue.requestMesh( arrMesh[meshIndex] );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
    SW_EXPECT_EQUAL( 0u, queue.flush() );

    for ( uint32 meshIndex = 0; meshIndex < kMeshCount; ++meshIndex )
        arrMesh[meshIndex]->releaseRhi( device.get() );
    device->waitIdle();
    device->shutdown();
    device.reset();
    window->destroy();
    window.reset();
}

/**
 * @brief 디바이스가 죽으면 **어디서 죽었든** 세대가 올라가야 한다 — 그래야 GPU 핸들이 스스로 무효가 된다.
 * @details 세대 카운터가 있는 이유는 "GPU 핸들을 든 CPU 객체(Mesh · MaterialInstance)가 디바이스보다 오래 산다" 는 것
 *          하나다. 그런데 세대를 올리는 일이 `RHI` 매니저의 두 경로(shutdown · recreateDevice)에만 있었다 — `RHI::createDevice`
 *          로 직접 만든 디바이스(테스트가 그렇게 쓴다)는 죽어도 세대가 그대로라, 그 디바이스에 올라간 메시가 계속
 *          "상주" 라고 답했다. 그러면 `upload()` 가 새 디바이스에 옛 핸들을 그대로 돌려주고, `releaseGpu()` 는 죽은
 *          디바이스에 destroy 를 부른다(세대를 도입한 바로 그 UAF).
 */
/**
 * @brief 새 디바이스가 서면 등록부가 **스스로** 리소스를 되살린다 — 아무도 각 리소스를 손으로 다시 올리지 않는다.
 * @details 이 테스트가 지키는 것은 "어느 캐시를 다시 올려야 하는지 기억하지 않아도 된다" 이다. 예전에는
 *          `MaterialCache::reinitializeAll` 처럼 되살릴 목록을 바깥이 들고 있었고, 목록에서 빠진 것
 *          (텍스처가 그랬다)은 교체 뒤 조용히 비어 있었다. 아래 initAllFor 한 줄을 지우면 이 테스트가 빨개진다.
 */
SW_TEST_CASE( RenderPassGpuTest, RegistryRestoresResourcesOnNewDevice )
{
    sw::unique_ptr<sw::IWindow>    window;
    sw::shared_ptr<sw::IRHIDevice> device;
    const sw::RHIBackend           backends[] = { sw::RHIBackend::DirectX12, sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };
    sw::RHIBackend                 backend    = sw::RHIBackend::DirectX12;
    bool                           bOk{ false };
    for ( sw::RHIBackend candidate : backends )
    {
        if ( tryInitDeviceForFrameRenderer( candidate, window, device ) )
        {
            backend = candidate;
            bOk     = true;
            break;
        }
    }
    if ( bOk == false )
        SW_TEST_SKIP( "No RHI backend for render resource registry test" );

    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( cube.get() );
    SW_EXPECT_TRUE( cube->initRhi( device.get() ) );
    SW_EXPECT_TRUE( cube->isRhiValid() );

    device->waitIdle();
    device->shutdown();
    device.reset();
    SW_EXPECT_TRUE( cube->isRhiValid() == false );

    device = sw::RHI::createDevice( backend );
    SW_ASSERT_NOT_NULL( device.get() );
    device->setInitialWindow( window.get() );
    SW_ASSERT_TRUE( device->initialize() );

    // 여기가 요점이다 — 큐브를 **이름으로 부르지 않는다.** 등록부가 알아서 되살린다.
    sw::RHIRenderResource::initAllFor( device.get() );
    SW_EXPECT_TRUE_MSG( cube->isRhiValid(), "새 디바이스가 섰는데 등록부가 리소스를 되살리지 않았다" );

    // 남의 디바이스가 죽었다는 통보는 내 핸들을 건드리면 안 된다. forgetRhi 는 이 주소를 **비교만** 한다
    // (역참조하지 않는다) — 그래서 실재하지 않는 디바이스 주소로 계약을 그대로 확인할 수 있다.
    sw::IRHIDevice* pStranger = reinterpret_cast<sw::IRHIDevice*>( static_cast<std::uintptr_t>( 0x1 ) );
    sw::RHIRenderResource::forgetAllFor( pStranger );
    SW_EXPECT_TRUE_MSG( cube->isRhiValid(), "남의 디바이스가 죽었다는 통보에 내 핸들까지 비웠다" );

    // 내 디바이스의 통보에는 반응해야 한다.
    sw::RHIRenderResource::releaseAllFor( device.get() );
    SW_EXPECT_TRUE_MSG( cube->isRhiValid() == false, "내 디바이스의 해제 통보를 받고도 상주라고 답한다" );

    device->waitIdle();
    device->shutdown();
    device.reset();
    window->destroy();
    window.reset();
}

SW_TEST_CASE( RenderPassGpuTest, DeviceDeathInvalidatesGpuHandles )
{
    sw::unique_ptr<sw::IWindow>    window;
    sw::shared_ptr<sw::IRHIDevice> device;
    const sw::RHIBackend           backends[] = { sw::RHIBackend::DirectX12, sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };
    sw::RHIBackend                 backend    = sw::RHIBackend::DirectX12;
    bool                           bOk{ false };
    for ( sw::RHIBackend candidate : backends )
    {
        if ( tryInitDeviceForFrameRenderer( candidate, window, device ) )
        {
            backend = candidate;
            bOk     = true;
            break;
        }
    }
    if ( bOk == false )
        SW_TEST_SKIP( "No RHI backend for device generation test" );

    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( cube.get() );
    SW_EXPECT_TRUE( cube->initRhi( device.get() ) );
    SW_EXPECT_TRUE( cube->isRhiValid() );
    SW_EXPECT_TRUE( cube->getVertexBuffer() != 0 );

    // **releaseGpu 를 부르지 않고** 디바이스를 죽인다 — 실수로 잊은 경우가 바로 이 카운터가 막아야 할 상황이다.
    device->waitIdle();
    device->shutdown();
    device.reset();

    // **동작으로 단언한다** — 세대 번호든 수명 토큰이든 구현은 바뀔 수 있다. 바뀌면 안 되는 것은
    // "디바이스가 죽으면 그 핸들은 더 이상 상주가 아니다" 뿐이다.
    SW_EXPECT_TRUE_MSG( cube->isRhiValid() == false, "죽은 디바이스의 핸들을 아직 상주 라고 답한다" );

    // 새 디바이스에는 **새로** 올라가야 한다. 옛 핸들을 그대로 돌려주면 그 드로우는 남의 버퍼를 읽는다.
    device = sw::RHI::createDevice( backend );
    SW_ASSERT_NOT_NULL( device.get() );
    device->setInitialWindow( window.get() );
    SW_ASSERT_TRUE( device->initialize() );
    SW_EXPECT_TRUE( cube->initRhi( device.get() ) );
    SW_EXPECT_TRUE( cube->isRhiValid() );
    SW_EXPECT_TRUE( cube->getVertexBuffer() != 0 );

    cube->releaseRhi( device.get() );
    device->waitIdle();
    device->shutdown();
    device.reset();
    window->destroy();
    window.reset();
}

/**
 * @brief [RenderPassGpuTest] **디퍼드 파이프라인이 실제로 그리는지** 검증.
 * @details 이 파이프라인은 오래 아무것도 안 그리고 있었다. 실행 중에 고를 방법이 없었기 때문이다 —
 *          `FrameRenderer::initialize` 의 파이프라인 인자를 주는 호출부가 하나도 없었고, 그래서
 *          앱은 늘 포워드로 돌았다. 그 사이 세 가지가 조용히 썩었다:
 *          (1) 디퍼드 XML 이 풀스크린 패스에도 `_cullMode="Back"` 을 적어 두어 삼각형이 컬링됐고,
 *          (2) 머티리얼 셰이더가 SV_TARGET 하나만 내어 G버퍼 노멀이 클리어 값 그대로였고,
 *          (3) 스크린샷 기본 첨부가 `"SceneColor"` 리터럴이라 디퍼드는 한 장도 못 찍었다.
 *          셋 다 오류도 경고도 없었다 — 그림을 봐야만 드러난다. 그래서 이 테스트는 **픽셀**을 본다.
 * @note 고유 색 수로 본다. "비배경 픽셀 수" 는 클리어 색·톤매핑에 무너지지만, 화면이 통째로 한
 *       색이면(= 아무것도 안 그렸다) 고유 색은 반드시 1 이다.
 */
SW_TEST_CASE( RenderPassGpuTest, DeferredPipelineDrawsGeometry )
{
    sw::unique_ptr<sw::IWindow>    window;
    sw::shared_ptr<sw::IRHIDevice> device;
    const sw::RHIBackend           backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL, sw::RHIBackend::DirectX12 };
    bool bOk{ false };
    for ( sw::RHIBackend backend : backends )
    {
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) )
        {
            bOk = true;
            break;
        }
    }
    if ( bOk == false )
        SW_TEST_SKIP( "No RHI backend for deferred pipeline test" );

    sw::FrameRenderer renderer;
    SW_EXPECT_TRUE( renderer.initialize( device.get(), "engine/pipeline/deferredpipeline.xml" ) );
    SW_EXPECT_TRUE( renderer.isReady() );

    // 화면에 나가는 첨부를 파이프라인에 물어본다 — 이름을 테스트에 박아 두면 XML 이 바뀔 때 조용히 어긋난다.
    const sw::string presented( renderer.getPresentedAttachmentName() );
    SW_EXPECT_TRUE_MSG( presented.empty() == false, "디퍼드 파이프라인에 Present 패스 입력이 없다" );

    sw::Scene scene( "DeferredPipelineScene" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    sw::GameObject*          go   = scene.getObjectManager()->createGameObject( sw::hashed_string( "Cube" ) );
    SW_ASSERT_NOT_NULL( go );
    sw::MeshComponent* mesh = go->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( mesh );
    mesh->setMesh( cube );

    // 첫 프레임엔 GpuScene 업로드가 아직이라 그릴 게 없다 — 몇 프레임 돌린 뒤에 읽는다.
    constexpr uint32 kWarmupFrames = 4;
    const sw::float4 clear         = { 0.02f, 0.02f, 0.05f, 1.0f };
    for ( uint32 frame = 0; frame < kWarmupFrames; ++frame )
    {
        device->beginFrame( clear );
        SW_EXPECT_TRUE( renderer.execute( device.get(), &scene ) );
        device->endFrame( false, false );
        device->waitIdle();
    }

    sw::vector<uint8>     bytes;
    sw::RHITextureMipSpan layout{};
    sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
    const bool            bRead  = renderer.readbackTransient( presented, bytes, layout, format );
    SW_EXPECT_TRUE_MSG( bRead, "화면에 나간 첨부를 되읽지 못했다" );
    if ( bRead )
    {
        const uint32 bytesPerPixel = sw::getRhiFormatBytesPerPixel( format );
        uint32       distinct      = 0;
        uint64       arrSeen[16]{};
        for ( uint32 row = 0; row < layout._height && distinct < 2; ++row )
        {
            const uint8* pRow = bytes.data() + static_cast<size_t>( row ) * layout._rowBytes;
            for ( uint32 col = 0; col < layout._width && distinct < 2; ++col )
            {
                const uint8* pPixel = pRow + static_cast<size_t>( col ) * bytesPerPixel;
                uint64       key    = 0;
                for ( uint32 b = 0; b < bytesPerPixel && b < 8; ++b )
                    key |= static_cast<uint64>( pPixel[b] ) << ( b * 8 );
                bool bFound = false;
                for ( uint32 slot = 0; slot < distinct; ++slot )
                {
                    if ( arrSeen[slot] == key )
                    {
                        bFound = true;
                        break;
                    }
                }
                if ( bFound == false )
                    arrSeen[distinct++] = key;
            }
        }
        SW_EXPECT_TRUE_MSG( distinct >= 2,
                            "디퍼드 파이프라인이 화면을 한 색으로 채웠다 — 지오메트리가 하나도 안 그려졌다" );
    }

    if ( cube != nullptr )
        cube->releaseRhi( device.get() );

    renderer.shutdown();
    device->shutdown();
    device.reset();
    window->destroy();
    window.reset();
}

/**
 * @brief [RenderPassGpuTest] 씬 배치를 멀티 드로우로 묶어도 그림이 같고, 호출 수는 배치 수보다 적다
 * @details 배치마다 다른 값(인스턴스 시작·모프 풀·정점 풀 시작)을 배치 표(g_SwBatches)로 옮기고, 같은 PSO·머티리얼의 연속
 *          배치를 drawIndirect 한 번(멀티 드로우)으로 낸다. 인스턴스는 슬롯 1 의 인스턴스 슬롯 스트림(간접 인자의 startInstance 부터)
 *          으로 자기 자리를 얻고, 배치는 인스턴스에서 — 어느 통로가 틀려도 그림이 달라진다(엉뚱한 인스턴스·정점 구간). 그래서
 *          묶은 그림과 배치마다 부른 그림, 그리고 정점 풀 없이 그린 그림을 픽셀로 비교한다.
 */
SW_TEST_CASE( RenderPassGpuTest, MergedSceneDrawsMatchPerBatch )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    struct Snapshot
    {
        uint32  _drawnCount{ 0 };
        float32 _arrMean[3]{};
        uint32  _drawCallCount{ 0 };
        bool    _bOk{ false };
    };
    auto snapshot = []( sw::FrameRenderer& renderer, sw::IRHIDevice& device, sw::Scene& scene ) -> Snapshot
    {
        Snapshot         result{};
        constexpr uint32 kFrames = 4;
        const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
        for ( uint32 frame = 0; frame < kFrames; ++frame )
        {
            device.beginFrame( clear );
            if ( renderer.execute( &device, &scene ) == false )
                return result;
            device.endFrame( false, false );
            device.waitIdle();
        }
        result._drawCallCount = renderer.getLastIndirectDrawCallCount();

        sw::vector<uint8>     bytes;
        sw::RHITextureMipSpan layout{};
        sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
        if ( renderer.readbackTransient( "SceneColor", bytes, layout, format ) == false )
            return result;
        uint64 arrSum[3]{};
        for ( uint32 y = 0; y < layout._height; ++y )
        {
            const uint8* pRow = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes;
            for ( uint32 x = 0; x < layout._width; ++x )
            {
                const uint8* pPixel = pRow + static_cast<size_t>( x ) * 4;
                const uint8  r      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[2] : pPixel[0];
                const uint8  b      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[0] : pPixel[2];
                arrSum[0] += r;
                arrSum[1] += pPixel[1];
                arrSum[2] += b;
                if ( r > 40 || pPixel[1] > 48 || b > 56 || r < 22 || pPixel[1] < 28 || b < 36 )
                    ++result._drawnCount;
            }
        }
        const uint32 pixelCount = layout._width * layout._height;
        for ( uint32 channel = 0; channel < 3; ++channel )
            result._arrMean[channel] = pixelCount > 0 ? static_cast<float32>( arrSum[channel] ) / static_cast<float32>( pixelCount ) : 0.0f;
        result._bOk = pixelCount > 0;
        return result;
    };

    uint32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;
        const sw::string label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( backend ) );

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "MergedDrawScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        // 메시 종류가 곧 배치 수다 — 여섯 도형 × 둘 = 배치 여섯(같은 메시는 한 배치). 정점 풀 시작이 배치마다 다르다.
        sw::shared_ptr<sw::Mesh> arrMesh[]  = { sw::MeshUtil::createUnitCube(), sw::MeshUtil::createSphere(), sw::MeshUtil::createCone(),
                                                sw::MeshUtil::createCylinder(), sw::MeshUtil::createCapsule(), sw::MeshUtil::createPlane() };
        constexpr uint32         kMeshCount = static_cast<uint32>( sizeof( arrMesh ) / sizeof( arrMesh[0] ) );
        for ( uint32 objectIndex = 0; objectIndex < kMeshCount * 2 && bOk; ++objectIndex )
        {
            sw::shared_ptr<sw::Mesh>& mesh = arrMesh[objectIndex % kMeshCount];
            bOk                            = mesh != nullptr;
            if ( bOk == false )
                break;
            sw::string      objectName = sw::string( "Merged" ) + sw::to_string( objectIndex );
            sw::GameObject* go         = scene.getObjectManager()->createGameObject( sw::hashed_string( objectName.c_str(), static_cast<uint32>( objectName.size() ) ) );
            bOk                        = go != nullptr;
            if ( bOk )
            {
                sw::MeshComponent* meshComp = go->addComponent<sw::MeshComponent>();
                bOk                         = meshComp != nullptr;
                if ( bOk )
                {
                    meshComp->setMesh( mesh );
                    meshComp->setLocalPosition( sw::float3{ ( static_cast<float32>( objectIndex % 4 ) - 1.5f ) * 1.4f, 0.5f + static_cast<float32>( objectIndex / 4 ) * 1.2f, 0.0f } );
                }
            }
        }
        SW_EXPECT_TRUE_MSG( bOk, ( label + ": 씬·렌더러 준비 실패" ).c_str() );

        if ( bOk )
        {
            // 기준: 정점 풀 없이(메시마다 자기 정점 버퍼, startVertex 0) 배치마다 그린 그림. 도형이 여섯 가지라 풀의 startVertex 나
            // SV_VertexID 를 백엔드가 다르게 다루면 엉뚱한 도형이 나와 여기서 갈린다(같은 큐브만 쓰면 못 잡는다).
            renderer.setVertexPoolEnabled( false );
            renderer.setDrawMergeEnabled( false );
            const Snapshot noPool = snapshot( renderer, *device, scene );
            renderer.setVertexPoolEnabled( true );
            const Snapshot perBatch = snapshot( renderer, *device, scene );
            renderer.setDrawMergeEnabled( true );
            const Snapshot merged = snapshot( renderer, *device, scene );
            SW_EXPECT_TRUE_MSG( noPool._bOk && noPool._drawnCount > 0, ( label + ": 풀 없이 그린 그림을 못 읽었다" ).c_str() );
            SW_EXPECT_TRUE_MSG( perBatch._bOk && perBatch._drawnCount > 0, ( label + ": 배치마다 그린 그림을 못 읽었다" ).c_str() );
            SW_EXPECT_TRUE_MSG( merged._bOk, ( label + ": 묶어 그린 그림을 못 읽었다" ).c_str() );
            if ( noPool._bOk && perBatch._bOk )
            {
                const uint32 tolerance = noPool._drawnCount / 100 + 8;
                const uint32 low       = noPool._drawnCount > tolerance ? noPool._drawnCount - tolerance : 0;
                SW_EXPECT_TRUE_MSG( low <= perBatch._drawnCount && perBatch._drawnCount <= noPool._drawnCount + tolerance,
                                    ( label + ": 정점 풀로 그린 그림의 픽셀 수가 다르다 (pool " + sw::to_string( perBatch._drawnCount ) + " vs no-pool " +
                                      sw::to_string( noPool._drawnCount ) + ") — startVertex 가 엉뚱한 도형을 가리킨다" )
                                        .c_str() );
                for ( uint32 channel = 0; channel < 3; ++channel )
                {
                    const float32 diff = perBatch._arrMean[channel] > noPool._arrMean[channel] ? perBatch._arrMean[channel] - noPool._arrMean[channel]
                                                                                               : noPool._arrMean[channel] - perBatch._arrMean[channel];
                    SW_EXPECT_TRUE_MSG( diff <= 1.5f, ( label + ": 정점 풀로 그린 그림의 평균이 다르다 (채널 " + sw::to_string( channel ) + ")" ).c_str() );
                }
            }
            if ( perBatch._bOk && merged._bOk )
            {
                const uint32 tolerance = perBatch._drawnCount / 100 + 8;
                const uint32 low       = perBatch._drawnCount > tolerance ? perBatch._drawnCount - tolerance : 0;
                SW_EXPECT_TRUE_MSG( low <= merged._drawnCount && merged._drawnCount <= perBatch._drawnCount + tolerance,
                                    ( label + ": 묶은 그림의 픽셀 수가 다르다 (merged " + sw::to_string( merged._drawnCount ) + " vs per-batch " +
                                      sw::to_string( perBatch._drawnCount ) + ") — 배치 번호가 엉뚱한 배치를 가리킨다" )
                                        .c_str() );
                for ( uint32 channel = 0; channel < 3; ++channel )
                {
                    const float32 diff = merged._arrMean[channel] > perBatch._arrMean[channel] ? merged._arrMean[channel] - perBatch._arrMean[channel]
                                                                                               : perBatch._arrMean[channel] - merged._arrMean[channel];
                    SW_EXPECT_TRUE_MSG( diff <= 1.5f, ( label + ": 묶은 그림의 평균이 다르다 (채널 " + sw::to_string( channel ) + ")" ).c_str() );
                }
                // 멀티 드로우가 되는 백엔드는 호출 수가 줄어야 한다 — 같으면 묶지 않은 것이다. DX11 은 늘 배치마다 하나다.
                if ( device->getCapabilities()._bMultiDrawIndirect != SW_FALSE )
                {
                    SW_EXPECT_TRUE_MSG( merged._drawCallCount * 2 <= perBatch._drawCallCount && merged._drawCallCount > 0,
                                        ( label + ": 멀티 드로우로 묶이지 않았다 (merged " + sw::to_string( merged._drawCallCount ) + " vs per-batch " +
                                          sw::to_string( perBatch._drawCallCount ) + " 호출)" )
                                            .c_str() );
                }
                else
                {
                    SW_EXPECT_TRUE_MSG( merged._drawCallCount == perBatch._drawCallCount,
                                        ( label + ": 멀티 드로우가 없는 백엔드인데 호출 수가 다르다" ).c_str() );
                }
            }
        }

        for ( sw::shared_ptr<sw::Mesh>& mesh : arrMesh )
        {
            if ( mesh != nullptr )
                mesh->releaseRhi( device.get() );
        }
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend for the merged draw test" );
}

/**
 * @brief [RenderPassGpuTest] SSAO 결과가 실제로 그림에 닿는다 — 끄면 밝아진다
 * @details 디퍼드 XML 은 처음부터 Bloom 의 입력으로 AOColor 를 선언했지만, 엔진은 그 패스에 SourceColor 하나만 걸었고
 *          postbloom.hlsl 도 그것만 읽었다 — SSAO 는 매 프레임 풀스크린 패스를 돌고 결과는 버려졌다(백로그 1-6).
 *          "패스가 돈다" 와 "결과가 쓰인다" 는 다른 말이라, AO 역할을 끈 프레임과 켠 프레임의 Bloom 출력을 비교한다.
 */
SW_TEST_CASE( RenderPassGpuTest, AmbientOcclusionReachesBloom )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    /// @brief 첨부 하나의 평균 밝기와 "가림이 있는(255 미만)" 픽셀 수.
    struct Stat
    {
        float64 _mean{ 0.0 };
        uint32  _darkCount{ 0 };
        bool    _bOk{ false };
    };
    auto readStat = []( sw::FrameRenderer& renderer, const utf8* pAttachment ) -> Stat
    {
        Stat                  stat{};
        sw::vector<uint8>     bytes;
        sw::RHITextureMipSpan layout{};
        sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
        if ( renderer.readbackTransient( pAttachment, bytes, layout, format ) == false )
            return stat;
        const uint32 bytesPerPixel = sw::getRhiFormatBytesPerPixel( format );
        const bool   bHalf         = ( format == sw::RHIFormat::R16G16B16A16_FLOAT );
        uint64       sum{ 0 };
        uint32       count{ 0 };
        for ( uint32 row = 0; row < layout._height; ++row )
        {
            const uint8* pRow = bytes.data() + static_cast<size_t>( row ) * layout._rowBytes;
            for ( uint32 col = 0; col < layout._width; ++col )
            {
                const uint8* pPixel = pRow + static_cast<size_t>( col ) * bytesPerPixel;
                uint32       r{ 0 };
                if ( bHalf )
                {
                    // IEEE 반정밀도 → float. 정규 수만 풀면 된다(색은 [0, 몇] 범위) — 비정규·무한은 0 으로 본다.
                    const uint16 half     = static_cast<uint16>( pPixel[0] | ( pPixel[1] << 8 ) );
                    const uint32 exponent = ( half >> 10 ) & 0x1Fu;
                    const uint32 mantissa = half & 0x3FFu;
                    float32      value    = 0.0f;
                    if ( exponent != 0 && exponent != 0x1Fu )
                        value = ( 1.0f + static_cast<float32>( mantissa ) / 1024.0f ) * std::ldexp( 1.0f, static_cast<int32>( exponent ) - 15 );
                    if ( ( half & 0x8000u ) != 0 )
                        value = 0.0f;
                    r = static_cast<uint32>( sw::MathUtil::clamp( value, 0.0f, 1.0f ) * 255.0f );
                }
                else
                    r = pPixel[0];
                sum += r;
                ++count;
                if ( r < 250 )
                    ++stat._darkCount;
            }
        }
        stat._mean = count > 0 ? static_cast<float64>( sum ) / static_cast<float64>( count ) : 0.0;
        stat._bOk  = count > 0;
        return stat;
    };

    uint32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;
        const sw::string label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( backend ) );

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get(), "engine/pipeline/deferredpipeline.xml" ) && renderer.isReady();

        sw::Scene scene( "AmbientOcclusionScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();
        // 바닥 위의 큐브 — 맞닿는 자리와 실루엣에서 깊이·노멀이 꺾여 SSAO 가 값을 낸다.
        sw::shared_ptr<sw::Mesh> cube  = sw::MeshUtil::createUnitCube();
        sw::shared_ptr<sw::Mesh> floor = sw::MeshUtil::createPlane();
        if ( bOk )
        {
            sw::GameObject* pCubeGo  = scene.getObjectManager()->createGameObject( sw::hashed_string( "AoCube" ) );
            sw::GameObject* pFloorGo = scene.getObjectManager()->createGameObject( sw::hashed_string( "AoFloor" ) );
            bOk                      = pCubeGo != nullptr && pFloorGo != nullptr && cube != nullptr && floor != nullptr;
            if ( bOk )
            {
                sw::MeshComponent* pCubeMesh  = pCubeGo->addComponent<sw::MeshComponent>();
                sw::MeshComponent* pFloorMesh = pFloorGo->addComponent<sw::MeshComponent>();
                bOk                           = pCubeMesh != nullptr && pFloorMesh != nullptr;
                if ( bOk )
                {
                    pCubeMesh->setMesh( cube );
                    pCubeMesh->setLocalPosition( sw::float3{ 0.0f, 0.5f, 0.0f } );
                    pFloorMesh->setMesh( floor );
                    pFloorMesh->setLocalScale( sw::float3{ 6.0f, 1.0f, 6.0f } );
                }
            }
        }
        SW_EXPECT_TRUE_MSG( bOk, ( label + ": 씬·렌더러 준비 실패" ).c_str() );

        auto renderFrames = [&]( uint32 frameCount )
        {
            const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
            for ( uint32 frame = 0; frame < frameCount; ++frame )
            {
                device->beginFrame( clear );
                if ( renderer.execute( device.get(), &scene ) == false )
                    return false;
                device->endFrame( false, false );
                device->waitIdle();
            }
            return true;
        };

        if ( bOk )
        {
            // 켬: AO 첨부에 가림이 있고, Bloom 출력은 그것을 곱한 값이다.
            renderer.setInputRoleEnabled( sw::RenderPassInputRole::AmbientOcclusion, true );
            SW_EXPECT_TRUE( renderFrames( 4 ) );
            const Stat ao      = readStat( renderer, "AOColor" );
            const Stat bloomOn = readStat( renderer, "BloomColor" );
            SW_EXPECT_TRUE_MSG( ao._bOk && ao._darkCount > 0, ( label + ": SSAO 가 가림을 하나도 내지 않았다 (AOColor 가 전부 흰색)" ).c_str() );

            // 끔: 같은 씬에서 AO 만 빠지면 Bloom 출력이 밝아져야 한다. 같으면 AO 가 그림에 닿지 않는 것이다.
            renderer.setInputRoleEnabled( sw::RenderPassInputRole::AmbientOcclusion, false );
            SW_EXPECT_TRUE( renderFrames( 4 ) );
            const Stat bloomOff = readStat( renderer, "BloomColor" );
            renderer.setInputRoleEnabled( sw::RenderPassInputRole::AmbientOcclusion, true );

            SW_EXPECT_TRUE_MSG( bloomOn._bOk && bloomOff._bOk, ( label + ": BloomColor 를 되읽지 못했다" ).c_str() );
            if ( bloomOn._bOk && bloomOff._bOk )
            {
                SW_EXPECT_TRUE_MSG( bloomOn._mean < bloomOff._mean,
                                    ( label + ": AO 를 꺼도 Bloom 출력이 같다 (켬 " + sw::to_string( static_cast<float32>( bloomOn._mean ) ) + " vs 끔 " +
                                      sw::to_string( static_cast<float32>( bloomOff._mean ) ) + ") — SSAO 결과를 아무도 읽지 않는다" )
                                        .c_str() );
            }
        }

        if ( cube != nullptr )
            cube->releaseRhi( device.get() );
        if ( floor != nullptr )
            floor->releaseRhi( device.get() );
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend for the ambient occlusion test" );
}

/**
 * @brief [RenderPassGpuTest] 렌더 타깃 목록이 **에디터가 읽을 수 있게** 공개된다
 * @details 에디터는 `FrameRenderer` 인스턴스를 쥘 방법이 없다 — 트랜지언트는 private 맵이다.
 *          그래서 "지금 G버퍼에 뭐가 들어 있나" 를 보려면 `-gv_screenshotAttachment` 로 프로세스를
 *          다시 띄워 PPM 을 찍는 수밖에 없었다. 이제 렌더러가 `RenderTargetRegistry` 에 목록을
 *          공개하고 `RenderTargetPanel` 이 그것을 읽는다.
 * @note 패널 자체는 ImGui 라 테스트가 붙지 않는다. 대신 **패널이 먹는 데이터**를 여기서 고정한다 —
 *       목록이 비거나 이름이 바뀌면 패널은 조용히 빈 창이 된다(그게 이 계약의 유일한 실패 모드다).
 */
SW_TEST_CASE( RenderPassGpuTest, RenderTargetsArePublishedForTheEditor )
{
    sw::unique_ptr<sw::IWindow>    window;
    sw::shared_ptr<sw::IRHIDevice> device;
    const sw::RHIBackend           backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL, sw::RHIBackend::DirectX12 };
    bool bOk{ false };
    for ( sw::RHIBackend backend : backends )
    {
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) )
        {
            bOk = true;
            break;
        }
    }
    if ( bOk == false )
        SW_TEST_SKIP( "No RHI backend for render target registry test" );

    sw::RenderTargetRegistry* pRegistry = sw::engine::getRenderTargetRegistry();
    SW_ASSERT_NOT_NULL( pRegistry );
    sw::RenderTargetRegistry& registry = *pRegistry;

    sw::FrameRenderer renderer;
    SW_EXPECT_TRUE( renderer.initialize( device.get(), "engine/pipeline/deferredpipeline.xml" ) );
    SW_EXPECT_TRUE( renderer.isReady() );

    sw::vector<sw::RenderTargetInfo> listTarget;
    registry.snapshot( listTarget );
    SW_EXPECT_TRUE_MSG( listTarget.empty() == false, "렌더러가 트랜지언트를 만들고도 목록을 공개하지 않았다" );

    // 패널이 보여 줄 것들 — 디퍼드의 핵심 단계가 이름으로 들어 있어야 한다.
    auto hasTarget = [&listTarget]( const utf8* pName ) -> bool
    {
        for ( const sw::RenderTargetInfo& info : listTarget )
        {
            if ( info._name == pName )
                return true;
        }
        return false;
    };
    SW_EXPECT_TRUE_MSG( hasTarget( "GBufferAlbedo" ), "G버퍼 알베도가 목록에 없다" );
    SW_EXPECT_TRUE_MSG( hasTarget( "GBufferNormal" ), "G버퍼 노멀이 목록에 없다" );
    SW_EXPECT_TRUE_MSG( hasTarget( "LitColor" ), "디퍼드 조명 결과가 목록에 없다" );

    // 값이 채워져 있어야 패널이 크기·포맷을 보여 줄 수 있다.
    for ( const sw::RenderTargetInfo& info : listTarget )
    {
        SW_EXPECT_TRUE_MSG( info._texture != 0, "목록에 텍스처 핸들이 0 인 항목이 있다" );
        SW_EXPECT_TRUE_MSG( info._width > 0 && info._height > 0, "목록에 크기가 0 인 항목이 있다" );
        // 깊이 첨부는 미리보기를 하지 않는다 — 그 판정이 여기서 이미 서 있어야 한다.
        if ( info._name == "SceneDepth" || info._name == "ShadowMap" )
            SW_EXPECT_TRUE_MSG( info._bDepth != SW_FALSE, "깊이 첨부가 깊이로 표시되지 않았다" );
    }

    // 목록은 **바뀔 때만** 세대가 오른다 — 패널이 매 프레임 복사하지 않는 근거다.
    const uint64 generation = registry.getGeneration();
    SW_EXPECT_EQUAL( generation, registry.getGeneration() );

    // 렌더러가 내려가면 목록도 비워진다 — 죽은 핸들을 UI 가 집으면 백엔드가 죽는다.
    renderer.shutdown();
    registry.snapshot( listTarget );
    SW_EXPECT_TRUE_MSG( listTarget.empty(), "렌더러가 내려갔는데 목록에 죽은 핸들이 남았다" );
    SW_EXPECT_TRUE_MSG( registry.getGeneration() != generation, "목록이 바뀌었는데 세대가 그대로다" );

    device->shutdown();
    device.reset();
    window->destroy();
    window.reset();
}

/**
 * @brief GPU 메시 모프의 **정점 셰이더 풀 읽기**가 네 백엔드에서 같은지 픽셀로 봅니다.
 * @details 세 장을 찍는다 — (A) 모프 안 켬(레스트), (B) 모프 켜되 컴퓨트 없이 **레스트 버퍼를 그대로 풀에
 *          물림**(`setMeshMorphDiag(2)`), (C) 진짜 모프. B 의 정답은 A 와 **같은 그림**이다: 풀 원소 i 가
 *          정점 i 의 레스트 값이므로 정점 셰이더가 제 원소를 읽으면 레스트와 픽셀이 같아야 한다.
 *          OpenGL 드라이버가 early-return 모양의 `SwMorphElementOf` 를 잘못 컴파일해 정점마다 **한 칸 앞
 *          원소**를 읽던 버그가 정확히 B≠A 로 나타난다(binding.hlsli 주석). C 는 "모프가 실제로 걸리는가"
 *          만 본다 — 시간에 따라 움직이므로 A 와 **달라야** 한다.
 *          이 케이스가 없던 동안 GL 은 능력표로 꺼 두어 조용히 레스트를 그렸고, 원인은 두 세션 동안 셰이더
 *          바깥(업로드·바인딩·인덱싱)에서 헛되이 찾았다.
 */
SW_TEST_CASE( RenderPassGpuTest, MorphPoolIdentityMatchesRest )
{
    const sw::RHIBackend backends[] = {
        sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL };

    /// @brief 그림 하나의 요약 — 그려진 픽셀 수와 채널 평균, 그리고 픽셀 비교용 원본.
    struct Snapshot
    {
        uint32            _drawnCount{ 0 };
        float32           _arrMean[3]{};
        bool              _bOk{ false };
        sw::vector<uint8> _bytes;
        uint32            _width{ 0 };
        uint32            _height{ 0 };
        uint32            _rowBytes{ 0 };
    };
    /// @brief 두 그림에서 어느 채널이든 8 이상 다른 픽셀 수 — 실루엣 수보다 튼튼한 지표(변형은 음영도 바꾼다).
    auto countDifferentPixels = []( const Snapshot& a, const Snapshot& b ) -> uint32
    {
        if ( a._width != b._width || a._height != b._height )
            return 0;
        uint32 count{ 0 };
        for ( uint32 y = 0; y < a._height; ++y )
        {
            const uint8* pRowA = a._bytes.data() + static_cast<size_t>( y ) * a._rowBytes;
            const uint8* pRowB = b._bytes.data() + static_cast<size_t>( y ) * b._rowBytes;
            for ( uint32 x = 0; x < a._width; ++x )
            {
                const uint8* pA = pRowA + static_cast<size_t>( x ) * 4;
                const uint8* pB = pRowB + static_cast<size_t>( x ) * 4;
                for ( uint32 channel = 0; channel < 3; ++channel )
                {
                    const int32 diff = static_cast<int32>( pA[channel] ) - static_cast<int32>( pB[channel] );
                    if ( diff > 8 || diff < -8 )
                    {
                        ++count;
                        break;
                    }
                }
            }
        }
        return count;
    };

    auto snapshot = []( sw::FrameRenderer& renderer, sw::IRHIDevice& device, sw::Scene& scene ) -> Snapshot
    {
        Snapshot result{};
        // 풀 빌드·GpuScene 업로드가 한 프레임 늦으므로 몇 프레임 돌린 뒤 읽는다.
        constexpr uint32 kFrames = 4;
        const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
        for ( uint32 frame = 0; frame < kFrames; ++frame )
        {
            device.beginFrame( clear );
            if ( renderer.execute( &device, &scene ) == false )
                return result;
            device.endFrame( false, false );
            device.waitIdle();
        }

        sw::vector<uint8>     bytes;
        sw::RHITextureMipSpan layout{};
        sw::RHIFormat         format = sw::RHIFormat::R8G8B8A8_UNORM;
        if ( renderer.readbackTransient( "SceneColor", bytes, layout, format ) == false )
            return result;

        uint64 arrSum[3]{};
        for ( uint32 y = 0; y < layout._height; ++y )
        {
            const uint8* pRow = bytes.data() + static_cast<size_t>( y ) * layout._rowBytes;
            for ( uint32 x = 0; x < layout._width; ++x )
            {
                const uint8* pPixel = pRow + static_cast<size_t>( x ) * 4;
                const uint8  r      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[2] : pPixel[0];
                const uint8  b      = format == sw::RHIFormat::B8G8R8A8_UNORM ? pPixel[0] : pPixel[2];
                arrSum[0] += r;
                arrSum[1] += pPixel[1];
                arrSum[2] += b;
                // 파이프라인 클리어 색(31, 38, 46) 이 아니면 그려진 픽셀이다 — FrameRendererParityAllBackends 와 같은 기준.
                if ( r > 40 || pPixel[1] > 48 || b > 56 || r < 22 || pPixel[1] < 28 || b < 36 )
                    ++result._drawnCount;
            }
        }
        const uint32 pixelCount = layout._width * layout._height;
        for ( uint32 channel = 0; channel < 3; ++channel )
            result._arrMean[channel] = pixelCount > 0 ? static_cast<float32>( arrSum[channel] ) / static_cast<float32>( pixelCount ) : 0.0f;
        result._bOk      = pixelCount > 0;
        result._bytes    = std::move( bytes );
        result._width    = layout._width;
        result._height   = layout._height;
        result._rowBytes = layout._rowBytes;
        return result;
    };

    uint32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : backends )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;
        const sw::string label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( backend ) );

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "MorphPoolIdentityScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        // 메시를 **여럿** 둔다 — 풀 시작 오프셋(g_MorphVertexBase)이 배치마다 달라야 "오프셋 + vid" 가 실제로 검증된다.
        constexpr uint32         kMeshCount = 3;
        sw::shared_ptr<sw::Mesh> arrMesh[kMeshCount];
        for ( uint32 meshIndex = 0; meshIndex < kMeshCount && bOk; ++meshIndex )
        {
            arrMesh[meshIndex] = sw::MeshUtil::createUnitCube();
            bOk                = arrMesh[meshIndex] != nullptr;
            if ( bOk == false )
                break;
            sw::string      objectName = sw::string( "MorphCube" ) + sw::to_string( meshIndex );
            sw::GameObject* go         = scene.getObjectManager()->createGameObject( sw::hashed_string( objectName.c_str(), static_cast<uint32>( objectName.size() ) ) );
            bOk                        = go != nullptr;
            if ( bOk )
            {
                sw::MeshComponent* meshComp = go->addComponent<sw::MeshComponent>();
                bOk                         = meshComp != nullptr;
                if ( bOk )
                {
                    meshComp->setMesh( arrMesh[meshIndex] );
                    meshComp->setLocalPosition( sw::float3{ ( static_cast<float32>( meshIndex ) - 1.0f ) * 1.2f, 1.0f, 0.0f } );
                }
            }
        }
        SW_EXPECT_TRUE_MSG( bOk, ( label + ": 씬·렌더러 준비 실패" ).c_str() );

        if ( bOk )
        {
            // (A) 레스트 — 모프를 요청하지 않은 메시는 입력 스트림 그대로 그려진다.
            renderer.setMeshMorphDiag( 0 );
            const Snapshot rest = snapshot( renderer, *device, scene );
            SW_EXPECT_TRUE_MSG( rest._bOk && rest._drawnCount > 0, ( label + ": 레스트 그림을 못 읽었다" ).c_str() );

            // (B) 풀 항등 — 모프를 켜되 컴퓨트를 건너뛰고 레스트 버퍼를 정점 셰이더에 물린다. 정답은 A 다.
            for ( sw::shared_ptr<sw::Mesh>& mesh : arrMesh )
                mesh->setGpuMorphEnabled( true );
            renderer.setMeshMorphDiag( 2 );
            const Snapshot identity = snapshot( renderer, *device, scene );
            SW_EXPECT_TRUE_MSG( identity._bOk, ( label + ": 풀 항등 그림을 못 읽었다" ).c_str() );
            if ( rest._bOk && identity._bOk )
            {
                const uint32 tolerance = rest._drawnCount / 50 + 8; // 2% + 가장자리 AA 여유
                const uint32 low       = rest._drawnCount > tolerance ? rest._drawnCount - tolerance : 0;
                SW_EXPECT_TRUE_MSG( low <= identity._drawnCount && identity._drawnCount <= rest._drawnCount + tolerance,
                                    ( label + ": 풀에서 읽은 레스트가 입력 스트림의 레스트와 다르다 (drawn " + sw::to_string( identity._drawnCount ) +
                                      " vs " + sw::to_string( rest._drawnCount ) + ") — 정점 셰이더가 다른 원소를 읽고 있다" )
                                        .c_str() );
                for ( uint32 channel = 0; channel < 3; ++channel )
                {
                    const float32 diff = identity._arrMean[channel] > rest._arrMean[channel] ? identity._arrMean[channel] - rest._arrMean[channel]
                                                                                             : rest._arrMean[channel] - identity._arrMean[channel];
                    SW_EXPECT_TRUE_MSG( diff <= 2.0f, ( label + ": 풀 항등 그림의 평균이 레스트와 다르다 (채널 " + sw::to_string( channel ) + ")" ).c_str() );
                }
            }

            // (C) 진짜 모프 — 컴퓨트가 정점을 밀었으니 레스트와 **달라야** 한다. 같으면 모프가 아예 안 걸린 것이다.
            renderer.setMeshMorphDiag( 1 );
            const Snapshot morphed = snapshot( renderer, *device, scene );
            SW_EXPECT_TRUE_MSG( morphed._bOk, ( label + ": 모프 그림을 못 읽었다" ).c_str() );
            if ( rest._bOk && morphed._bOk )
            {
                // 지표는 **달라진 픽셀 수**다. 예전엔 실루엣(그려진 픽셀 수)의 차를 봤는데 변위가 sin(시간) 이라 시간에 따라
                // 실루엣 차가 0 근처를 지나가 흔들렸다(드로우 루프가 빨라지자 DX 에서 떨어졌다). 변형은 위치와 노멀을 같이
                // 바꾸므로 음영이 바뀐 픽셀까지 세면 어느 시점에도 그려진 픽셀의 수 % 이상이 다르다.
                const uint32 diffCount = countDifferentPixels( rest, morphed );
                SW_EXPECT_TRUE_MSG( diffCount > rest._drawnCount / 20,
                                    ( label + ": 모프를 켰는데 그림이 레스트와 같다 (달라진 픽셀 " + sw::to_string( diffCount ) + " / 그려진 " +
                                      sw::to_string( rest._drawnCount ) + ") — 컴퓨트 결과가 정점 셰이더에 닿지 않는다" )
                                        .c_str() );
            }
            renderer.setMeshMorphDiag( -1 );
        }

        for ( sw::shared_ptr<sw::Mesh>& mesh : arrMesh )
        {
            if ( mesh != nullptr )
                mesh->releaseRhi( device.get() );
        }
        renderer.shutdown();
        device->shutdown();
        device.reset();
        window->destroy();
        window.reset();
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend for the morph pool identity test" );
}

/**
 * @brief [RenderPassGpuTest] forgetRhi 뒤 다시 올려도 텍스처 서수가 **처음부터** 다시 센다
 * @details `forgetRhi` 와 `releaseRhi` 는 둘 다 "디바이스가 사라졌다" 는 통보인데, 남기는 상태가
 *          달랐다 — `releaseRhi` 만 빌린 텍스처 목록을 비웠다. 그래서 forget 뒤에 `initRhi` 가
 *          오면 `resolveTextureAssets` 가 목록에 **덧붙였다.**
 *
 *          그러면 `ordinal`(= `_listMaterialTextureSrv.size()`)이 0 이 아닌 값에서 시작한다.
 *          네이티브 bindless 가 없는 백엔드(DX11 · GL)는 그 서수를 **t5..t8 고정 슬롯 번호**로
 *          쓰므로 엉뚱한 텍스처를 읽거나, 한도(`kMaterialTextureCount`)를 넘어 흰색으로 남는다 —
 *          "백엔드를 바꾸면 화면이 이상해진다" 로만 보이는 종류다.
 *
 *          `forgetRhi` 는 `~IRHIDevice()` 의 안전망 경로에서 온다(shutdown 을 거치지 않고 사라지는
 *          디바이스). 정상 종료는 `releaseRhi` 라서 평소에는 드러나지 않는다.
 */
SW_TEST_CASE( RenderPassGpuTest, ForgetThenInitDoesNotDoubleMaterialTextureOrdinals )
{
    int32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : { sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::DirectX11, sw::RHIBackend::OpenGL } )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;

        {
            // 텍스처가 붙은 실제 에셋이어야 한다 — 손으로 지은 XML 은 이 저장소를 여러 번 물었다.
            sw::shared_ptr<sw::Material> material = sw::Material::create();
            SW_ASSERT_TRUE( material->initialize( device.get(), "engine/materials/benchtextured.material" ) );

            const size_t firstCount = material->getMaterialTextureSrvs().size();
            SW_ASSERT_TRUE( firstCount > 0 );

            // 디바이스가 정상 종료를 거치지 않고 사라진 경우의 통보.
            material->forgetRhi( device.get() );
            SW_EXPECT_TRUE_MSG( material->getMaterialTextureSrvs().empty(),
                                "forgetRhi 가 빌린 텍스처 목록을 남겼습니다" );

            // 새 디바이스가 서서 다시 올린다.
            SW_EXPECT_TRUE( material->initRhi( device.get() ) );
            SW_EXPECT_TRUE_MSG( material->getMaterialTextureSrvs().size() == firstCount,
                                "다시 올린 뒤 텍스처 서수가 누적됐습니다 — DX11 · GL 이 엉뚱한 슬롯을 읽습니다" );

            material->releaseRhi( device.get() );
        }

        device->shutdown();
        device.reset();
        if ( window != nullptr )
        {
            window->destroy();
            window.reset();
        }
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend for the material texture ordinal test" );
}

/**
 * @brief [RenderPassGpuTest] 부모 레이아웃이 커지면 인스턴스 상수버퍼를 **다시 만든다**
 * @details `updateConstantBuffer( 버퍼, 데이터, 크기 )` 에는 적혀 있지 않은 전제가 있었다 — 그
 *          크기는 버퍼를 만들 때 준 크기를 넘으면 안 된다. 네 백엔드 중 셋(DX12 · Vulkan · DX11)은
 *          받은 크기를 **그대로 복사**하므로 넘기면 프레임 슬롯 밖까지 쓴다. GL 만
 *          `glBufferSubData` 가 막아 준다 — 한 백엔드에서만 조용히 안전했다는 뜻이다.
 *
 *          그런데 부모 머티리얼의 상수버퍼는 **셰이더를 다시 구우면 커질 수 있다**(레이아웃이
 *          바뀐다). `MaterialInstance::updateRhi` 는 `_constant._buffer` 가 0 이 아니면 그대로
 *          쓰고 새 크기로 갱신했다. 라이브 셰이더 편집 + 파라미터 변경이 겹치면 그 자리를 밟는다.
 *
 *          GPU 메모리로 넘치는 것이라 ASan 도 단언도 잡지 못한다. 대신 **버퍼를 다시 만들었는지**
 *          를 본다 — 다시 만들면 bindless 디스크립터 인덱스가 새로 발급되므로 그것으로 가른다.
 */
SW_TEST_CASE( RenderPassGpuTest, InstanceConstantBufferIsRecreatedWhenLayoutGrows )
{
    int32 attemptedCount{ 0 };
    for ( sw::RHIBackend backend : { sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::DirectX11, sw::RHIBackend::OpenGL } )
    {
        sw::unique_ptr<sw::IWindow>    window;
        sw::shared_ptr<sw::IRHIDevice> device;
        if ( tryInitDeviceForFrameRenderer( backend, window, device ) == false )
            continue;
        ++attemptedCount;

        {
            sw::shared_ptr<sw::Material> parent = sw::Material::create();
            SW_ASSERT_TRUE( parent->initialize( device.get(), "engine/materials/defaultmaterial.material" ) );

            sw::shared_ptr<sw::MaterialInstance> instance = sw::MaterialInstance::create( parent.get() );
            SW_ASSERT_TRUE( instance->updateRhi( device.get() ) );

            const sw::RHIDescriptorIndex firstIndex = instance->getDescriptorIndex();
            const size_t                 firstSize  = instance->getBuffer().size();
            SW_ASSERT_TRUE( firstSize > 0 );

            // 셰이더를 다시 구워 레이아웃이 커진 상황을 만든다 — 부모 상수버퍼가 256 을 넘게 한다.
            sw::ShaderReflectionData reflection{};
            sw::ShaderBufferInfo     cb{};
            cb._name      = "MaterialCB";
            cb._totalSize = static_cast<uint32>( firstSize ) + 256;

            sw::ShaderVariableInfo var{};
            var._name   = "grownTail";
            var._type   = "Float4";
            var._offset = static_cast<uint32>( firstSize ) + 16;
            var._size   = 16;
            cb._listVariable.push_back( var );
            reflection._listConstantBuffer.push_back( cb );
            (void)parent->syncPropertiesFromReflection( reflection );
            SW_ASSERT_TRUE( parent->getBuffer().size() > firstSize );

            // 파라미터를 건드려 인스턴스를 더럽힌다 — 이것이 실제로 겹치는 조합이다.
            instance->setScalarParameter( sw::hashed_string( "roughness" ), 0.75f );
            SW_ASSERT_TRUE( instance->updateRhi( device.get() ) );

            SW_EXPECT_TRUE_MSG( instance->getBuffer().size() > firstSize,
                                "인스턴스가 커진 부모 레이아웃을 따라가지 않았습니다" );
            SW_EXPECT_TRUE_MSG( instance->getDescriptorIndex() != firstIndex,
                                "상수버퍼를 다시 만들지 않고 더 큰 크기로 갱신했습니다 — 슬롯 밖으로 씁니다" );

            instance->releaseRhi( device.get() );
            parent->releaseRhi( device.get() );
        }

        device->shutdown();
        device.reset();
        if ( window != nullptr )
        {
            window->destroy();
            window.reset();
        }
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No RHI backend for the instance constant buffer growth test" );
}
