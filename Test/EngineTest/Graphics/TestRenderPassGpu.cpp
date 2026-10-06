#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/FrameArenaAllocator.h"
#include "Core/String/StringUtil.h"
#include "Core/String/hashed_string.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/Codec/Raw/RawAnimCodec.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Environment/Water/WaterWaveMath.h"
#include "Engine/Graphics/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/Renderer/Capture/PortraitRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/RenderFramePacket.h"
#include "Engine/Graphics/Renderer/Frame/TransientAttachmentPool.h"
#include "Engine/Graphics/Renderer/Graph/RenderGraph.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassAsset.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineAssetCache.h"
#include "Engine/Graphics/Renderer/Scene/GpuMeshMorphPool.h"
#include "Engine/Graphics/Renderer/Scene/GpuMeshVertexPool.h"
#include "Engine/Graphics/Renderer/Scene/GpuScene.h"
#include "Engine/Graphics/Renderer/Scene/GpuSceneBuilder.h"
#include "Engine/Graphics/Renderer/Scene/GpuSceneSnapshot.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Graphics/Upload/GpuUploadQueue.h"
#include "Engine/Object/Component/2D/Light2DComponent.h"
#include "Engine/Object/Component/2D/PixelPerfectCameraComponent.h"
#include "Engine/Object/Component/2D/ShadowCaster2DComponent.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/CameraRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Window/IWindow.h"

#include "EngineTest/AnimationTestUtil.h"
#include "EngineTest/RHITestDevice.h"
#include "EngineTest/RHITestImage.h"

#include "TestFramework/TestFramework.h"

// 프레임 렌더러를 실제 디바이스 위에서 돌린다 — 패스 · 파이프라인 · 백엔드 패리티를 픽셀로 본다.
//
// 실제 GPU 디바이스를 만든다. **디바이스가 필요한 케이스는 전부 이 스위트에 넣는다** — 이 규칙을 비켜 CI 로 들어간
// 케이스는 Windows 러너의 WARP 가 초기화에 성공해 픽셀 검증이 실제로 돌고 진다.

SW_TEST_REQUIRES_HOST( RenderPassGpuTest, "runs FrameRenderer on a real GPU device and reads pixels back" );

namespace
{
    /**
     * @brief 첨부 하나의 R 채널 평균(0~255)을 되읽습니다. 실패하면 0.
     * @details 반정밀도(R16G16B16A16_FLOAT) 첨부는 [0,1] 로 클램프해 환산한다 — 두 판을 비교하는 데만
     *          쓰므로 정확한 휘도가 아니라 **같은 규칙으로 잰 같은 수**이면 된다.
     */
    float64 readMeanChannel( sw::FrameRenderer& renderer, const utf8* pAttachment )
    {
        test::RHITestImage image;
        if ( image.readTransient( renderer, pAttachment ) == false )
            return 0.0;
        uint64 sum{ 0 };
        uint64 count{ 0 };
        for ( uint32 row = 0; row < image.getHeight(); ++row )
        {
            for ( uint32 col = 0; col < image.getWidth(); ++col )
            {
                sum += image.getPixel( col, row )._r;
                ++count;
            }
        }
        return count > 0 ? static_cast<float64>( sum ) / static_cast<float64>( count ) : 0.0;
    }

    /**
     * @brief 씬을 한 프레임 그리고(Present 없이) GPU 가 끝날 때까지 기다립니다. `execute` 의 결과를 돌려줍니다.
     * @details 렌더 스레드와 같은 순서다 — beginFrame → execute → endFrame. 실패해도 프레임은 닫는다.
     */
    bool renderSceneFrame( sw::FrameRenderer& renderer, sw::IRHIDevice* pDevice, sw::Scene& scene, const sw::float4& clear )
    {
        pDevice->beginFrame( clear );
        const bool bExecuted = renderer.execute( pDevice, &scene );
        pDevice->endFrame( false, false );
        pDevice->waitIdle();
        return bExecuted;
    }

    /**
     * @brief 한 프레임을 그리고 Present 전에 백버퍼를 텍스처로 읽어 옵니다(창에 나갈 그림 — 0 행이 화면 위). 실패면 false.
     * @details 백버퍼는 Present 뒤 내용이 버려지므로 프레임 스트림에 복사를 기록한 뒤 endFrame 한다.
     */
    bool renderSceneFrameReadingBackBuffer( sw::FrameRenderer& renderer, sw::IRHIDevice* pDevice, sw::Scene& scene, const sw::float4& clear,
                                            test::RHITestImage& outImage )
    {
        sw::IRHIResourceFactory* pFactory = pDevice->getResourceFactory();
        sw::RHITextureDesc       desc{};
        desc._width                         = pDevice->getBackBufferWidth();
        desc._height                        = pDevice->getBackBufferHeight();
        desc._format                        = pDevice->getBackBufferFormat();
        desc._bIsRenderTarget               = SW_TRUE; // GL 블릿 대상은 FBO 가 있어야 한다
        desc._bIsShaderResource             = SW_TRUE;
        const sw::RHITextureHandle backCopy = pFactory->createTexture2D( desc );
        if ( backCopy == 0 )
            return false;

        pDevice->beginFrame( clear );
        bool                    bOk     = renderer.execute( pDevice, &scene );
        sw::IRHICommandContext* pStream = pDevice->getFrameStreamContext();
        bOk                             = bOk && pStream != nullptr;
        if ( bOk )
            pStream->blitTexture( 0, backCopy );
        pDevice->endFrame( false, false );
        pDevice->waitIdle();

        sw::vector<uint8>     bytes;
        sw::RHITextureMipSpan layout{};
        bOk = bOk && pFactory->readbackTexture2D( backCopy, 0, 0, bytes, layout );
        if ( bOk )
            outImage.assign( std::move( bytes ), layout, desc._format );
        pFactory->destroyTexture( backCopy );
        return bOk;
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
            if ( packet._gpuScene.getInstances().empty() )
                return -1;
            pDevice->beginFrame( clear );
            const bool bOk = renderer.executePacket( pDevice, packet );
            pDevice->endFrame( true, false );
            if ( bOk == false )
                return -1;
        }
        pDevice->waitIdle();

        test::RHITestImage image;
        if ( image.readTransient( renderer, "SceneColor" ) == false )
            return -1;
        int64 drawnCount{ 0 };
        for ( uint32 y = 0; y < image.getHeight(); ++y )
        {
            for ( uint32 x = 0; x < image.getWidth(); ++x )
            {
                if ( test::RHITestImage::isDefaultClearBackground( image.getPixel( x, y ) ) == false )
                    ++drawnCount;
            }
        }
        return drawnCount;
    }

    /**
     * @brief 파이프라인끼리 그림을 맞출 때의 공통 입력입니다 — 기본 카메라, 주광, 색을 준 기본 머티리얼의 큐브 하나와 그림자를 받는 바닥.
     * @details 메시는 디바이스를 내리기 전에 `releaseRhi` 해야 합니다(static 메시 캐시가 죽은 디바이스를 붙잡지 않게).
     */
    struct LitCubeScene
    {
        sw::Scene                    _scene{ "LitCubeScene" };
        sw::shared_ptr<sw::Material> _material;
        sw::shared_ptr<sw::Mesh>     _mesh;
        sw::shared_ptr<sw::Mesh>     _floor;            ///< 큐브의 그림자를 받는다 — 바닥이 없으면 그림자 맵을 잘못 걸어도 그림이 같다
        sw::MeshComponent*           _pCube{ nullptr }; ///< 큐브의 메시 컴포넌트(씬 소유) — 케이스가 트랜스폼을 바꿀 때 쓴다

        /** @brief 카메라 · 주광 · 큐브 · 바닥을 채웁니다. 하나라도 못 만들면 false 입니다. */
        bool populate()
        {
            if ( _scene.ensureDefaultCameras() == false )
                return false;
            sw::GameObject* pLightObject = _scene.getObjectManager()->createGameObject( sw::hashed_string( "KeyLight" ) );
            if ( pLightObject != nullptr )
            {
                if ( sw::DirectionalLightComponent* pLight = pLightObject->addComponent<sw::DirectionalLightComponent>(); pLight != nullptr )
                    pLight->setIntensity( 2.0f );
            }
            _material = sw::Material::create();
            if ( _material->loadFromFile( "engine/materials/defaultmaterial.material" ) == false ||
                 _material->setParameter( nullptr, sw::hashed_string( "color" ), "1.0 0.5 0.25 1.0" ) == false )
                return false;
            _mesh = sw::MeshUtil::createUnitCube();
            if ( _mesh == nullptr )
                return false;
            sw::GameObject*    pObject = _scene.getObjectManager()->createGameObject( sw::hashed_string( "Cube" ) );
            sw::MeshComponent* pMesh   = pObject != nullptr ? pObject->addComponent<sw::MeshComponent>() : nullptr;
            if ( pMesh == nullptr )
                return false;
            pMesh->setMesh( _mesh );
            pMesh->setMaterial( _material.get() );
            pMesh->setLocalPosition( sw::float3{ 0.0f, 1.0f, 0.0f } );
            _pCube = pMesh;

            _floor                        = sw::MeshUtil::createPlane();
            sw::GameObject*    pFloorGo   = _scene.getObjectManager()->createGameObject( sw::hashed_string( "Floor" ) );
            sw::MeshComponent* pFloorMesh = ( pFloorGo != nullptr && _floor != nullptr ) ? pFloorGo->addComponent<sw::MeshComponent>() : nullptr;
            if ( pFloorMesh == nullptr )
                return false;
            pFloorMesh->setMesh( _floor );
            pFloorMesh->setMaterial( _material.get() );
            pFloorMesh->setLocalScale( sw::float3{ 6.0f, 1.0f, 6.0f } );
            return true;
        }
    };

    /**
     * @brief 파이프라인 하나로 몇 프레임 돌리고 **화면에 나간 그림**(Present 캡처)을 읽어 옵니다.
     * @details 첫 프레임에는 GpuScene 업로드가 아직이라 그릴 것이 없다 — 몇 장 돌린다. 백버퍼는 핸들이 없어 읽을 수 없으므로 캡처를 켠다.
     */
    bool renderPresentCaptureOf( sw::IRHIDevice* pDevice, sw::Scene& scene, const utf8* pPipelinePath, sw::vector<uint8>& outByte,
                                 sw::RHITextureMipSpan& outLayout )
    {
        sw::FrameRenderer renderer;
        if ( renderer.initialize( pDevice, pPipelinePath ) == false || renderer.isReady() == false )
            return false;
        renderer.setPresentCaptureEnabled( true );

        constexpr uint32 kWarmupFrameCount = 3;
        for ( uint32 frameIndex = 0; frameIndex < kWarmupFrameCount; ++frameIndex )
        {
            pDevice->beginFrame( sw::float4{ 0.02f, 0.02f, 0.05f, 1.0f } );
            if ( renderer.execute( pDevice, &scene ) == false )
            {
                pDevice->endFrame( false, false );
                return false;
            }
            pDevice->endFrame( false, false );
            pDevice->waitIdle();
        }
        const bool bRead = renderer.readbackPresentCapture( outByte, outLayout );
        renderer.shutdown();
        return bRead;
    }

    /** @brief 두 Present 캡처의 차이입니다(채널 단위). */
    struct CaptureDifference
    {
        uint32 _maxDelta{ 0 };
        uint32 _differCount{ 0 };
        uint32 _notBackgroundCount{ 0 }; ///< 기준 그림에서 첫 픽셀과 다른 채널 수 — 0 이면 배경뿐이라 비교가 뜻이 없다
        size_t _compareCount{ 0 };

        /** @brief 반올림 차이(1)를 넘는 칸이 없고 다른 칸이 1% 이하면 같은 그림입니다. */
        bool isSameImage() const { return _maxDelta <= 1 && _differCount * 100u <= _compareCount; }
        /** @brief 실패 메시지에 붙일 요약입니다. */
        sw::string describe() const
        {
            return sw::string( "최대 차이 " ) + sw::to_string( _maxDelta ) + ", 다른 칸 " + sw::to_string( _differCount ) + " / " +
                   sw::to_string( static_cast<uint64>( _compareCount ) );
        }
    };

    /** @brief 기준 캡처와 다른 캡처를 칸마다 맞춥니다. */
    CaptureDifference compareCaptures( const sw::vector<uint8>& listReference, const sw::vector<uint8>& listOther )
    {
        CaptureDifference difference{};
        difference._compareCount = ( listReference.size() < listOther.size() ) ? listReference.size() : listOther.size();
        for ( size_t index = 0; index < difference._compareCount; ++index )
        {
            if ( listReference[index] != listReference[index % 4] )
                ++difference._notBackgroundCount;
            const uint32 delta = ( listReference[index] > listOther[index] ) ? static_cast<uint32>( listReference[index] - listOther[index] )
                                                                             : static_cast<uint32>( listOther[index] - listReference[index] );
            if ( delta == 0 )
                continue;
            ++difference._differCount;
            if ( delta > difference._maxDelta )
                difference._maxDelta = delta;
        }
        return difference;
    }

    /**
     * @brief G버퍼 노멀 첨부에서 그려진 픽셀(알파 1 — 클리어는 알파 0)의 노멀을 풀어 평균합니다. 정규화하지 않습니다.
     * @param outDrawnCount 그려진 픽셀 수. 되읽기에 실패했거나 아무것도 그려지지 않았으면 0 이고 반환값은 영벡터입니다.
     */
    sw::float3 readMeanGBufferNormal( sw::FrameRenderer& renderer, uint32& outDrawnCount )
    {
        outDrawnCount = 0;
        test::RHITestImage image;
        if ( image.readTransient( renderer, "GBufferNormal" ) == false )
            return sw::float3{};
        sw::float3 sum{};
        for ( uint32 row = 0; row < image.getHeight(); ++row )
        {
            for ( uint32 col = 0; col < image.getWidth(); ++col )
            {
                const test::Rgba8 pixel = image.getPixel( col, row );
                if ( pixel._a < 128 )
                    continue;
                // 셰이더가 n * 0.5 + 0.5 로 적는다(swStoreSurface · gbuffer.hlsl).
                sum._x += static_cast<float32>( pixel._r ) / 255.0f * 2.0f - 1.0f;
                sum._y += static_cast<float32>( pixel._g ) / 255.0f * 2.0f - 1.0f;
                sum._z += static_cast<float32>( pixel._b ) / 255.0f * 2.0f - 1.0f;
                ++outDrawnCount;
            }
        }
        if ( outDrawnCount == 0 )
            return sw::float3{};
        return sum * ( 1.0f / static_cast<float32>( outDrawnCount ) );
    }

    /** @brief 실패 메시지에 붙일 벡터 글입니다. */
    sw::string describeVector( const sw::float3& value )
    {
        return sw::string( "(" ) + sw::to_string( value._x ) + ", " + sw::to_string( value._y ) + ", " + sw::to_string( value._z ) + ")";
    }

    /**
     * @brief 다중 뷰 시험 무대 — 붉은 큐브(−X 멀리) · 푸른 큐브(+X 멀리), 주광, 주 카메라(원점을 본다), 큐브마다 그것만 보는 캡처 카메라를 둘 수 있다.
     * @details 머티리얼은 실제 에셋을 읽어 색만 바꾼다(손으로 지은 XML 은 퍼뮤테이션이 빠져 쿠킹된 변형과 맞지 않는다).
     */
    struct MultiViewScene
    {
        static constexpr float32 kCubeDistance = 30.0f; ///< 주 카메라 화면 밖 — 캡처 카메라만 본다

        sw::Scene                    _scene{ "MultiViewScene" };
        sw::shared_ptr<sw::Material> _materialRed;
        sw::shared_ptr<sw::Material> _materialBlue;
        sw::shared_ptr<sw::Mesh>     _meshRed;
        sw::shared_ptr<sw::Mesh>     _meshBlue;
        sw::MeshComponent*           _pCubeRed{ nullptr };

        static sw::shared_ptr<sw::Material> makeMaterial( const utf8* pColor )
        {
            sw::shared_ptr<sw::Material> material = sw::Material::create();
            if ( material->loadFromFile( "engine/materials/defaultmaterial.material" ) == false ||
                 material->setParameter( nullptr, sw::hashed_string( "color" ), pColor ) == false )
                material.reset();
            return material;
        }

        bool populate()
        {
            if ( _scene.ensureDefaultCameras() == false )
                return false;
            sw::GameObject* pLightObject = _scene.getObjectManager()->createGameObject( sw::hashed_string( "KeyLight" ) );
            if ( pLightObject == nullptr || pLightObject->addComponent<sw::DirectionalLightComponent>() == nullptr )
                return false;
            _materialRed  = makeMaterial( "1.0 0.02 0.02 1.0" );
            _materialBlue = makeMaterial( "0.02 0.02 1.0 1.0" );
            _meshRed      = sw::MeshUtil::createUnitCube();
            _meshBlue     = sw::MeshUtil::createUnitCube();
            if ( _materialRed == nullptr || _materialBlue == nullptr || _meshRed == nullptr || _meshBlue == nullptr )
                return false;
            _pCubeRed = addCube( "CubeRed", _meshRed, _materialRed.get(), sw::float3{ -kCubeDistance, 0.0f, 0.0f } );
            return _pCubeRed != nullptr && addCube( "CubeBlue", _meshBlue, _materialBlue.get(), sw::float3{ kCubeDistance, 0.0f, 0.0f } ) != nullptr;
        }

        sw::MeshComponent* addCube( const utf8* pName, const sw::shared_ptr<sw::Mesh>& mesh, sw::Material* pMaterial, const sw::float3& position )
        {
            sw::GameObject*    pObject = _scene.getObjectManager()->createGameObject( sw::hashed_string( pName ) );
            sw::MeshComponent* pMesh   = pObject != nullptr ? pObject->addComponent<sw::MeshComponent>() : nullptr;
            if ( pMesh == nullptr )
                return nullptr;
            pMesh->setMesh( mesh );
            pMesh->setMaterial( pMaterial );
            pMesh->setLocalPosition( position );
            pMesh->setLocalScale( sw::float3{ 2.0f, 2.0f, 2.0f } );
            return pMesh;
        }

        /** @brief @p target 을 2.5 m 앞에서 보는 카메라를 둡니다(출력은 @p output). */
        sw::CameraComponent* addViewCamera( const utf8* pName, const sw::float3& target, const sw::CameraRenderOutput& output, sw::CameraRole role )
        {
            sw::GameObject*      pObject = _scene.getObjectManager()->createGameObject( sw::hashed_string( pName ) );
            sw::CameraComponent* pCamera = pObject != nullptr ? pObject->addComponent<sw::CameraComponent>() : nullptr;
            if ( pCamera == nullptr )
                return nullptr;
            pCamera->setRole( role );
            pCamera->setLocalPosition( target + sw::float3{ 0.0f, 1.5f, -4.0f } );
            pCamera->lookAt( target );
            pCamera->setRenderOutput( output );
            return pCamera;
        }

        static sw::CameraRenderOutput makeTextureOutput( const utf8* pPath, float32 updateRate )
        {
            sw::CameraRenderOutput output;
            output._target              = sw::CameraOutputTarget::RenderTexture;
            output._renderTexture       = pPath;
            output._renderTextureWidth  = 64;
            output._renderTextureHeight = 48;
            output._updateRate          = updateRate;
            return output;
        }

        /** @brief 렌더 텍스처를 되읽어 그려진(모서리와 다른) 픽셀의 평균 (R − B)와 그 수를 냅니다. 실패면 false 입니다. */
        static bool readTextureRedMinusBlue( sw::IRHIDevice* pDevice, const utf8* pPath, int64& outMeanRedMinusBlue, uint32& outDrawnCount,
                                             uint32& outWidth )
        {
            outMeanRedMinusBlue           = 0;
            outDrawnCount                 = 0;
            const sw::Texture2D* pTexture = sw::engine::getAssetManager().getTextureManager().find( pPath );
            if ( pTexture == nullptr || pTexture->isRhiValid() == false )
                return false;
            sw::vector<uint8>     bytes;
            sw::RHITextureMipSpan layout{};
            if ( pDevice->getResourceFactory()->readbackTexture2D( pTexture->getHandle(), 0, 0, bytes, layout ) == false )
                return false;
            test::RHITestImage image;
            image.assign( std::move( bytes ), layout, pTexture->getFormat() );
            outWidth                 = image.getWidth();
            const test::Rgba8 corner = image.getPixel( 0, 0 );
            int64             sum    = 0;
            for ( uint32 y = 0; y < image.getHeight(); ++y )
            {
                for ( uint32 x = 0; x < image.getWidth(); ++x )
                {
                    const test::Rgba8 pixel = image.getPixel( x, y );
                    if ( test::RHITestImage::getColorDistance( pixel, corner ) < 24 )
                        continue;
                    sum += static_cast<int64>( pixel._r ) - static_cast<int64>( pixel._b );
                    ++outDrawnCount;
                }
            }
            if ( outDrawnCount > 0 )
                outMeanRedMinusBlue = sum / static_cast<int64>( outDrawnCount );
            return true;
        }
    };

    /**
     * @brief 툰 · 외곽선 시험 무대 — 기본 카메라(원점을 본다), 옆에서 비스듬히 드는 주광, 원점의 구 하나.
     * @details 머티리얼은 실제 에셋(defaultmaterial · toon)을 읽어 값만 바꾼다(손으로 지은 XML 은 퍼뮤테이션이 빠져 쿠킹된 변형과 맞지 않는다).
     */
    struct ToonSphereScene
    {
        static constexpr float32 kSphereScale = 1.6f;

        sw::Scene                    _scene{ "ToonSphereScene" };
        sw::shared_ptr<sw::Material> _material;
        sw::shared_ptr<sw::Mesh>     _mesh;
        sw::MeshComponent*           _pSphere{ nullptr };

        /** @brief 기본 머티리얼(forwardlit — 램버트)을 주황색으로 둔 것입니다. */
        static sw::shared_ptr<sw::Material> makeLitMaterial()
        {
            sw::shared_ptr<sw::Material> material = sw::Material::create();
            if ( material->loadFromFile( "engine/materials/defaultmaterial.material" ) == false ||
                 material->setParameter( nullptr, sw::hashed_string( "color" ), "0.9 0.5 0.25 1.0" ) == false )
                material.reset();
            return material;
        }

        /**
         * @brief toon.material 을 같은 주황색 · 어두운 그림자색으로, 계단을 칼같이(toony 1) 둔 것입니다. 림 · 맷캡 · 발광은 없습니다.
         * @param bOutline 켜면 검은 외곽선(화면 높이의 1 %, 빛 섞기 0)을 그립니다.
         */
        static sw::shared_ptr<sw::Material> makeToonMaterial( bool bOutline )
        {
            sw::shared_ptr<sw::Material> material = sw::Material::create();
            const bool                   bLoaded  = material->loadFromFile( "engine/materials/toon.material" );
            const bool                   bSet     = bLoaded && material->setParameter( nullptr, sw::hashed_string( "baseColor" ), "0.9 0.5 0.25 1.0" ) &&
                              material->setParameter( nullptr, sw::hashed_string( "shadeColor" ), "0.35 0.2 0.3 1.0" ) &&
                              material->setParameter( nullptr, sw::hashed_string( "shadingToony" ), "1.0" ) &&
                              material->setParameter( nullptr, sw::hashed_string( "outlineColor" ), "0.0 0.0 0.0 1.0" ) &&
                              material->setParameter( nullptr, sw::hashed_string( "outlineWidth" ), "0.01" ) &&
                              material->setParameter( nullptr, sw::hashed_string( "outlineWidthMode" ), "1.0" ) &&
                              material->setParameter( nullptr, sw::hashed_string( "outlineLightingMix" ), "0.0" );
            if ( bSet == false )
                return nullptr;
            material->setStaticSwitch( sw::hashed_string( "Outline" ), bOutline );
            return material;
        }

        /** @brief 카메라 · 주광 · 구를 채웁니다. 하나라도 못 만들면 false 입니다. */
        bool populate( const sw::shared_ptr<sw::Material>& material )
        {
            _material = material;
            if ( _material == nullptr || _scene.ensureDefaultCameras() == false )
                return false;
            sw::GameObject*                pLightObject = _scene.getObjectManager()->createGameObject( sw::hashed_string( "KeyLight" ) );
            sw::DirectionalLightComponent* pLight       = pLightObject != nullptr ? pLightObject->addComponent<sw::DirectionalLightComponent>() : nullptr;
            if ( pLight == nullptr )
                return false;
            // 옆에서 들어 화면의 구 절반쯤이 그늘이다 — 계단 경계가 구 한가운데를 지난다.
            pLight->setLocalRotation( sw::float3{ 0.0f, 1.3f, 0.0f } );
            pLight->setIntensity( 1.5f );
            pLight->setCastShadow( false );

            // 정점 색은 흰색이다 — 생성기의 검증 색(무지개)이 그대로 남으면 계단 위에 색 그라데이션이 얹혀 단계 수를 셀 수 없다.
            _mesh = sw::MeshUtil::createPrimitive( "sphere", sw::PrimitiveVertexColor::White );
            if ( _mesh == nullptr )
                return false;
            sw::GameObject* pObject = _scene.getObjectManager()->createGameObject( sw::hashed_string( "Sphere" ) );
            _pSphere                = pObject != nullptr ? pObject->addComponent<sw::MeshComponent>() : nullptr;
            if ( _pSphere == nullptr )
                return false;
            _pSphere->setMesh( _mesh );
            _pSphere->setMaterial( _material.get() );
            _pSphere->setLocalScale( sw::float3{ kSphereScale, kSphereScale, kSphereScale } );
            return true;
        }
    };

    /**
     * @brief 그려진 픽셀 마스크 — 모서리 픽셀(배경)과 색 거리가 문턱을 넘는 픽셀입니다. 클리어 색 · 톤매핑과 무관하게 배경을 걷어 냅니다.
     */
    struct DrawnMask
    {
        static constexpr uint32 kBackgroundDistance = 24;

        sw::vector<uint8> _listDrawn;
        uint32            _width{ 0 };
        uint32            _height{ 0 };
        uint32            _drawnCount{ 0 };
        sw::float2        _centroid{};

        explicit DrawnMask( const test::RHITestImage& image )
            : _listDrawn{}
            , _width{ image.getWidth() }
            , _height{ image.getHeight() }
        {
            // 크기 · 값 생성자는 중괄호로 부르면 원소 둘짜리 목록이 된다 — 본문에서 늘린다.
            _listDrawn.resize( image.getPixelCount(), SW_FALSE );
            const test::Rgba8 corner = image.getPixel( 0, 0 );
            float64           sumX{ 0.0 };
            float64           sumY{ 0.0 };
            for ( uint32 y = 0; y < _height; ++y )
            {
                for ( uint32 x = 0; x < _width; ++x )
                {
                    if ( test::RHITestImage::getColorDistance( image.getPixel( x, y ), corner ) <= kBackgroundDistance )
                        continue;
                    _listDrawn[static_cast<size_t>( y ) * _width + x] = SW_TRUE;
                    ++_drawnCount;
                    sumX += x;
                    sumY += y;
                }
            }
            if ( _drawnCount > 0 )
                _centroid = sw::float2{ static_cast<float32>( sumX / _drawnCount ), static_cast<float32>( sumY / _drawnCount ) };
        }

        bool isDrawn( uint32 x, uint32 y ) const { return _listDrawn[static_cast<size_t>( y ) * _width + x] == SW_TRUE; }
    };

    /** @brief 픽셀의 밝기(R · G · B 평균, 0~255)입니다. */
    uint32 computeLuma( const test::Rgba8& pixel )
    {
        return ( static_cast<uint32>( pixel._r ) + pixel._g + pixel._b ) / 3u;
    }

    /**
     * @brief 그려진 픽셀의 밝기 단계 수 — 밝기 64 칸 히스토그램에서 그려진 픽셀의 1 % 이상이 든 칸의 수입니다.
     * @details 셀 셰이딩은 빛 · 그늘 두 단계(+ 경계의 몇 픽셀)라 몇 칸에 몰리고, 램버트는 구 표면을 따라 고르게 퍼져 여러 칸을 채운다.
     *          특정 색의 픽셀 수가 아니라 분포의 모양을 보므로 클리어 색 · 톤매핑 · 백엔드 반올림에 흔들리지 않는다.
     */
    uint32 countBrightnessLevels( const test::RHITestImage& image, const DrawnMask& mask )
    {
        constexpr uint32 kBinCount = 64;
        uint32           arrBin[kBinCount]{};
        for ( uint32 y = 0; y < mask._height; ++y )
        {
            for ( uint32 x = 0; x < mask._width; ++x )
            {
                if ( mask.isDrawn( x, y ) )
                    ++arrBin[computeLuma( image.getPixel( x, y ) ) * kBinCount / 256u];
            }
        }
        uint32 levelCount{ 0 };
        for ( const uint32 binCount : arrBin )
        {
            if ( binCount * 100u >= mask._drawnCount && binCount > 0 )
                ++levelCount;
        }
        return levelCount;
    }

    /** @brief 씬을 몇 프레임 그리고(첫 프레임은 업로드 전이다) 첨부 하나를 읽습니다. 실패면 false 입니다. */
    bool renderAndReadAttachment( sw::FrameRenderer& renderer, sw::IRHIDevice* pDevice, sw::Scene& scene, const utf8* pAttachment, test::RHITestImage& outImage )
    {
        constexpr uint32 kFrameCount = 4;
        for ( uint32 frame = 0; frame < kFrameCount; ++frame )
        {
            scene.getObjectManager()->getAnimationSystem().evaluate( 0.0f );
            if ( renderSceneFrame( renderer, pDevice, scene, sw::float4{ 0.02f, 0.02f, 0.05f, 1.0f } ) == false )
                return false;
        }
        return outImage.readTransient( renderer, pAttachment );
    }

    /**
     * @brief 외곽선을 켠 그림과 끈 그림의 차이 — 실루엣 둘레의 고리입니다.
     * @details 고리 = 끈 그림에서 배경이고 켠 그림에서 그려진 픽셀. 구의 화면 반지름은 끈 그림의 면적에서 구한다(√(면적/π)).
     */
    struct OutlineRing
    {
        uint32  _sphereCount{ 0 };    ///< 끈 그림의 구 픽셀 수
        uint32  _ringCount{ 0 };      ///< 고리 픽셀 수
        uint32  _inBandCount{ 0 };    ///< 고리 중 실루엣 띠(반지름 R - 2 ~ R + 두께 + 3) 안의 수
        uint32  _interiorDiffer{ 0 }; ///< 두 그림 모두 구인데 색이 30 넘게 다른 픽셀 수 — 외곽선이 구 안쪽을 덮으면 크다
        float64 _ringMeanLuma{ 0.0 }; ///< 고리의 평균 밝기
        float32 _radius{ 0.0f };      ///< 구의 화면 반지름(픽셀)

        static OutlineRing measure( const test::RHITestImage& imageOff, const test::RHITestImage& imageOn, float32 ringWidthPixel )
        {
            OutlineRing     ring{};
            const DrawnMask maskOff( imageOff );
            const DrawnMask maskOn( imageOn );
            ring._sphereCount = maskOff._drawnCount;
            ring._radius      = sw::MathUtil::sqrt( static_cast<float32>( maskOff._drawnCount ) / sw::MathUtil::kPi );
            if ( maskOff._width != maskOn._width || maskOff._height != maskOn._height )
                return ring;
            uint64 lumaSum{ 0 };
            for ( uint32 y = 0; y < maskOff._height; ++y )
            {
                for ( uint32 x = 0; x < maskOff._width; ++x )
                {
                    const bool bOff = maskOff.isDrawn( x, y );
                    const bool bOn  = maskOn.isDrawn( x, y );
                    if ( bOff && bOn )
                    {
                        if ( test::RHITestImage::getColorDistance( imageOff.getPixel( x, y ), imageOn.getPixel( x, y ) ) > 30 )
                            ++ring._interiorDiffer;
                        continue;
                    }
                    if ( bOff || bOn == false )
                        continue;
                    ++ring._ringCount;
                    lumaSum += computeLuma( imageOn.getPixel( x, y ) );
                    const float32 dx     = static_cast<float32>( x ) - maskOff._centroid._x;
                    const float32 dy     = static_cast<float32>( y ) - maskOff._centroid._y;
                    const float32 radius = sw::MathUtil::sqrt( dx * dx + dy * dy );
                    if ( ring._radius - 2.0f <= radius && radius <= ring._radius + ringWidthPixel + 3.0f )
                        ++ring._inBandCount;
                }
            }
            if ( ring._ringCount > 0 )
                ring._ringMeanLuma = static_cast<float64>( lumaSum ) / static_cast<float64>( ring._ringCount );
            return ring;
        }

        sw::string describe() const
        {
            return sw::string( "구 " ) + sw::to_string( _sphereCount ) + " px · 반지름 " + sw::to_string( _radius ) + " · 고리 " + sw::to_string( _ringCount ) +
                   " px(띠 안 " + sw::to_string( _inBandCount ) + ", 평균 밝기 " + sw::to_string( _ringMeanLuma ) + ") · 안쪽 차이 " + sw::to_string( _interiorDiffer );
        }
    };

    /**
     * @brief 그림자 패스가 바닥에 그림자를 드리우는지 — 그림자 맵을 비운 판(그림자 패스 깊이 쓰기를 끈 판)과 그림이 **달라야** 한다.
     * @param shadowExtent 0 이면 주광의 기본 볼륨, 아니면 그 반경(m)의 볼륨(빛 거리는 그 두 배)
     */
    void expectShadowCastsOnEveryBackend( const utf8* pTestName, float32 shadowExtent )
    {
        sw::string pipelineText;
        SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "engine/pipeline/forwardpipeline.xml", pipelineText ) );
        constexpr sw::string_view kDepthWriteOn  = "_bEnableDepthWrite=\"true\"";
        constexpr sw::string_view kDepthWriteOff = "_bEnableDepthWrite=\"false\"";
        const size_t              shadowPassAt   = pipelineText.find( "_type=\"Shadow\"" );
        SW_ASSERT_TRUE( shadowPassAt != sw::string::npos );
        const size_t depthWriteAt = pipelineText.find( kDepthWriteOn.data(), shadowPassAt );
        SW_ASSERT_TRUE( depthWriteAt != sw::string::npos );
        pipelineText.replace( depthWriteAt, kDepthWriteOn.size(), kDepthWriteOff.data() );
        const sw::string emptyShadowPath = test::makeTempPath( "emptyshadowpipeline.xml" );
        SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( emptyShadowPath, pipelineText ) );

        uint32                comparedCount{ 0 };
        test::RHIBackendSweep sweep;
        for ( test::RHITestDevice& device : sweep )
        {
            LitCubeScene          cube;
            sw::vector<uint8>     listShadow;
            sw::vector<uint8>     listEmptyShadow;
            sw::RHITextureMipSpan layoutShadow{};
            sw::RHITextureMipSpan layoutEmptyShadow{};
            const utf8*           pName = device->getBackendName();
            bool                  bOk   = cube.populate();
            if ( bOk && shadowExtent > 0.0f )
            {
                sw::DirectionalLightComponent* pLight = cube._scene.findActiveDirectionalLight();
                bOk                                   = pLight != nullptr;
                if ( bOk )
                {
                    pLight->setShadowExtent( shadowExtent );
                    pLight->setShadowDistance( shadowExtent * 2.0f );
                }
            }
            if ( bOk )
                bOk = renderPresentCaptureOf( device.get(), cube._scene, "engine/pipeline/forwardpipeline.xml", listShadow, layoutShadow );
            if ( bOk )
                bOk = renderPresentCaptureOf( device.get(), cube._scene, emptyShadowPath.c_str(), listEmptyShadow, layoutEmptyShadow );

            if ( bOk && layoutShadow._width == layoutEmptyShadow._width )
            {
                ++comparedCount;
                const CaptureDifference difference = compareCaptures( listShadow, listEmptyShadow );
                SW_EXPECT_TRUE_MSG( difference._notBackgroundCount > 0, ( sw::string( pName ) + ": 기준 그림이 배경뿐입니다" ).c_str() );
                SW_EXPECT_FALSE_MSG( difference.isSameImage(),
                                     ( sw::string( pName ) + ": 그림자 맵을 비워도 그림이 같다 — 그림자가 하나도 지지 않는다 (" + difference.describe() + ")" ).c_str() );
            }
            else if ( bOk == false )
                SW_LOG_WARNING( "%#: %# 에서 파이프라인을 돌리지 못했습니다.", pTestName, pName );
        }

        if ( comparedCount == 0 )
            SW_TEST_SKIP( "No RHI backend could run the shadow pipelines" );
    }
} // namespace

/**
 * @brief FrameRenderer 파이프라인 로드 + 씬 execute 스모크 (RenderThread와 동일 begin/execute/end)
 */
SW_TEST_CASE( RenderPassGpuTest, FrameRendererInitializeAndExecuteSmoke )
{
    test::RHITestDevice device( { sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL, sw::RHIBackend::DirectX12 } );
    if ( device.isReady() == false )
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
}

/**
 * @brief [RenderPassGpuTest] 셰이더 핫리로드가 PSO 를 **실제로 다시 만드는지** 검증.
 * @details PSO 는 바이트코드를 박아 넣은 객체다. onShaderRecompiled 가 바인딩 레이아웃만
 *          새로 만들면 셰이더를 고쳐도 화면이 시작 시 컴파일된 그대로다 —
 *          로그는 "Recompilation Succeeded" 를 찍는데 그림은 안 바뀌니 눈치채기 어렵다.
 *          LiveShaderTest 는 등록과 리로드 큐만 보므로 이 경로를 잡지 못한다.
 *
 *          네 백엔드 모두 PSO 를 RHIHandleTable(generation 팩드)로 발급하므로, 다시 만들면
 *          핸들 값이 반드시 달라진다. 재생성 여부를 핸들로 판정하는 근거다.
 */
SW_TEST_CASE( RenderPassGpuTest, ShaderRecompileRebuildsPipelineStates )
{
    test::RHITestDevice device( { sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL, sw::RHIBackend::DirectX12 } );
    if ( device.isReady() == false )
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
}

/**
 * @brief [RenderPassGpuTest] 셰이더 핫 리로드(패스 자원만 다시 세움)가 TAA 히스토리를 잃지 않는지.
 * @details TAA 히스토리는 트랜지언트 크기를 따르는 텍스처라 `ensureTransientResources` 가 만들고, 그 함수는 크기가 그대로면
 *          아무것도 하지 않는다. 패스 자원 해제(`releasePassResources`)가 히스토리까지 놓으면, 리로드 뒤 창 크기가 바뀔 때까지
 *          히스토리가 0 이라 TAA 패스가 지난 프레임을 읽지도 쓰지도 않는다.
 */
SW_TEST_CASE( RenderPassGpuTest, ShaderRecompileKeepsTaaHistory )
{
    test::RHITestDevice device( { sw::RHIBackend::DirectX12, sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL } );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "No RHI backend for TAA history test" );

    sw::FrameRenderer renderer;
    SW_ASSERT_TRUE( renderer.initialize( device.get(), "engine/pipeline/deferredpipeline.xml" ) );
    sw::Scene scene( "TaaHistoryScene" );
    SW_ASSERT_TRUE( scene.ensureDefaultCameras() );

    const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
    device->beginFrame( clear );
    SW_EXPECT_TRUE( renderer.execute( device.get(), &scene ) );
    device->endFrame( false, false );
    SW_ASSERT_TRUE_MSG( renderer.getTaaHistory() != 0, "디퍼드 파이프라인의 첫 프레임 뒤 TAA 히스토리가 없다" );

    sw::ShaderCompileResult result{};
    result._bSuccess = true;
    renderer.onShaderRecompiled( "engine/shaders/forwardlit.hlsl", result );

    device->beginFrame( clear );
    SW_EXPECT_TRUE( renderer.execute( device.get(), &scene ) );
    device->endFrame( false, false );
    device->waitIdle();
    SW_EXPECT_TRUE_MSG( renderer.getTaaHistory() != 0, "셰이더 리로드 뒤 TAA 히스토리가 사라졌다 — 패스 자원 해제가 트랜지언트 자원을 놓았다" );
}

/**
 * @brief executePacket()이 프레임마다 GpuScene GPU 버퍼를 재생성하지 않고 재사용하는지 검증.
 * @details GT/RT 소유권 분리(exportCpuSnapshot/adoptCpuSnapshot) 회귀 테스트 — FrameRenderer::_gpuScene 을
 *          매 프레임 통째로 덮어쓰면 인스턴스 버퍼 핸들이 매번 바뀌고, 직전 프레임 버퍼/디스크립터는
 *          releaseGpu() 없이 버려져 샌다.
 */
SW_TEST_CASE( RenderPassGpuTest, GpuSceneBufferReusedAcrossPackets )
{
    test::RHITestDevice device( { sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL, sw::RHIBackend::DirectX12 } );
    if ( device.isReady() == false )
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
}

/**
 * @brief [RenderPassGpuTest] 패킷에 실린 머티리얼·인스턴스는 GT 가 소유를 놓아도 RT 가 그 패킷을 다 쓸 때까지 산다.
 * @details 렌더 스레드는 씬을 못 보고 스냅샷만 받는다. 스냅샷이 생포인터만 들고 있으면 GT 가 오브젝트를
 *          지우거나 인스턴스를 바꾼 직후 ≤ 패킷 링 깊이 프레임 동안 RT 가 해제된 메모리를 읽는다
 *          (`applyInstanceCbsVal` 의 updateRhi, `uploadMaterialGroups` 의 getBuffer). 여기서는 패킷을
 *          내보낸 **뒤에** GT 쪽 소유를 전부 놓고 그 패킷을 실행한다 — ASAN 빌드에서 use-after-free 로
 *          잡히는 순서다. 스냅샷이 소유를 함께 실어야만 통과한다.
 */
SW_TEST_CASE( RenderPassGpuTest, MaterialLifetimeFollowsPacket )
{
    test::RHITestDevice device( { sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL, sw::RHIBackend::DirectX12 } );
    if ( device.isReady() == false )
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
    SW_ASSERT_FALSE( lateePacket._gpuScene.getInstances().empty() );

    mesh->setMaterialInstance( nullptr );
    mesh->setMaterial( nullptr );
    gtGpuScene.clear(); // GT 쪽 빌드 캐시도 놓는다 — 살아 있는 참조는 패킷 안의 것뿐이어야 한다
    instance.reset();
    material.reset();

    device->beginFrame( clear );
    SW_EXPECT_TRUE( renderer.executePacket( device.get(), lateePacket ) );
    device->endFrame( false, false );
}

/**
 * @brief 실제 RHI 디바이스로 RenderGraph::executeParallel을 레벨 단위로 끝까지 실행해 본다.
 * @details 독립 브랜치(DepthPass/ShadowPass) + 합류 패스(ForwardPass) 구조로 레벨 경계를 넘나드는
 *          제출 순서(레벨마다 먼저 제출 후 다음 레벨)까지 실제로 동작하는지 확인한다.
 * @note **병렬 기록 여부는 디바이스에 묻는다 — 백엔드 이름으로 고르지 않는다.**
 *       `D3D11RHIDevice::getCapabilities` 가 드라이버 조회 결과(`D3D11_FEATURE_THREADING`)로 이 항목을
 *       런타임에 덮으므로 DX11 도 병렬로 기록한다. DX12 만 하드코딩하면 DX11 의 병렬 경로(즉시 컨텍스트 동시
 *       Map, 리스트끼리 기록 상태 캐시 공유 같은 레이스가 생기는 곳)에 테스트가 닿지 않고, 그 레이스는
 *       `AmbientOcclusionReachesBloom` 이 플래키해지는 것으로만 드러난다.
 */
SW_TEST_CASE( RenderPassGpuTest, RenderGraphExecuteParallelRunsOnRealDevice )
{
    uint32                attemptedCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        if ( device->getCapabilities()._bParallelCommandRecording == SW_FALSE )
            continue;
        ++attemptedCount;
        const sw::string label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( device.getBackend() ) );

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
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No backend reports parallel command recording" );
}

/**
 * @brief Deferred 파이프라인을 실제 디바이스에서 돌린다 — 같은 레벨의 패스가 병렬로 기록되는 유일한 구성.
 * @details forwardpipeline 은 Shadow→ForwardOpaque→…→Present 완전 체인이라 레벨이 전부 1개다.
 *          즉 병렬 기록 경로가 있어도 실제로 동시에 도는 패스가 없어, 패스 콜백이 만지는 렌더러
 *          공유 상태(TransientAttachmentPool 의 _listClearedThisFrame, 프레임 래치 플래그)의 레이스가 드러나지
 *          않는다. deferredpipeline 은 레벨 0 = {Shadow, GBuffer}, 레벨 1 = {Shading, SSAO} 가
 *          동시에 기록된다. 메시가 있어야 드로우 경로까지 들어가므로 큐브를 넣고 여러 프레임 돌린다.
 *
 *          파이프라인 XML 이 선언한 포맷과 PSO/보조 텍스처가 어긋나면(Shading 별칭 미해석, 풀스크린 PSO 의
 *          뎁스 포맷, TAA 히스토리 포맷 하드코딩) DX12 가 몇 프레임 만에 fence wait timeout → DEVICE_HUNG →
 *          크래시로 간다. 검증 레이어 오류가 0 인지도 같이 봐야 의미가 있다.
 */
SW_TEST_CASE( RenderPassGpuTest, FrameRendererDeferredPipelineParallelLevels )
{
    uint32                attemptedCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        // **병렬로 기록하는 백엔드는 전부 돈다 — 하나 찾고 멈추지 않는다.** 첫 성공에서 멈추면
        // 레이스를 잡으려고 만든 테스트가 정작 레이스가 있는 백엔드(DX11 등)를 건너뛴다.
        if ( device->getCapabilities()._bParallelCommandRecording == SW_FALSE )
            continue;
        ++attemptedCount;
        const sw::string label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( device.getBackend() ) );

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
        // 부족하다: 기록 레이스는 예외도 실패 코드도 내지 않고 **픽셀만** 바꾼다. "돌았다" 만 보면
        // DX11 의 기록 레이스를 그대로 통과시킨다.
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

        // **병렬 판은 세 번 재서 각각 기준과 대조한다.** 레이스는 **프로세스마다 굳는** 경향이 있어
        // (한 번 어긋나면 그 프로세스의 판이 모두 어긋난다) 반복은 굳지 않는 경우를 위한 보험이다.
        // **코드가 옳으면 언제나 통과한다**(직렬 == 병렬은 결정적이다).
        constexpr uint32 kParallelRunCount = 3;
        for ( uint32 runIndex = 0; runIndex < kParallelRunCount; ++runIndex )
        {
            const float64 parallelMean = renderAndMeasureLit( &taskManager );
            // 허용 오차는 1% 다. 기록 레이스는 LitColor 를 수십 % 흔들므로 한참 아래고,
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
    }

    if ( attemptedCount == 0 )
        SW_TEST_SKIP( "No backend reports parallel command recording" );
}

/**
 * @brief [RenderPassGpuTest] 머티리얼의 퍼뮤테이션이 실제로 그 배치의 PSO 가 되는지 (4 백엔드).
 * @details 머티리얼은 자기 셰이더 변형을 선언한다(유리는 MATERIAL_BLEND_TRANSLUCENT 를 always-define 으로
 *          들고 있다). 그런데 드로우가 **패스 PSO 하나로** 전부 그리면 그 선언은 쿠킹되기만 하고 한 번도
 *          걸리지 않는다. 반투명 패스 PSO 에 그 define 을 직접 박으면 이것이 가려진다 —
 *          "반투명 패스에 들어온 것은 무조건 반투명" 이 되어 머티리얼이 뭘 선언했는지는 상관이 없어진다.
 *
 *          픽셀로는 잡기 어렵다. 알파 경로가 컴파일됐는지 여부는 겹치는 곳의 색만 바꾸는데, 그 색은
 *          조명·톤매핑을 타고 흔들린다. 그래서 **드로우가 실제로 고른 PSO 의 디스크립터**를 본다.
 */
SW_TEST_CASE( RenderPassGpuTest, MaterialPermutationDrivesBatchPso )
{
    auto hasDefine = []( const sw::RHIPipelineStateDesc& desc, const utf8* pDefine ) -> bool
    {
        for ( const sw::string& defineStr : desc._listShaderDefine )
        {
            if ( defineStr == pDefine )
                return true;
        }
        return false;
    };

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
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
            bOk = renderSceneFrame( renderer, device.get(), scene, clear );
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
                // 패스 PSO 자체에는 그 define 이 없다 — 반투명 패스가 정하는 것은 블렌드·뎁스지 셰이더가 아니다.
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
                        // 여기에 PS 가 남으면 (shadowdepth · PSMain · 머티리얼 define) 조합을 쿠커는 쿠킹하지 않으므로
                        // Shipping 이 매니페스트 미스([Error])를 낸다.
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
    }

    // 형제 시험(카메라 컬링·투명 정렬·뷰 모드 등)과 같은 규칙으로 빠진다 — 여기서 단언하면 디스플레이가 없는
    // 환경에서 진다. X11 디스플레이가 없으면 창이 안 열려 네 백엔드가 전부 초기화에 실패하고,
    // 그건 결함이 아니라 그 환경에 GPU 가 없다는 뜻이다.
    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend available for material permutation PSO test" );
}

/**
 * @brief [RenderPassGpuTest] 스프라이트는 스프라이트 셰이더(`sprite2d.hlsl`)의 반투명 배치로 그려진다 (4 백엔드)
 * @details 스프라이트가 씬 기본 머티리얼의 단위 큐브로 그려지면 `sprite2d.material` 과 그 셰이더는 쿠킹되기만 하고 한 번도 걸리지 않는다.
 *          스프라이트가 사각형 + 스프라이트 머티리얼 + 텍스처 인스턴스로 풀리면, 엔진 루프가 패킷 전에 그 머티리얼을 올리고(`initializePending`)
 *          배치 PSO 가 스프라이트 셰이더가 된다. Shipping 에서는 그 퍼뮤테이션이 쿠킹돼 있어야 한다(쿠킹 구멍이면 여기서 진다).
 */
SW_TEST_CASE( RenderPassGpuTest, SpriteDrawsWithTheSpriteShader )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::FrameRenderer renderer;
        bool              bOk   = renderer.initialize( device.get() ) && renderer.isReady();
        const sw::string  label = sw::string( device->getBackendName() );
        {
            sw::Scene scene( "SpriteScene" );
            if ( bOk )
                bOk = scene.ensureDefaultCameras();
            sw::SpriteComponent* pSprite = nullptr;
            if ( bOk )
            {
                sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( "Torch" ) );
                pSprite              = ( pObj != nullptr ) ? pObj->addComponent<sw::SpriteComponent>() : nullptr;
                bOk                  = pSprite != nullptr;
            }
            if ( bOk )
            {
                pSprite->setTextureName( "engine/textures/perlin.dds" );
                pSprite->resolveRenderAssets();
                // 엔진 루프가 패킷을 내기 전에 하는 일 — 컴포넌트가 디바이스 없이 잡은 머티리얼을 올린다.
                sw::engine::getAssetManager().getMaterialManager().initializePending( device.get() );
                bOk = pSprite->getMaterial() != nullptr && pSprite->getMaterial()->isRhiValid();
                SW_EXPECT_TRUE_MSG( bOk, ( label + ": 스프라이트 머티리얼이 올라가지 않았다" ).c_str() );
            }
            if ( bOk )
            {
                const sw::float4 clear{ 0.0f, 0.0f, 0.0f, 1.0f };
                bOk = renderSceneFrame( renderer, device.get(), scene, clear );
                SW_EXPECT_TRUE_MSG( bOk, ( label + ": 프레임 실행 실패" ).c_str() );
            }
            if ( bOk )
            {
                const sw::vector<sw::GpuMeshBatch>& batches = renderer.getGpuScene().getTransparentBatches();
                SW_EXPECT_TRUE_MSG( batches.empty() == false, ( label + ": 스프라이트(반투명 머티리얼)인데 반투명 배치가 없다" ).c_str() );
                const sw::RHIPipelineStateHandle passPso = renderer.getEnginePso( sw::RenderPassType::Transparent );
                if ( batches.empty() == false && passPso != 0 )
                {
                    sw::RHIPipelineStateDesc batchDesc{};
                    SW_EXPECT_TRUE_MSG( renderer.findPsoDesc( renderer.psoForBatch( passPso, batches[0] ), batchDesc ),
                                        ( label + ": 스프라이트 배치 PSO 의 디스크립터를 찾을 수 없다" ).c_str() );
                    SW_EXPECT_TRUE_MSG( batchDesc._pixelShaderPath.find( "sprite2d" ) != sw::string::npos,
                                        ( label + ": 스프라이트 배치가 스프라이트 셰이더가 아니다 — " + batchDesc._pixelShaderPath ).c_str() );
                }
            }
        }
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend available for the sprite shader test" );
}

/**
 * @brief [RenderPassGpuTest] 메인 패스가 **카메라 절두체**로 컬링되는지 — 라이트 절두체가 아니라 (4 백엔드).
 * @details 컬링은 뷰마다 돈다(메인 카메라 / 그림자 라이트). 그런데 상수버퍼를 **하나만** 두고 두 뷰가
 *          나눠 쓰면, 두 번째 업로드가 첫 번째 디스패치가 읽을 내용을 덮어쓴다 — CPU 는 디스패치 사이에
 *          쓰지만 GPU 는 제출 뒤에 읽기 때문이다. 그러면 메인 뷰가 **그림자 라이트의 좁은
 *          직교 절두체**로 걸러져 화면에서 격자의 절반이 사라진다.
 *
 *          그래서 큐브를 **라이트 상자 밖, 카메라 시야 안**에 둔다. 라이트는 원점 근처 2.2 폭 상자만
 *          비추므로 x = ±2.5 는 확실히 밖이고, 카메라는 z 를 뒤로 물리면 그만큼 넓게 본다.
 *          뷰를 잘못 쓰면 이 큐브들이 통째로 사라진다.
 */
SW_TEST_CASE( RenderPassGpuTest, MainPassCullsWithCameraFrustumNotLight )
{
    // 라이트 직교 상자는 원점 중심 폭 2.22 — 반폭 1.11 에 바운드 반지름 0.87 을 더해도 2.5 는 밖이다.
    constexpr float32 kSideX = 2.5f;
    // 카메라(0, 1.2, 3.2)에서 뒤로 물려 시야 폭을 넓힌다 — 그래야 ±2.5 가 화면 안에 들어온다.
    constexpr float32 kDepthZ = -6.0f;

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
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
            bOk = renderSceneFrame( renderer, device.get(), scene, clear );
        }

        test::RHITestImage image;
        if ( bOk && image.readTransient( renderer, "SceneColor" ) )
        {
            const test::Rgba8 corner = image.getPixel( 0, 0 );

            uint32 arrDrawn[2]{};
            for ( uint32 y = 0; y < image.getHeight(); ++y )
            {
                for ( uint32 x = 0; x < image.getWidth(); ++x )
                {
                    const test::Rgba8 pixel = image.getPixel( x, y );
                    if ( test::RHITestImage::getColorDistance( pixel, corner ) < 24 )
                        continue;
                    ++arrDrawn[( x < image.getWidth() / 2 ) ? 0u : 1u];
                }
            }

            const sw::string label    = sw::string( device->getBackendName() );
            const uint32     minDrawn = image.getPixelCount() / 3000;
            SW_EXPECT_TRUE_MSG( arrDrawn[0] > minDrawn && arrDrawn[1] > minDrawn,
                                ( label + ": 라이트 상자 밖의 큐브가 사라졌다 (좌 " + sw::to_string( arrDrawn[0] ) + ", 우 " +
                                  sw::to_string( arrDrawn[1] ) + ", 최소 " + sw::to_string( minDrawn ) +
                                  ") — 메인 패스가 카메라가 아니라 라이트 절두체로 걸러지고 있다" )
                                    .c_str() );
        }
        SW_EXPECT_TRUE_MSG( bOk, device->getBackendName() );
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend available for camera frustum cull test" );
}

/**
 * @brief [RenderPassGpuTest] 한 배치 안의 투명 인스턴스가 백엔드마다 같은 순서로 섞이는지 (4 백엔드).
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
    bool    bHasReference{ false };
    float32 referenceMean[3]{};
    uint32  referenceDrawn{ 0 };

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "TransparentOrderScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        // 메시와 머티리얼 인스턴스를 **공유**한다 — 그래야 한 배치에 투명 인스턴스가 여럿 들어가고,
        // 배치 안의 정렬이 실제로 검사된다. 따로 주면 배치가 하나씩 갈려 검사할 순서가 없다.
        // 로드하지 않은 씬은 기본 머티리얼이 없다(getMaterial 이 null) — 반투명 에셋을 직접 읽는다.
        sw::shared_ptr<sw::Mesh>     sharedMesh;
        sw::shared_ptr<sw::Material> glassMaterial;
        if ( bOk )
        {
            sharedMesh    = sw::MeshUtil::createUnitCube();
            glassMaterial = sw::Material::create();
            // 반투명 전용 에셋 — blendMode 와 퍼뮤테이션이 불투명과 다르다. 불투명 에셋에
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
            bOk = renderSceneFrame( renderer, device.get(), scene, clear );
        }

        test::RHITestImage image;
        if ( bOk && image.readTransient( renderer, "SceneColor" ) )
        {
            const test::Rgba8 corner = image.getPixel( 0, 0 );

            uint64 arrSum[3]{};
            uint32 drawn{ 0 };
            for ( uint32 y = 0; y < image.getHeight(); ++y )
            {
                for ( uint32 x = 0; x < image.getWidth(); ++x )
                {
                    const test::Rgba8 pixel = image.getPixel( x, y );
                    if ( test::RHITestImage::getColorDistance( pixel, corner ) < 24 )
                        continue;
                    arrSum[0] += static_cast<uint64>( pixel._r );
                    arrSum[1] += static_cast<uint64>( pixel._g );
                    arrSum[2] += static_cast<uint64>( pixel._b );
                    ++drawn;
                }
            }

            const sw::string label = sw::string( device->getBackendName() );
            SW_EXPECT_TRUE_MSG( drawn > image.getPixelCount() / 400,
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
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend available for transparent order test" );
}

/**
 * @brief [RenderPassGpuTest] 컴퓨트가 만든 드로우 커맨드가 **보이는 인스턴스만** 고르는지 (4 백엔드).
 * @details 컬링 컴퓨트는 배치의 개수를 줄이는 데서 끝나지 않고, 살아남은 인스턴스 번호를 압축 목록
 *          (g_SwVisibleInstanceIds)에 적는다. 정점 셰이더는 그 목록으로 자기 인스턴스를 찾는다 —
 *          언리얼 FInstanceCullingContext 와 같은 구조다.
 *
 *          개수만 줄이면 "배치 앞쪽 N 개"를 그린다. 한 배치 안에서 앞이 안 보이고 뒤가
 *          보이면 **보이는 쪽이 사라지고 안 보이는 쪽이 그려진다**. 개수만으로는 무엇을 그릴지 고를 수가
 *          없기 때문이다.
 *
 *          그래서 **메시 하나를 여럿이 공유해 한 배치에 인스턴스를 여러 개** 만들고, 그중 절반을 카메라
 *          뒤로 보낸다. 화면에 남아야 할 둘이 좌우에 제대로 찍히는지 본다.
 */
SW_TEST_CASE( RenderPassGpuTest, GpuGeneratedCommandsDrawOnlyVisibleInstances )
{
    // 기본 카메라는 (0, 1.2, 3.2) 에서 원점을 본다 — -Z 를 보므로 월드 +X 는 화면 왼쪽이다.
    constexpr float32 kSideOffset = 1.2f;
    // 카메라 뒤(+Z 쪽 멀리)로 보내 절두체 밖에 둔다.
    constexpr float32 kBehindCameraZ = 40.0f;

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
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
            bOk = renderSceneFrame( renderer, device.get(), scene, clear );
        }

        test::RHITestImage image;
        if ( bOk && image.readTransient( renderer, "SceneColor" ) )
        {
            const test::Rgba8 corner = image.getPixel( 0, 0 );

            uint32 arrDrawn[2]{};
            for ( uint32 y = 0; y < image.getHeight(); ++y )
            {
                for ( uint32 x = 0; x < image.getWidth(); ++x )
                {
                    const test::Rgba8 pixel = image.getPixel( x, y );
                    if ( test::RHITestImage::getColorDistance( pixel, corner ) < 24 )
                        continue;
                    ++arrDrawn[( x < image.getWidth() / 2 ) ? 0u : 1u];
                }
            }

            const sw::string label    = sw::string( device->getBackendName() );
            const uint32     minDrawn = image.getPixelCount() / 400;
            SW_EXPECT_TRUE_MSG( arrDrawn[0] > minDrawn && arrDrawn[1] > minDrawn,
                                ( label + ": 보이는 큐브 둘이 화면 좌우에 남지 않았다 (좌 " + sw::to_string( arrDrawn[0] ) +
                                  ", 우 " + sw::to_string( arrDrawn[1] ) + ", 최소 " + sw::to_string( minDrawn ) +
                                  ") — 컬링이 보이는 인스턴스를 고르지 못한다" )
                                    .c_str() );
        }
        SW_EXPECT_TRUE_MSG( bOk, device->getBackendName() );
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend available for GPU-generated command test" );
}

/**
 * @brief [RenderPassGpuTest] 배치마다 자기 머티리얼 **색**으로 그려지는지 (4 백엔드).
 * @details GpuSceneTest.PerBatchMaterialElementsAreDistinct 는 CPU 쪽 원소 선택까지만 본다. 여기서는 그
 *          원소가 실제로 셰이더까지 도달하는지를 픽셀로 본다 — 붉은 머티리얼과 푸른 머티리얼을 좌우에 두고
 *          그린다.
 *
 *          판정은 "붉은 픽셀 수" 가 아니라 **그려진 픽셀의 평균 (R - B)** 로 한다. 절대 색은 조명·톤매핑·
 *          백엔드 색공간에 따라 흔들리지만, 같은 조명을 받는 두 큐브 사이의 R-B 대소는 흔들리지 않는다.
 *          픽셀 수로 세면 백엔드마다 값이 널뛰어 판정이 되지 않는다.
 */
SW_TEST_CASE( RenderPassGpuTest, PerBatchMaterialColorsReachShader )
{
    // 기본 카메라는 -Z 를 본다 — 월드 +X 가 화면 **왼쪽**으로 간다. 그래서 붉은 큐브를 -X 에 두면
    // 화면 오른쪽이 붉어진다.
    constexpr float32 kSideOffset = 1.2f;

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "PerBatchMaterialColorScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        // 머티리얼은 **실제 에셋**을 읽어 색만 바꾼다 — 손으로 지은 XML 은 퍼뮤테이션 선언이 빠져 쿠킹해 둔
        // 셰이더 변형과 맞지 않는다(그러면 드로우가 통째로 사라져 검증이 무의미해진다).
        // initialize 가 아니라 loadFromFile 을 쓴다 — initialize 는 텍스처 에셋 해석까지 하므로 에셋
        // 시스템이 없는 테스트 프로세스에서는 못 쓴다. GPUScene 은 머티리얼 상수버퍼가 아니라
        // getBuffer() 를 구조버퍼 원소로 패킹하므로 여기까지면 충분하다.
        auto makeMaterial = []( const utf8* pColor ) -> sw::shared_ptr<sw::Material>
        {
            // 반환 대상을 하나로 둔다 — nullptr 과 material 을 섞어 돌려주면 NRVO 가 걸리지 않는다.
            sw::shared_ptr<sw::Material> material = sw::Material::create();
            if ( material->loadFromFile( "engine/materials/defaultmaterial.material" ) == false ||
                 material->setParameter( nullptr, sw::hashed_string( "color" ), pColor ) == false )
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
            bOk = renderSceneFrame( renderer, device.get(), scene, clear );
        }

        test::RHITestImage image;
        if ( bOk && image.readTransient( renderer, "SceneColor" ) )
        {
            // 그려진 픽셀(검은 배경이 아닌 곳)만 모아 좌/우 절반의 평균 (R - B) 를 낸다.
            int64  arrSumDiff[2]{};
            uint32 arrDrawn[2]{};

            // 배경은 검지 않다 — 패스 리소스가 정한 클리어 색과 톤매핑이 섞여 회색빛이 깔린다. 그래서
            // "검지 않은 픽셀" 이 아니라 **모서리 픽셀과 확연히 다른 픽셀** 을 큐브로 본다.
            const test::Rgba8 corner = image.getPixel( 0, 0 );
            for ( uint32 y = 0; y < image.getHeight(); ++y )
            {
                for ( uint32 x = 0; x < image.getWidth(); ++x )
                {
                    const test::Rgba8 pixel = image.getPixel( x, y );
                    if ( test::RHITestImage::getColorDistance( pixel, corner ) < 24 )
                        continue; // 배경
                    const uint32 side = ( x < image.getWidth() / 2 ) ? 0u : 1u;
                    arrSumDiff[side] += ( pixel._r - pixel._b );
                    ++arrDrawn[side];
                }
            }

            const sw::string label    = sw::string( device->getBackendName() );
            const uint32     minDrawn = image.getPixelCount() / 400;
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
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend available for per-batch material color test" );
}

/**
 * @brief [RenderPassGpuTest] 한 패스에 드로우가 둘일 때 배치마다 다른 상수가 유지되는지 (4 백엔드).
 * @details 배치 키에 메시 포인터가 들어가므로 **메시가 다르면 배치가 갈린다**. 그러면 한 패스가 드로우를
 *          두 번 하는데, 배치마다 다른 값(인스턴스 시작 등)을 패스당 하나뿐인 상수버퍼 슬롯에 드로우마다 덮어쓰면
 *          GPU 는 제출 뒤에 읽으므로 두 드로우가 **마지막 배치의 값**을 보게 되고, 앞 배치의 메시가 뒤 배치의
 *          인스턴스 자리에 그려진다(= 한쪽이 비어 보인다). 배치별 값은 배치 표(`g_SwBatches`)에 있고, 패스 상수는
 *          `bindForDraw` 가 드로우마다 새 슬롯을 잡는다.
 *
 *          메시를 하나만 쓰는 씬(벤치 씬 등)은 이 경로를 타지 않는다. 그래서 같은 큐브를 **두 번 따로 만들어** 포인터를 다르게 하고(기하는 동일해 가시성 변수를 없앤다)
 *          좌우로 떨어뜨린 뒤, 화면 좌우 양쪽에 모두 그려졌는지 본다.
 */
SW_TEST_CASE( RenderPassGpuTest, MultiBatchPassKeepsPerBatchConstants )
{
    /// @brief 두 큐브를 카메라가 보는 원점에서 좌우로 이만큼 떼어 놓는다.
    constexpr float32 kSideOffset = 1.1f;

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
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
            bOk = renderSceneFrame( renderer, device.get(), scene, clear );
        }
        SW_EXPECT_TRUE_MSG( bOk, device->getBackendName() );

        if ( bOk )
        {
            test::RHITestImage image;
            if ( image.readTransient( renderer, "SceneColor" ) )
            {
                // 화면을 좌/우로 나눠 각각 그려진 픽셀을 센다. 한 배치가 다른 배치의 인스턴스를 읽으면
                // 두 큐브가 같은 자리에 겹쳐 그려져 한쪽이 비어 버린다.
                uint32 arrSideCount[2]{};
                for ( uint32 y = 0; y < image.getHeight(); ++y )
                {
                    for ( uint32 x = 0; x < image.getWidth(); ++x )
                    {
                        if ( test::RHITestImage::isDefaultClearBackground( image.getPixel( x, y ) ) == false )
                            ++arrSideCount[x < image.getWidth() / 2 ? 0 : 1];
                    }
                }

                const sw::string label      = sw::string( device->getBackendName() );
                const uint32     minPerSide = image.getPixelCount() / 400;
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
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend available for multi-batch pass test" );
}

/**
 * @brief 인스턴스 애니메이션 컴퓨트가 돈 뒤에도 인스턴스 버퍼를 정점 셰이더가 읽을 수 있어야 한다.
 * @details 이 프리패스는 인스턴스 버퍼를 **UAV 로 쓴 다음** 같은 프레임에 정점 셰이더가 SRV 로 읽는다.
 *          D3D11 은 같은 리소스를 출력과 입력에 동시에 걸 수 없어서, UAV 를 안 떼면 런타임이 SRV 를
 *          조용히 NULL 로 강제한다 — 경고만 나오고 화면에서는 전부 사라진다.
 *
 *          spinSeed 를 세운 인스턴스가 하나도 없으면 디스패치 자체가 건너뛰어져 이 경로를 못 잡는다
 *          (패리티 테스트의 씬이 그렇다). 여기서는 **반드시 세운다**.
 *
 *          회전각은 시간에 따라 달라지므로 백엔드 사이 픽셀 수를 비교하지 않는다. 각 백엔드가
 *          "무언가를 그렸는지" 만 본다 — UAV 를 안 뗐을 때의 증상이 정확히 "아무것도 안 그린다" 다.
 */
SW_TEST_CASE( RenderPassGpuTest, InstanceAnimationKeepsInstancesReadable )
{

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
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
            bOk = renderSceneFrame( renderer, device.get(), scene, clear );
        }

        if ( bOk )
        {
            test::RHITestImage image;
            const bool         bRead = image.readTransient( renderer, "SceneColor" );
            SW_EXPECT_TRUE_MSG( bRead, "SceneColor readback" );
            if ( bRead )
            {
                const uint32 pixelCount = image.getPixelCount();
                uint32       drawnCount{ 0 };
                for ( uint32 y = 0; y < image.getHeight(); ++y )
                {
                    for ( uint32 x = 0; x < image.getWidth(); ++x )
                    {
                        if ( test::RHITestImage::isDefaultClearBackground( image.getPixel( x, y ) ) == false )
                            ++drawnCount;
                    }
                }
                const sw::string label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( device.getBackend() ) );
                SW_EXPECT_TRUE_MSG( pixelCount > 0 && drawnCount > pixelCount / 200,
                                    ( label + ": 인스턴스 애니메이션 뒤 SceneColor 가 비었다 (drawn " + sw::to_string( drawnCount ) + "/" +
                                      sw::to_string( pixelCount ) + ") — UAV 를 떼지 않아 정점 셰이더가 인스턴스를 못 읽는지 의심하라" )
                                        .c_str() );
            }
        }

        renderer.shutdown();

        SW_EXPECT_TRUE_MSG( bOk, ( sw::string( "backend " ) + sw::to_string( static_cast<uint32>( device.getBackend() ) ) + " execute" ).c_str() );
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend available for instance animation test" );
}

/**
 * @brief FrameRenderer 패리티 스모크 — DX11 / DX12 / Vulkan / OpenGL 각각 begin→execute→end(no present)
 * @details Present 없이 waitIdle까지. 가용 백엔드는 전부 성공해야 한다.
 */
SW_TEST_CASE( RenderPassGpuTest, FrameRendererParityAllBackends )
{
    /// @brief 큐브를 원점(카메라가 보는 지점)보다 이만큼 위에 둔다 — 그림을 세로로 비대칭하게 만들어 방향을 검사할 수 있게.
    constexpr float32 kParityCubeHeight = 1.0f;

    uint32  okCount{ 0 };
    bool    bHasReferenceMean{ false };
    float32 referenceMean[3]{};
    uint32  referenceDrawnCount{ 0 };

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene scene( "FrameRendererParityScene" );
        if ( bOk )
            bOk = scene.ensureDefaultCameras();

        // 메시를 **여러 개** 만든다 — 배치 키에 메시가 들어가므로 곧 배치 수이고, 배치가 하나뿐이면
        // 인스턴스 시작 오프셋이 늘 0 이라 인다이렉트 드로우의 백엔드 차이를 전혀 재지 못한다
        // (Vulkan 의 gl_InstanceIndex 가 firstInstance 를 포함하는 차이가 그런 것이다).
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
                    // 그러면 상하 반전을 평균으로도 무게중심으로도 잡을 수 없다.
                }
            }
        }
        cube = arrMesh[0];

        if ( bOk )
        {
            const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
            bOk = renderSceneFrame( renderer, device.get(), scene, clear );
        }

        // 실행 성공만으로는 부족하다 — 실제로 큐브가 찍혔는지, 백엔드끼리 같은 그림인지 SceneColor 픽셀로 본다.
        // 실행 성공만 보면 한 백엔드가 아무것도 안 그리거나 큐브를 한 자리에 겹쳐 그려도 통과한다.
        if ( bOk )
        {
            test::RHITestImage image;
            const bool         bRead = image.readTransient( renderer, "SceneColor" );
            SW_EXPECT_TRUE_MSG( bRead, "SceneColor readback" );
            if ( bRead )
            {
                const uint32 pixelCount = image.getPixelCount();
                uint64       arrSum[3]{};
                uint32       drawnCount{ 0 };
                uint64       drawnSumY{ 0 };
                for ( uint32 y = 0; y < image.getHeight(); ++y )
                {
                    for ( uint32 x = 0; x < image.getWidth(); ++x )
                    {
                        const test::Rgba8 pixel = image.getPixel( x, y );
                        arrSum[0] += pixel._r;
                        arrSum[1] += pixel._g;
                        arrSum[2] += pixel._b;
                        // 파이프라인 클리어 색(0.12, 0.15, 0.18 → 31, 38, 46) 이 아니면 무언가 그려진 픽셀이다.
                        if ( test::RHITestImage::isDefaultClearBackground( pixel ) == false )
                        {
                            ++drawnCount;
                            drawnSumY += y;
                        }
                    }
                }
                const sw::string label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( device.getBackend() ) );
                SW_EXPECT_TRUE_MSG( pixelCount > 0 && drawnCount > pixelCount / 200,
                                    ( label + ": SceneColor 에 큐브가 없다 (drawn " + sw::to_string( drawnCount ) + "/" + sw::to_string( pixelCount ) + ")" ).c_str() );
                // **방향 검사** — 평균과 픽셀 수는 상하 반전에 무관하다. 큐브를 원점 위에 두었으므로 올바른 방향이면
                // 그려진 픽셀의 무게중심이 이미지 위쪽(행 번호가 작은 쪽)에 있어야 한다. 뒤집히면 아래쪽으로 간다.
                // OpenGL 이 glClipControl 없이 좌하단 원점으로 그리면 이 단언이 잡는다.
                if ( drawnCount > 0 )
                {
                    const float32 centroidY = static_cast<float32>( drawnSumY ) / static_cast<float32>( drawnCount );
                    const float32 centerY   = static_cast<float32>( image.getHeight() ) * 0.5f;
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
                    // 거의 안 움직인다 — 인다이렉트 드로우의 인스턴스 오프셋이 백엔드마다 다르게 먹어도
                    // 평균만으로는 통과한다. 배치가 여럿일 때 한 백엔드만 큐브를 잃으면 여기서 걸린다.
                    const uint32 lowerBound = referenceDrawnCount - referenceDrawnCount / 8;
                    const uint32 upperBound = referenceDrawnCount + referenceDrawnCount / 8;
                    SW_EXPECT_TRUE_MSG( lowerBound <= drawnCount && drawnCount <= upperBound,
                                        ( label + ": 그려진 픽셀 수가 첫 백엔드와 다르다 (" + sw::to_string( drawnCount ) + " vs " +
                                          sw::to_string( referenceDrawnCount ) + ") — 배치별 인스턴스 오프셋을 의심하라" )
                                            .c_str() );
                }
            }
        }

        renderer.shutdown();

        if ( bOk )
            ++okCount;
        else
            SW_LOG_ERROR( "backend %# failed", static_cast<uint32>( device.getBackend() ) );
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend available for FrameRenderer parity" );

    SW_EXPECT_TRUE( okCount >= 1 );
    SW_EXPECT_EQUAL( okCount, sweep.getReadyCount() );
}

/**
 * @brief [RenderPassGpuTest] 뷰 모드(Lit/Unlit/Wireframe)가 **PSO 를 실제로 가르는지** 검증.
 * @details 이 기능이 조용히 죽는 방식은 하나다 — 값은 바뀌는데 드로우가 고르는 PSO 는 그대로인 것
 *          (툴바 콤보 값만 바뀌고 화면은 그대로). 그래서 여기서는 화면이
 *          아니라 **드로우가 고른 PSO 의 디스크립터**를 본다 — 픽셀 비교는 인스턴스 애니메이션이
 *          벽시계로 도는 탓에 프레임마다 달라져 판정 근거가 못 된다.
 *
 *          함께 보는 것: 그림자 패스는 뷰 모드를 **받지 않아야** 한다(와이어프레임 그림자를 구우면
 *          그림자가 선 몇 개로 남는다), 모드를 되돌리면 캐시에서 같은 PSO 가 다시 나와야 한다.
 */
SW_TEST_CASE( RenderPassGpuTest, ViewModeSelectsDistinctPipelineStates )
{
    auto hasDefine = []( const sw::RHIPipelineStateDesc& desc, const utf8* pDefine ) -> bool
    {
        for ( const sw::string& defineStr : desc._listShaderDefine )
        {
            if ( defineStr == pDefine )
                return true;
        }
        return false;
    };

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
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
                                        ( label + ": 그림자 패스가 와이어프레임으로 만들어진다" ).c_str() );
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
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for view mode test" );
}

/**
 * @brief 디바이스를 다시 만든 뒤에도 **같은 FrameRenderer** 가 다시 그리는지 — 백엔드 교체의 재현.
 * @details 앱의 교체 경로는 GT GpuScene 을 비우고, FrameRenderer 를 shutdown → 새 디바이스로 initialize 한다.
 *          **투명(유리) 머티리얼**로 잰다 — 불투명은 머티리얼 버퍼가 빠져도(폴백 0) 보이지만, 투명은 알파 0 이라
 *          사라진다. 예컨대 `clear()` 가 그룹 목록만 지우고 경로→인덱스 맵을 남기면 그룹 조회가 범위 밖 인덱스를 돌려줘
 *          빈 화면이 된다. 세 번 잰다: 첫 디바이스, 재생성 뒤 같은 렌더러, 재생성 뒤
 *          새 렌더러(대조군 — 렌더러에 남은 상태인지 디바이스 쪽인지 가른다).
 */
SW_TEST_CASE( RenderPassGpuTest, RendererSurvivesDeviceRecreate )
{
    test::RHITestDevice device( { sw::RHIBackend::DirectX12, sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL } );
    if ( device.isReady() == false )
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
    device.shutdownDevice();

    SW_ASSERT_TRUE( device.recreateDevice() );
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
}

/**
 * @brief 업로드 큐가 그리기 **전에** 정점 버퍼를 만들어 두는지 — 그리고 두 번 만들지 않는지.
 * @details 렌더 스레드는 그리기만 해야 한다. 큐가 먼저 만들어 두면 RT 의 `Mesh::initRhi` 는 핸들을 읽는 일이 된다.
 *          여기서는 (1) flush 뒤에 상주하는지, (2) 같은 메시를 여러 배치가 써도 한 번만 만드는지(중복 요청이
 *          워커 둘을 돌려 버퍼 하나를 새게 하면 안 된다), (3) 이미 상주하면 요청 자체가 쌓이지 않는지를 본다.
 */
SW_TEST_CASE( RenderPassGpuTest, UploadQueueMakesMeshesResidentBeforeDraw )
{
    test::RHITestDevice device( { sw::RHIBackend::DirectX12, sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan } );
    if ( device.isReady() == false )
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
}

/**
 * @brief 워커 생성을 못 하는 백엔드(OpenGL)에서는 업로드 큐가 메시를 받지 않는다 — 렌더 스레드가 그 프레임의 업로드에서 만든다.
 * @details 게임 스레드가 대신 만들면 렌더 스레드가 컨텍스트를 오래 쥔 동안(쿠킹 안 된 셰이더의 실시간 컴파일) 컨텍스트 대기가 시간을 넘겨
 *          `acquireGraphicsContextBlocking timed out` · `createVertexBuffer failed` 가 [Error] 로 남는다(쿠킹 전 Debug -gl 환경 쇼케이스).
 */
SW_TEST_CASE( RenderPassGpuTest, UploadQueueLeavesMeshesToTheRenderThreadWithoutThreadSafeCreation )
{
    test::RHITestDevice device( { sw::RHIBackend::OpenGL } );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "No OpenGL backend for upload queue test" );
    SW_ASSERT_TRUE( device->getCapabilities()._bThreadSafeResourceCreation == SW_FALSE );

    sw::GpuUploadQueue queue;
    queue.bindDevice( device.get(), &sw::engine::getTaskManager() );

    sw::shared_ptr<sw::Mesh> mesh = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( mesh.get() );
    queue.requestMesh( mesh );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
    SW_EXPECT_EQUAL( 0u, queue.flush() );
    SW_EXPECT_FALSE( mesh->isRhiValid() );

    // 렌더 스레드 자리(여기서는 컨텍스트를 쥔 시험 스레드)에서 만든다 — GpuScene 업로드가 하는 일이다.
    SW_EXPECT_TRUE( mesh->initRhi( device.get() ) );
    mesh->releaseRhi( device.get() );
}

/**
 * @brief 렌더 스레드가 프레임을 든 동안 다른 스레드가 메시의 마지막 소유를 놓으면 정점 버퍼 반환이 그 프레임 뒤로 미뤄진다.
 * @details 지형 LOD 교체 · 씬 교체가 게임 스레드에서 메시를 놓는다. 그 자리에서 `destroyBuffer` 를 부르면 렌더 스레드가 기록하며 읽는
 *          백엔드 표(DX11 버퍼 SRV · 기록 상태의 묶인 정점 버퍼)를 쓴다 — DX11 + 환경 쇼케이스가 DataRaceDetector 로 죽었다(5 번 중 2 번).
 */
SW_TEST_CASE( RenderPassGpuTest, MeshReleaseWaitsForTheRenderThreadFrame )
{
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string         label = sw::string( device->getBackendName() );
        sw::shared_ptr<sw::Mesh> mesh  = sw::MeshUtil::createUnitCube();
        SW_ASSERT_NOT_NULL( mesh.get() );
        SW_EXPECT_TRUE_MSG( mesh->initRhi( device.get() ), label.c_str() );

        // 렌더 스레드가 프레임을 들고 있다. 이 시험 스레드는 렌더 스레드가 아니다(묶인 렌더 스레드가 없다).
        const size_t deferredBefore = device->getDeferredHandleCount();
        device->notifyRenderFrameQueued();
        mesh.reset();
        SW_EXPECT_TRUE_MSG( device->getDeferredHandleCount() == deferredBefore + 1, label.c_str() );

        // 프레임이 끝나고 비우면 내린다.
        device->notifyRenderFrameRetired();
        device->flushDeferredHandleReleases();
        SW_EXPECT_TRUE_MSG( device->getDeferredHandleCount() == 0, label.c_str() );
    }
}

/**
 * @brief 새 디바이스가 서면 등록부가 **스스로** 리소스를 되살린다 — 아무도 각 리소스를 손으로 다시 올리지 않는다.
 * @details 이 테스트가 지키는 것은 "어느 캐시를 다시 올려야 하는지 기억하지 않아도 된다" 이다. 되살릴 목록을
 *          바깥이 들면 목록에서 빠진 것은 교체 뒤 조용히 비어 있다. 아래 initAllFor 한 줄을 지우면 이 테스트가 빨개진다.
 */
SW_TEST_CASE( RenderPassGpuTest, RegistryRestoresResourcesOnNewDevice )
{
    test::RHITestDevice device( { sw::RHIBackend::DirectX12, sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL } );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "No RHI backend for render resource registry test" );

    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( cube.get() );
    SW_EXPECT_TRUE( cube->initRhi( device.get() ) );
    SW_EXPECT_TRUE( cube->isRhiValid() );

    device.shutdownDevice();
    SW_EXPECT_TRUE( cube->isRhiValid() == false );

    SW_ASSERT_TRUE( device.recreateDevice() );

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
}

/**
 * @brief 디바이스가 죽으면 **어디서 죽었든** 세대가 올라가야 한다 — 그래야 GPU 핸들이 스스로 무효가 된다.
 * @details 세대 카운터가 있는 이유는 "GPU 핸들을 든 CPU 객체(Mesh · MaterialInstance)가 디바이스보다 오래 산다" 는 것
 *          하나다. 세대를 올리는 일이 `RHI` 매니저의 경로(shutdown · recreateDevice)에만 있으면 `RHI::createDevice`
 *          로 직접 만든 디바이스(테스트가 그렇게 쓴다)는 죽어도 세대가 그대로라, 그 디바이스에 올라간 메시가 계속
 *          "상주" 라고 답한다. 그러면 새 디바이스에 옛 핸들을 그대로 돌려주고, 해제는 죽은 디바이스에 destroy 를
 *          부른다(UAF).
 */
SW_TEST_CASE( RenderPassGpuTest, DeviceDeathInvalidatesGpuHandles )
{
    test::RHITestDevice device( { sw::RHIBackend::DirectX12, sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL } );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "No RHI backend for device generation test" );

    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( cube.get() );
    SW_EXPECT_TRUE( cube->initRhi( device.get() ) );
    SW_EXPECT_TRUE( cube->isRhiValid() );
    SW_EXPECT_TRUE( cube->getVertexBuffer() != 0 );

    // **releaseGpu 를 부르지 않고** 디바이스를 죽인다 — 실수로 잊은 경우가 바로 이 카운터가 막아야 할 상황이다.
    device.shutdownDevice();

    // **동작으로 단언한다** — 세대 번호든 수명 토큰이든 구현은 바뀔 수 있다. 바뀌면 안 되는 것은
    // "디바이스가 죽으면 그 핸들은 더 이상 상주가 아니다" 뿐이다.
    SW_EXPECT_TRUE_MSG( cube->isRhiValid() == false, "죽은 디바이스의 핸들을 아직 상주 라고 답한다" );

    // 새 디바이스에는 **새로** 올라가야 한다. 옛 핸들을 그대로 돌려주면 그 드로우는 남의 버퍼를 읽는다.
    SW_ASSERT_TRUE( device.recreateDevice() );
    SW_EXPECT_TRUE( cube->initRhi( device.get() ) );
    SW_EXPECT_TRUE( cube->isRhiValid() );
    SW_EXPECT_TRUE( cube->getVertexBuffer() != 0 );
}

/**
 * @brief [RenderPassGpuTest] **디퍼드 파이프라인이 실제로 그리는지** 검증.
 * @details 앱이 쓰지 않는 파이프라인은 조용히 썩는다. 디퍼드가 깨지는 방식은 이런 것들이다:
 *          (1) 풀스크린 패스에 `_cullMode="Back"` 이 적히면 삼각형이 컬링되고,
 *          (2) 머티리얼 셰이더가 SV_TARGET 하나만 내면 G버퍼 노멀이 클리어 값 그대로이고,
 *          (3) 스크린샷 첨부를 `"SceneColor"` 리터럴로 고르면 디퍼드는 한 장도 못 찍는다.
 *          셋 다 오류도 경고도 없다 — 그림을 봐야만 드러난다. 그래서 이 테스트는 **픽셀**을 본다.
 * @note 고유 색 수로 본다. "비배경 픽셀 수" 는 클리어 색·톤매핑에 무너지지만, 화면이 통째로 한
 *       색이면(= 아무것도 안 그렸다) 고유 색은 반드시 1 이다.
 */
SW_TEST_CASE( RenderPassGpuTest, DeferredPipelineDrawsGeometry )
{
    test::RHITestDevice device( { sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL, sw::RHIBackend::DirectX12 } );
    if ( device.isReady() == false )
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

    test::RHITestImage image;
    const bool         bRead = image.readTransient( renderer, presented );
    SW_EXPECT_TRUE_MSG( bRead, "화면에 나간 첨부를 되읽지 못했다" );
    if ( bRead )
    {
        const uint32 bytesPerPixel = image.getBytesPerPixel();
        uint32       distinct      = 0;
        uint64       arrDistinctKey[16]{};
        for ( uint32 row = 0; row < image.getHeight() && distinct < 2; ++row )
        {
            for ( uint32 col = 0; col < image.getWidth() && distinct < 2; ++col )
            {
                const uint8* pPixel = image.getRawPixel( col, row );
                uint64       key    = 0;
                for ( uint32 b = 0; b < bytesPerPixel && b < 8; ++b )
                    key |= static_cast<uint64>( pPixel[b] ) << ( b * 8 );
                bool bFound = false;
                for ( uint32 slot = 0; slot < distinct; ++slot )
                {
                    if ( arrDistinctKey[slot] == key )
                    {
                        bFound = true;
                        break;
                    }
                }
                if ( bFound == false )
                    arrDistinctKey[distinct++] = key;
            }
        }
        SW_EXPECT_TRUE_MSG( distinct >= 2,
                            "디퍼드 파이프라인이 화면을 한 색으로 채웠다 — 지오메트리가 하나도 안 그려졌다" );
    }
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

        test::RHITestImage image;
        if ( image.readTransient( renderer, "SceneColor" ) == false )
            return result;
        uint64 arrSum[3]{};
        for ( uint32 y = 0; y < image.getHeight(); ++y )
        {
            for ( uint32 x = 0; x < image.getWidth(); ++x )
            {
                const test::Rgba8 pixel = image.getPixel( x, y );
                arrSum[0] += pixel._r;
                arrSum[1] += pixel._g;
                arrSum[2] += pixel._b;
                if ( test::RHITestImage::isDefaultClearBackground( pixel ) == false )
                    ++result._drawnCount;
            }
        }
        const uint32 pixelCount = image.getPixelCount();
        for ( uint32 channel = 0; channel < 3; ++channel )
            result._arrMean[channel] = pixelCount > 0 ? static_cast<float32>( arrSum[channel] ) / static_cast<float32>( pixelCount ) : 0.0f;
        result._bOk = pixelCount > 0;
        return result;
    };

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( device.getBackend() ) );

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
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the merged draw test" );
}

/**
 * @brief [RenderPassGpuTest] SSAO 결과가 실제로 그림에 닿는다 — 끄면 밝아진다
 * @details 디퍼드 XML 은 Bloom 의 입력으로 AOColor 를 선언한다. 엔진이 그 패스에 SourceColor 하나만 걸거나 postbloom.hlsl 이 그것만
 *          읽으면 SSAO 는 매 프레임 풀스크린 패스를 돌고 결과는 버려진다.
 *          "패스가 돈다" 와 "결과가 쓰인다" 는 다른 말이라, AO 역할을 끈 프레임과 켠 프레임의 Bloom 출력을 비교한다.
 */
SW_TEST_CASE( RenderPassGpuTest, AmbientOcclusionReachesBloom )
{
    /// @brief 첨부 하나의 평균 밝기와 "가림이 있는(255 미만)" 픽셀 수.
    struct Stat
    {
        float64 _mean{ 0.0 };
        uint32  _darkCount{ 0 };
        bool    _bOk{ false };
    };
    auto readStat = []( sw::FrameRenderer& renderer, const utf8* pAttachment ) -> Stat
    {
        Stat               stat{};
        test::RHITestImage image;
        if ( image.readTransient( renderer, pAttachment ) == false )
            return stat;
        uint64 sum{ 0 };
        uint32 count{ 0 };
        for ( uint32 row = 0; row < image.getHeight(); ++row )
        {
            for ( uint32 col = 0; col < image.getWidth(); ++col )
            {
                const uint32 r = image.getPixel( col, row )._r;
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

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( device.getBackend() ) );

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
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the ambient occlusion test" );
}

/**
 * @brief [RenderPassGpuTest] 렌더 타깃 목록이 **에디터가 읽을 수 있게** 공개된다
 * @details 에디터는 `FrameRenderer` 인스턴스를 쥘 방법이 없다 — 트랜지언트는 private 맵이다.
 *          그래서 렌더러가 `RenderTargetRegistry` 에 목록을 공개하고 `RenderTargetPanel` 이 그것을 읽는다
 *          (그것이 없으면 `-gv_screenshotAttachment` 로 프로세스를 다시 띄워 PPM 을 찍어야 한다).
 * @note 패널 자체는 ImGui 라 테스트가 붙지 않는다. 대신 **패널이 먹는 데이터**를 여기서 고정한다 —
 *       목록이 비거나 이름이 바뀌면 패널은 조용히 빈 창이 된다(그게 이 계약의 유일한 실패 모드다).
 */
SW_TEST_CASE( RenderPassGpuTest, RenderTargetsArePublishedForTheEditor )
{
    test::RHITestDevice device( { sw::RHIBackend::DirectX11, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL, sw::RHIBackend::DirectX12 } );
    if ( device.isReady() == false )
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
}

#if !defined( SW_SHIPPING ) // 모프 시각 고정(`setAnimationTimeOverride`)은 배포본에 없다 — 벽시계로 찍으면 흔들린다
/**
 * @brief GPU 메시 모프의 **정점 셰이더 풀 읽기**가 네 백엔드에서 같은지 픽셀로 봅니다.
 * @details 세 장을 찍는다 — (A) 모프 안 켬(레스트), (B) 모프 켜되 컴퓨트 없이 **레스트 버퍼를 그대로 풀에
 *          물림**(`setMeshMorphDiag(2)`), (C) 진짜 모프. B 의 정답은 A 와 **같은 그림**이다: 풀 원소 i 가
 *          정점 i 의 레스트 값이므로 정점 셰이더가 제 원소를 읽으면 레스트와 픽셀이 같아야 한다.
 *          OpenGL 드라이버가 early-return 모양의 `swComputeMorphElement` 를 잘못 컴파일하면 정점마다 **한 칸 앞
 *          원소**를 읽고, 그것이 정확히 B≠A 로 나타난다(binding.hlsli 주석). C 는 "모프가 실제로 걸리는가"
 *          만 본다 — A 와 **달라야** 한다.
 *          **모프 시각을 고정한다**(`setAnimationTimeOverride`). 변위는 sin(2t + F·(x+y+z)) 인데 단위 큐브 정점의 위상(F = 6)은 2π 로 접으면
 *          모두 π ± 0.43 근처라, t ≈ π/2 + kπ 에서는 모든 정점의 변위가 함께 0 을 지나 노멀 음영만 남는다(달라진 픽셀 약 2 %, 문턱 5 % 미만).
 *          벽시계로 찍으면 부하로 늦어진 실행이 그 창(주기 π 초의 약 12 %)에 들어가 진다. t = 3π/4 는 |sin 2t| = 1 인 자리다.
 */
SW_TEST_CASE( RenderPassGpuTest, MorphPoolIdentityMatchesRest )
{
    /// @brief 그림 하나의 요약 — 그려진 픽셀 수와 채널 평균, 그리고 픽셀 비교용 원본.
    struct Snapshot
    {
        uint32             _drawnCount{ 0 };
        float32            _arrMean[3]{};
        bool               _bOk{ false };
        test::RHITestImage _image;
    };
    /// @brief 두 그림에서 어느 채널이든 8 이상 다른 픽셀 수 — 실루엣 수보다 튼튼한 지표(변형은 음영도 바꾼다).
    auto countDifferentPixels = []( const Snapshot& a, const Snapshot& b ) -> uint32
    {
        if ( a._image.getWidth() != b._image.getWidth() || a._image.getHeight() != b._image.getHeight() )
            return 0;
        uint32 count{ 0 };
        for ( uint32 y = 0; y < a._image.getHeight(); ++y )
        {
            for ( uint32 x = 0; x < a._image.getWidth(); ++x )
            {
                const test::Rgba8 pixelA = a._image.getPixel( x, y );
                const test::Rgba8 pixelB = b._image.getPixel( x, y );
                const auto        isFar  = []( uint8 lhs, uint8 rhs )
                { return lhs > rhs + 8 || rhs > lhs + 8; };
                if ( isFar( pixelA._r, pixelB._r ) || isFar( pixelA._g, pixelB._g ) || isFar( pixelA._b, pixelB._b ) )
                    ++count;
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

        test::RHITestImage image;
        if ( image.readTransient( renderer, "SceneColor" ) == false )
            return result;

        uint64 arrSum[3]{};
        for ( uint32 y = 0; y < image.getHeight(); ++y )
        {
            for ( uint32 x = 0; x < image.getWidth(); ++x )
            {
                const test::Rgba8 pixel = image.getPixel( x, y );
                arrSum[0] += pixel._r;
                arrSum[1] += pixel._g;
                arrSum[2] += pixel._b;
                // 파이프라인 클리어 색(31, 38, 46) 이 아니면 그려진 픽셀이다 — FrameRendererParityAllBackends 와 같은 기준.
                if ( test::RHITestImage::isDefaultClearBackground( pixel ) == false )
                    ++result._drawnCount;
            }
        }
        const uint32 pixelCount = image.getPixelCount();
        for ( uint32 channel = 0; channel < 3; ++channel )
            result._arrMean[channel] = pixelCount > 0 ? static_cast<float32>( arrSum[channel] ) / static_cast<float32>( pixelCount ) : 0.0f;
        result._bOk   = pixelCount > 0;
        result._image = std::move( image );
        return result;
    };

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( device.getBackend() ) );

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();
        // 변위가 가장 큰 시각에 고정한다(위 @details) — 찍는 순간의 벽시계에 따라 (C) 의 답이 바뀌지 않게.
        renderer.setAnimationTimeOverride( sw::MathUtil::kPi * 0.75f );

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
                // 지표는 **달라진 픽셀 수**다 — 변형은 위치와 노멀을 같이 바꾸므로 음영이 바뀐 픽셀까지 센다. 고정한 시각(3π/4)에서
                // 네 백엔드 모두 그려진 픽셀의 약 18 % 가 다르다(문턱 5 %).
                const uint32 diffCount = countDifferentPixels( rest, morphed );
                SW_EXPECT_TRUE_MSG( diffCount > rest._drawnCount / 20,
                                    ( label + ": 모프를 켰는데 그림이 레스트와 같다 (달라진 픽셀 " + sw::to_string( diffCount ) + " / 그려진 " +
                                      sw::to_string( rest._drawnCount ) + ") — 컴퓨트 결과가 정점 셰이더에 닿지 않는다" )
                                        .c_str() );
            }
            renderer.setMeshMorphDiag( -1 );
        }
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the morph pool identity test" );
}
#endif

/**
 * @brief [RenderPassGpuTest] GPU 스키닝 — 스킨드 메시는 유닛의 팔레트대로 휘고, 그 그림이 CPU 로 미리 스키닝한 메시의 그림과 같다(네 백엔드)
 * @details 단위 큐브의 위쪽 정점(y > 0)은 본 1, 아래쪽은 본 0 에 가중치 1 로 묶고, 본 1 을 Z 축으로 50° 돌린다. (A) 바인드 포즈(팔레트 단위)는
 *          스킨 없는 큐브와 같아야 하고, (B) 돌린 포즈는 바인드 포즈와 **달라야** 하며, (C) 같은 회전을 CPU 에서 정점에 걸어 둔 정적 큐브와 같아야 한다.
 *          (C) 가 지면 팔레트 행 · 팔레트 시작 · 행렬 열 순서 중 하나가 GPU 와 CPU 에서 다르다(meshskin.hlsl · `GpuMeshMorphPool::uploadSkinPalettes`).
 */
SW_TEST_CASE( RenderPassGpuTest, SkinnedMeshFollowsPaletteLikeCpuSkinning )
{
    struct Snapshot
    {
        uint32             _drawnCount{ 0 };
        bool               _bOk{ false };
        test::RHITestImage _image;
    };
    /// @brief 두 그림에서 어느 채널이든 12 넘게 다른 픽셀 수입니다.
    auto countDifferentPixels = []( const Snapshot& a, const Snapshot& b ) -> uint32
    {
        if ( a._image.getWidth() != b._image.getWidth() || a._image.getHeight() != b._image.getHeight() )
            return 0xFFFFFFFFu;
        uint32 count{ 0 };
        for ( uint32 y = 0; y < a._image.getHeight(); ++y )
        {
            for ( uint32 x = 0; x < a._image.getWidth(); ++x )
            {
                const test::Rgba8 pixelA = a._image.getPixel( x, y );
                const test::Rgba8 pixelB = b._image.getPixel( x, y );
                const auto        isFar  = []( uint8 lhs, uint8 rhs )
                { return lhs > rhs + 12 || rhs > lhs + 12; };
                if ( isFar( pixelA._r, pixelB._r ) || isFar( pixelA._g, pixelB._g ) || isFar( pixelA._b, pixelB._b ) )
                    ++count;
            }
        }
        return count;
    };
    auto snapshot = []( sw::FrameRenderer& renderer, sw::IRHIDevice& device, sw::Scene& scene ) -> Snapshot
    {
        Snapshot         result{};
        const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
        for ( uint32 frame = 0; frame < 4; ++frame )
        {
            // 팔레트는 애니메이션 시스템이 틱 뒤에 만든다 — 시험은 틱 대신 평가만 부른다.
            scene.getObjectManager()->getAnimationSystem().evaluate( 0.0f );
            device.beginFrame( clear );
            if ( renderer.execute( &device, &scene ) == false )
                return result;
            device.endFrame( false, false );
            device.waitIdle();
        }
        if ( result._image.readTransient( renderer, "SceneColor" ) == false )
            return result;
        for ( uint32 y = 0; y < result._image.getHeight(); ++y )
        {
            for ( uint32 x = 0; x < result._image.getWidth(); ++x )
            {
                if ( test::RHITestImage::isDefaultClearBackground( result._image.getPixel( x, y ) ) == false )
                    ++result._drawnCount;
            }
        }
        result._bOk = result._drawnCount > 0;
        return result;
    };

    /**
     * @class BendTask
     * @brief 기본 포즈 단계에서 본 1 을 정해진 각만큼 Z 축으로 돌리는 일입니다.
     */
    class BendTask final : public sw::IAnimationPhaseTask
    {
    public:
        explicit BendTask( float32 angle )
            : _angle{ angle }
        {
        }
        bool isAnimationActive() const override { return true; }
        void runAnimationPhase( sw::AnimationPhase phase, sw::SkeletalMeshComponent& unit, const sw::AnimationFrameContext& context ) override
        {
            (void)context;
            if ( phase != sw::AnimationPhase::BasePose )
                return;
            sw::BoneTransform bone = unit.getLocalPose().getBoneTransform( 1 );
            bone._rotation         = sw::quaternion::createFromAxisAngle( sw::float3{ 0.0f, 0.0f, 1.0f }, _angle );
            unit.getLocalPose().setBoneTransform( 1, bone );
        }
        float32 _angle;
    };

    constexpr float32            kBendAngle = 0.87f; // 약 50°
    sw::shared_ptr<sw::Skeleton> skeleton   = sw::make_shared<sw::Skeleton>();
    (void)skeleton->addBone( sw::hashed_string( "base" ), -1, sw::BoneTransform{}, sw::float4x4::Identity );
    (void)skeleton->addBone( sw::hashed_string( "top" ), 0, sw::BoneTransform{}, sw::float4x4::Identity );
    skeleton->computeInverseBindFromReference();

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( device.getBackend() ) );
        // 씬마다 렌더러를 따로 둔다 — 한 렌더러의 씬 빌더는 그리던 씬의 수집 캐시를 들고 있어 씬을 바꿔 그리면 옛 배치가 남는다.
        sw::FrameRenderer renderer;
        sw::FrameRenderer staticRenderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady() && staticRenderer.initialize( device.get() ) && staticRenderer.isReady();
        if ( bOk && device->getCapabilities()._bGpuMeshMorph == SW_FALSE )
            continue;

        // 스킨드 큐브와, 같은 정점에 CPU 스키닝을 걸어 둔 정적 큐브를 따로 둔 두 씬.
        sw::shared_ptr<sw::Mesh> bindCube = sw::MeshUtil::createUnitCube();
        SW_ASSERT_NOT_NULL( bindCube.get() );
        sw::vector<sw::MeshSkinVertex> listSkin;
        sw::vector<sw::RHIVertex>      listCpuSkinned = bindCube->getVertices();
        const sw::float4x4             bend           = sw::float4x4::createFromQuaternion( sw::quaternion::createFromAxisAngle( sw::float3{ 0.0f, 0.0f, 1.0f }, kBendAngle ) );
        for ( sw::RHIVertex& vertex : listCpuSkinned )
        {
            sw::MeshSkinVertex skin{};
            skin._arrJoint[0] = vertex._arrPosition[1] > 0.0f ? 1u : 0u;
            listSkin.push_back( skin );
            if ( skin._arrJoint[0] == 0 )
                continue;
            const sw::float3 position = sw::float3::transform( sw::float3{ vertex._arrPosition }, bend );
            const sw::float3 normal   = sw::float3::transformVector( sw::float3{ vertex._arrNormal }, bend );
            vertex._arrPosition[0]    = position._x;
            vertex._arrPosition[1]    = position._y;
            vertex._arrPosition[2]    = position._z;
            vertex._arrNormal[0]      = normal._x;
            vertex._arrNormal[1]      = normal._y;
            vertex._arrNormal[2]      = normal._z;
        }
        sw::shared_ptr<sw::Mesh> skinnedCube = sw::Mesh::create();
        skinnedCube->setVertices( bindCube->getVertices() );
        skinnedCube->setSkin( listSkin, 2 );
        sw::shared_ptr<sw::Mesh> cpuCube = sw::Mesh::create();
        cpuCube->setVertices( listCpuSkinned );

        sw::Scene skinnedScene( "SkinnedCubeScene" );
        sw::Scene staticScene( "CpuSkinnedCubeScene" );
        bOk                                 = bOk && skinnedScene.ensureDefaultCameras() && staticScene.ensureDefaultCameras();
        sw::SkeletalMeshComponent* pSkinned = nullptr;
        sw::MeshComponent*         pStatic  = nullptr;
        if ( bOk )
        {
            sw::GameObject* pSkinnedObject = skinnedScene.getObjectManager()->createGameObject( sw::hashed_string( "Skinned" ) );
            sw::GameObject* pStaticObject  = staticScene.getObjectManager()->createGameObject( sw::hashed_string( "Static" ) );
            bOk                            = pSkinnedObject != nullptr && pStaticObject != nullptr;
            if ( bOk )
            {
                pSkinned = pSkinnedObject->addComponent<sw::SkeletalMeshComponent>();
                pStatic  = pStaticObject->addComponent<sw::MeshComponent>();
                bOk      = pSkinned != nullptr && pStatic != nullptr;
            }
        }
        if ( bOk )
        {
            pSkinned->setSkeleton( skeleton );
            pSkinned->setMesh( skinnedCube );
            pSkinned->setBoundsRadius( 2.0f );
            pSkinned->setLocalPosition( sw::float3{ 0.0f, 1.0f, 0.0f } );
            pStatic->setMesh( bindCube );
            pStatic->setLocalPosition( sw::float3{ 0.0f, 1.0f, 0.0f } );
        }
        SW_EXPECT_TRUE_MSG( bOk, ( label + ": 씬·렌더러 준비 실패" ).c_str() );
        if ( bOk == false )
            continue;

        // (A) 바인드 포즈 = 스킨 없는 큐브
        const Snapshot bindSkinned = snapshot( renderer, *device, skinnedScene );
        const Snapshot bindStatic  = snapshot( staticRenderer, *device, staticScene );
        SW_EXPECT_TRUE_MSG( bindSkinned._bOk && bindStatic._bOk, ( label + ": 바인드 포즈 그림을 못 읽었다" ).c_str() );
        const uint32 bindDiff = countDifferentPixels( bindSkinned, bindStatic );
        SW_EXPECT_TRUE_MSG( bindDiff <= bindStatic._drawnCount / 50 + 8,
                            ( label + ": 바인드 포즈의 스킨드 큐브가 정적 큐브와 다르다 (달라진 픽셀 " + sw::to_string( bindDiff ) + ")" ).c_str() );

        // (B) · (C) 본 1 을 돌린다.
        BendTask task( kBendAngle );
        pSkinned->addAnimationPhaseTask( &task );
        pStatic->setMesh( cpuCube );
        const Snapshot bentSkinned = snapshot( renderer, *device, skinnedScene );
        const Snapshot bentStatic  = snapshot( staticRenderer, *device, staticScene );
        SW_EXPECT_TRUE_MSG( bentSkinned._bOk && bentStatic._bOk, ( label + ": 굽힌 포즈 그림을 못 읽었다" ).c_str() );
        const uint32 poseDiff = countDifferentPixels( bindSkinned, bentSkinned );
        SW_EXPECT_TRUE_MSG( poseDiff > bindStatic._drawnCount / 10,
                            ( label + ": 본을 돌렸는데 스킨드 큐브가 그대로다 (달라진 픽셀 " + sw::to_string( poseDiff ) + ") — 팔레트가 GPU 에 닿지 않는다" ).c_str() );
        const uint32 cpuDiff = countDifferentPixels( bentSkinned, bentStatic );
        SW_EXPECT_TRUE_MSG( cpuDiff <= bentStatic._drawnCount / 50 + 8,
                            ( label + ": GPU 스키닝이 CPU 스키닝과 다르다 (달라진 픽셀 " + sw::to_string( cpuDiff ) + " / 그려진 " +
                              sw::to_string( bentStatic._drawnCount ) + ")" )
                                .c_str() );
        pSkinned->removeAnimationPhaseTask( &task );
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the GPU skinning test" );
}

/**
 * @brief [RenderPassGpuTest] GPU 모프 타깃 — 유닛의 모프 가중치가 스키닝 앞에 레스트를 밀고, 그 그림이 CPU 에서 (레스트 + 가중치 × 차이) 를 스키닝한 정적 큐브와 같다(네 백엔드)
 * @details 큐브의 위쪽 정점(본 1)을 +X 로 0.8 미는 타깃 `push` 와 아무것도 안 하는 타깃 `idle` 을 둔다. 가중치 0.6 · 본 1 을 Z 축 50° — 순서가
 *          "모프 → 스킨" 이 아니면(스킨 뒤에 더하면) 민 방향이 회전하지 않아 CPU 그림과 갈린다. 가중치 0 이면 모프 없는 굽힘과 같아야 한다.
 */
SW_TEST_CASE( RenderPassGpuTest, MorphWeightsDeformBeforeSkinningLikeCpu )
{
    struct Snapshot
    {
        uint32             _drawnCount{ 0 };
        bool               _bOk{ false };
        test::RHITestImage _image;
    };
    auto countDifferentPixels = []( const Snapshot& a, const Snapshot& b ) -> uint32
    {
        if ( a._image.getWidth() != b._image.getWidth() || a._image.getHeight() != b._image.getHeight() )
            return 0xFFFFFFFFu;
        uint32 count{ 0 };
        for ( uint32 y = 0; y < a._image.getHeight(); ++y )
        {
            for ( uint32 x = 0; x < a._image.getWidth(); ++x )
            {
                const test::Rgba8 pixelA = a._image.getPixel( x, y );
                const test::Rgba8 pixelB = b._image.getPixel( x, y );
                const auto        isFar  = []( uint8 lhs, uint8 rhs )
                { return lhs > rhs + 12 || rhs > lhs + 12; };
                if ( isFar( pixelA._r, pixelB._r ) || isFar( pixelA._g, pixelB._g ) || isFar( pixelA._b, pixelB._b ) )
                    ++count;
            }
        }
        return count;
    };
    auto snapshot = []( sw::FrameRenderer& renderer, sw::IRHIDevice& device, sw::Scene& scene ) -> Snapshot
    {
        Snapshot         result{};
        const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
        for ( uint32 frame = 0; frame < 4; ++frame )
        {
            scene.getObjectManager()->getAnimationSystem().evaluate( 0.0f );
            device.beginFrame( clear );
            if ( renderer.execute( &device, &scene ) == false )
                return result;
            device.endFrame( false, false );
            device.waitIdle();
        }
        if ( result._image.readTransient( renderer, "SceneColor" ) == false )
            return result;
        for ( uint32 y = 0; y < result._image.getHeight(); ++y )
        {
            for ( uint32 x = 0; x < result._image.getWidth(); ++x )
            {
                if ( test::RHITestImage::isDefaultClearBackground( result._image.getPixel( x, y ) ) == false )
                    ++result._drawnCount;
            }
        }
        result._bOk = result._drawnCount > 0;
        return result;
    };

    /**
     * @class MorphBendTask
     * @brief 기본 포즈 단계에서 본 1 을 돌리고 타깃 0(`push`)의 가중치를 정하는 일입니다.
     */
    class MorphBendTask final : public sw::IAnimationPhaseTask
    {
    public:
        MorphBendTask( float32 angle, float32 weight )
            : _angle{ angle }
            , _weight{ weight }
        {
        }
        bool isAnimationActive() const override { return true; }
        void runAnimationPhase( sw::AnimationPhase phase, sw::SkeletalMeshComponent& unit, const sw::AnimationFrameContext& context ) override
        {
            (void)context;
            if ( phase != sw::AnimationPhase::BasePose )
                return;
            sw::BoneTransform bone = unit.getLocalPose().getBoneTransform( 1 );
            bone._rotation         = sw::quaternion::createFromAxisAngle( sw::float3{ 0.0f, 0.0f, 1.0f }, _angle );
            unit.getLocalPose().setBoneTransform( 1, bone );
            unit.setMorphWeight( 0, _weight );
        }
        float32 _angle;
        float32 _weight;
    };

    constexpr float32            kBendAngle   = 0.87f;
    constexpr float32            kMorphWeight = 0.6f;
    const sw::float3             pushDelta{ 0.8f, 0.0f, 0.0f };
    sw::shared_ptr<sw::Skeleton> skeleton = sw::make_shared<sw::Skeleton>();
    (void)skeleton->addBone( sw::hashed_string( "base" ), -1, sw::BoneTransform{}, sw::float4x4::Identity );
    (void)skeleton->addBone( sw::hashed_string( "top" ), 0, sw::BoneTransform{}, sw::float4x4::Identity );
    skeleton->computeInverseBindFromReference();

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string  label = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( device.getBackend() ) );
        sw::FrameRenderer renderer;
        sw::FrameRenderer staticRenderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady() && staticRenderer.initialize( device.get() ) && staticRenderer.isReady();
        if ( bOk && device->getCapabilities()._bGpuMeshMorph == SW_FALSE )
            continue;

        // 스킨 + 모프 큐브와, (레스트 + 가중치 × 차이) 를 CPU 에서 굽혀 둔 정적 큐브 · 모프 없이 굽힌 정적 큐브.
        sw::shared_ptr<sw::Mesh> bindCube = sw::MeshUtil::createUnitCube();
        SW_ASSERT_NOT_NULL( bindCube.get() );
        const sw::float4x4             bend = sw::float4x4::createFromQuaternion( sw::quaternion::createFromAxisAngle( sw::float3{ 0.0f, 0.0f, 1.0f }, kBendAngle ) );
        sw::vector<sw::MeshSkinVertex> listSkin;
        sw::MeshMorphTarget            push{};
        push._name = sw::hashed_string( "push" );
        sw::MeshMorphTarget idle{};
        idle._name                                = sw::hashed_string( "idle" );
        sw::vector<sw::RHIVertex> listMorphedBent = bindCube->getVertices();
        sw::vector<sw::RHIVertex> listPlainBent   = bindCube->getVertices();
        for ( uint32 vertexIndex = 0; vertexIndex < listMorphedBent.size(); ++vertexIndex )
        {
            sw::MeshSkinVertex skin{};
            const bool         bTop = listMorphedBent[vertexIndex]._arrPosition[1] > 0.0f;
            skin._arrJoint[0]       = bTop ? 1u : 0u;
            listSkin.push_back( skin );
            if ( bTop == false )
                continue;
            push._listDelta.push_back( sw::MeshMorphDelta{ vertexIndex, pushDelta, sw::float3{} } );
            sw::RHIVertex* arrVertex[2] = { &listMorphedBent[vertexIndex], &listPlainBent[vertexIndex] };
            for ( uint32 variant = 0; variant < 2; ++variant )
            {
                sw::RHIVertex&   vertex   = *arrVertex[variant];
                const sw::float3 rest     = sw::float3{ vertex._arrPosition } + ( variant == 0 ? pushDelta * kMorphWeight : sw::float3{} );
                const sw::float3 position = sw::float3::transform( rest, bend );
                const sw::float3 normal   = sw::float3::transformVector( sw::float3{ vertex._arrNormal }, bend );
                vertex._arrPosition[0]    = position._x;
                vertex._arrPosition[1]    = position._y;
                vertex._arrPosition[2]    = position._z;
                vertex._arrNormal[0]      = normal._x;
                vertex._arrNormal[1]      = normal._y;
                vertex._arrNormal[2]      = normal._z;
            }
        }
        sw::shared_ptr<sw::Mesh> morphCube = sw::Mesh::create();
        morphCube->setVertices( bindCube->getVertices() );
        morphCube->setSkin( listSkin, 2 );
        morphCube->setMorphTargets( { push, idle } );
        sw::shared_ptr<sw::Mesh> cpuMorphed = sw::Mesh::create();
        cpuMorphed->setVertices( listMorphedBent );
        sw::shared_ptr<sw::Mesh> cpuPlain = sw::Mesh::create();
        cpuPlain->setVertices( listPlainBent );

        sw::Scene morphScene( "MorphCubeScene" );
        sw::Scene staticScene( "CpuMorphCubeScene" );
        bOk                                 = bOk && morphScene.ensureDefaultCameras() && staticScene.ensureDefaultCameras();
        sw::SkeletalMeshComponent* pSkinned = nullptr;
        sw::MeshComponent*         pStatic  = nullptr;
        if ( bOk )
        {
            sw::GameObject* pSkinnedObject = morphScene.getObjectManager()->createGameObject( sw::hashed_string( "Morph" ) );
            sw::GameObject* pStaticObject  = staticScene.getObjectManager()->createGameObject( sw::hashed_string( "Static" ) );
            bOk                            = pSkinnedObject != nullptr && pStaticObject != nullptr;
            if ( bOk )
            {
                pSkinned = pSkinnedObject->addComponent<sw::SkeletalMeshComponent>();
                pStatic  = pStaticObject->addComponent<sw::MeshComponent>();
                bOk      = pSkinned != nullptr && pStatic != nullptr;
            }
        }
        if ( bOk )
        {
            pSkinned->setSkeleton( skeleton );
            pSkinned->setMesh( morphCube );
            pSkinned->setBoundsRadius( 2.5f );
            pSkinned->setLocalPosition( sw::float3{ 0.0f, 1.0f, 0.0f } );
            pStatic->setLocalPosition( sw::float3{ 0.0f, 1.0f, 0.0f } );
        }
        SW_EXPECT_TRUE_MSG( bOk, ( label + ": 씬·렌더러 준비 실패" ).c_str() );
        if ( bOk == false )
            continue;
        SW_EXPECT_EQUAL( 2u, pSkinned->getMorphTargetCount() );

        // (A) 가중치 0 — 모프 없이 굽힌 큐브와 같다.
        MorphBendTask task( kBendAngle, 0.0f );
        pSkinned->addAnimationPhaseTask( &task );
        pStatic->setMesh( cpuPlain );
        const Snapshot plainSkinned = snapshot( renderer, *device, morphScene );
        const Snapshot plainStatic  = snapshot( staticRenderer, *device, staticScene );
        SW_EXPECT_TRUE_MSG( plainSkinned._bOk && plainStatic._bOk, ( label + ": 가중치 0 그림을 못 읽었다" ).c_str() );
        const uint32 plainDiff = countDifferentPixels( plainSkinned, plainStatic );
        SW_EXPECT_TRUE_MSG( plainDiff <= plainStatic._drawnCount / 50 + 8,
                            ( label + ": 가중치 0 인데 모프 큐브가 굽힌 큐브와 다르다 (달라진 픽셀 " + sw::to_string( plainDiff ) + ")" ).c_str() );

        // (B) 가중치 0.6 — CPU 에서 민 뒤 굽힌 큐브와 같고, 가중치 0 과는 다르다.
        task._weight = kMorphWeight;
        pSkinned->markPoseDirty();
        pStatic->setMesh( cpuMorphed );
        const Snapshot morphSkinned = snapshot( renderer, *device, morphScene );
        const Snapshot morphStatic  = snapshot( staticRenderer, *device, staticScene );
        SW_EXPECT_TRUE_MSG( morphSkinned._bOk && morphStatic._bOk, ( label + ": 모프 그림을 못 읽었다" ).c_str() );
        const uint32 weightDiff = countDifferentPixels( plainSkinned, morphSkinned );
        SW_EXPECT_TRUE_MSG( weightDiff > plainStatic._drawnCount / 10,
                            ( label + ": 가중치를 올렸는데 그림이 그대로다 (달라진 픽셀 " + sw::to_string( weightDiff ) + ") — 가중치가 GPU 에 닿지 않는다" ).c_str() );
        const uint32 cpuDiff = countDifferentPixels( morphSkinned, morphStatic );
        SW_EXPECT_TRUE_MSG( cpuDiff <= morphStatic._drawnCount / 50 + 8,
                            ( label + ": GPU 모프가 CPU 모프 · 스키닝과 다르다 (달라진 픽셀 " + sw::to_string( cpuDiff ) + " / 그려진 " +
                              sw::to_string( morphStatic._drawnCount ) + ")" )
                                .c_str() );
        pSkinned->removeAnimationPhaseTask( &task );
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the GPU morph target test" );
}

/**
 * @brief [RenderPassGpuTest] 정점 · 모프 풀은 메시 **내용**이 바뀌면 다시 만든다 — 포인터가 같아도
 * @details 메시 집합이 그대로인지를 포인터로만 보면, 메시가 지워진 자리에 새 메시가 생기거나(할당기는 같은 크기의 자리를 곧바로
 *          다시 준다) 같은 메시의 정점을 바꿀 때(`setVertices`) "같은 집합" 으로 보여 옛 정점을 그리고, 정점 수가 줄었으면 배치의 정점 구간이
 *          다른 메시의 정점을 읽는다. 주소 재사용은 시험에서 마음대로 일으킬 수 없어, 같은 원인의 다른 얼굴(같은 메시의 정점 교체)로 본다.
 */
SW_TEST_CASE( RenderPassGpuTest, MeshPoolsRebuildWhenMeshContentChanges )
{
    test::RHIBackendSweep sweep( { sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::DirectX11, sw::RHIBackend::OpenGL } );
    for ( test::RHITestDevice& device : sweep )
    {
        {
            sw::shared_ptr<sw::Mesh> mesh = sw::MeshUtil::createUnitCube();
            SW_ASSERT_NOT_NULL( mesh.get() );
            const uint32 cubeVertexCount = mesh->getVertexCount();
            SW_ASSERT_TRUE( cubeVertexCount > 3 );
            sw::vector<sw::Mesh*> listMesh;
            listMesh.push_back( mesh.get() );

            sw::GpuMeshVertexPool vertexPool;
            sw::GpuMeshMorphPool  morphPool;
            SW_EXPECT_TRUE( vertexPool.build( device.get(), listMesh ) );
            SW_EXPECT_FALSE( vertexPool.build( device.get(), listMesh ) ); // 그대로면 다시 만들지 않는다
            morphPool.build( device.get(), listMesh );
            SW_EXPECT_EQUAL( cubeVertexCount, vertexPool.getVertexCount() );
            SW_EXPECT_EQUAL( cubeVertexCount, morphPool.getVertexCount() );

            // 같은 메시, 다른 내용 — 삼각형 하나.
            sw::vector<sw::RHIVertex> listTriangle;
            for ( uint32 vertexIndex = 0; vertexIndex < 3; ++vertexIndex )
                listTriangle.push_back( mesh->getVertices()[vertexIndex] );
            mesh->setVertices( listTriangle );

            SW_EXPECT_TRUE_MSG( vertexPool.build( device.get(), listMesh ), "정점 풀이 바뀐 내용을 같은 집합으로 봤습니다" );
            morphPool.build( device.get(), listMesh );
            SW_EXPECT_EQUAL( 3u, vertexPool.getVertexCount() );
            SW_EXPECT_EQUAL( 3u, morphPool.getVertexCount() );

            vertexPool.release( device.get() );
            morphPool.release( device.get() );
        }
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the mesh pool content test" );
}

/**
 * @brief [RenderPassGpuTest] forgetRhi 뒤 다시 올려도 텍스처 서수가 **처음부터** 다시 센다
 * @details `forgetRhi` 와 `releaseRhi` 는 둘 다 "디바이스가 사라졌다" 는 통보라 남기는 상태가 같아야 한다 —
 *          `releaseRhi` 만 빌린 텍스처 목록을 비우면 forget 뒤에 `initRhi` 가 올 때 `resolveTextureAssets` 가
 *          목록에 **덧붙인다.**
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
    test::RHIBackendSweep sweep( { sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::DirectX11, sw::RHIBackend::OpenGL } );
    for ( test::RHITestDevice& device : sweep )
    {
        {
            // 텍스처가 붙은 실제 에셋이어야 한다 — 손으로 지은 XML 은 퍼뮤테이션을 빠뜨리기 쉽다.
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
        }
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the material texture ordinal test" );
}

/**
 * @brief [RenderPassGpuTest] 디바이스 없이 경로로 잡은 머티리얼(메시의 저장된 참조)을 `initializePending` 이 올린다 — 미리보기로 잡은 것은 올리지 않고,
 *        올릴 것이 남은 동안만 `hasPendingInitialize` 가 선다
 * @details 컴포넌트는 디바이스를 모른다(Object 는 Scene 을 include 하지 않는다). 메시가 `acquire( path, nullptr )` 로 잡고 `requestInitialize` 로
 *          표시하면, 엔진 루프가 패킷을 내기 전 · 씬 초기화가 디바이스로 올린다. 표시하지 않은 것(머티리얼 편집기 미리보기 — 편집 중인 내용을
 *          넣는다)은 파일 내용으로 덮이면 안 되므로 그대로 둔다.
 */
SW_TEST_CASE( RenderPassGpuTest, MaterialRequestedWithoutADeviceIsUploadedLater )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    constexpr const utf8* kPath = "engine/materials/benchtextured.material";
    sw::MaterialCache&    cache = sw::engine::getAssetManager().getMaterialManager();

    test::RHIBackendSweep sweep( { sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::DirectX11, sw::RHIBackend::OpenGL } );
    for ( test::RHITestDevice& device : sweep )
    {
        sw::Material* pMaterial = cache.acquire( kPath, nullptr );
        SW_ASSERT_NOT_NULL( pMaterial );
        // 표시 전에는 올리지 않는다(미리보기 길).
        cache.initializePending( device.get() );
        SW_EXPECT_FALSE( pMaterial->isRhiValid() );
        SW_EXPECT_FALSE( cache.hasPendingInitialize() );

        // 올릴 것이 있는 동안만 표시가 선다 — 엔진 루프는 이것이 true 인 프레임에만 렌더 스레드를 기다린다(병렬 기록 중에 bindless 표를 바꾸지 않게).
        cache.requestInitialize( kPath );
        SW_EXPECT_TRUE( cache.hasPendingInitialize() );
        cache.initializePending( nullptr ); // 디바이스가 없으면 표시를 그대로 둔다
        SW_EXPECT_FALSE( pMaterial->isRhiValid() );
        SW_EXPECT_TRUE( cache.hasPendingInitialize() );
        cache.initializePending( device.get() );
        SW_EXPECT_TRUE_MSG( pMaterial->isRhiValid(), device->getBackendName() );
        SW_EXPECT_FALSE( cache.hasPendingInitialize() );

        pMaterial->releaseRhi( device.get() );
        cache.release( kPath );
        SW_EXPECT_FALSE( cache.isCached( kPath ) );
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the deferred material upload test" );
}

/**
 * @brief [RenderPassGpuTest] 다시 올린 텍스처의 새 SRV 인덱스를 머티리얼과 배치가 받는다
 * @details 텍스처 핫 리로드(`TextureCache::reload`)는 같은 `Texture2D` 에 새 텍스처 · 새 SRV 인덱스를 올리고 옛 인덱스를 돌려준다. 머티리얼은
 *          resolve 때 받은 인덱스를 바이트(DX12 · Vulkan)와 슬롯 목록(DX11 · GL — 배치가 값으로 복사한다)에 들고 있으므로, 그대로 두면 **돌려준 자리**를
 *          읽는다 — 지연 해제가 끝나면 다른 텍스처가 그 자리를 받는다. 씬 빌드가 reload 세대를 보고 머티리얼이 새 인덱스를 받게 한다.
 *          DX11 · GL 은 인덱스를 바로 다시 쓰므로(지연 해제가 없다) 같은 인덱스가 돌아오는 일이 있다 — 그때는 결함이 보이지 않아 상태만 본다.
 */
SW_TEST_CASE( RenderPassGpuTest, ReloadedTextureIsReboundToMaterialsAndBatches )
{
    const sw::string      kTexturePath = "engine/textures/test/checker.dds";
    int32                 changedIndexCount{ 0 };
    test::RHIBackendSweep sweep( { sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::DirectX11, sw::RHIBackend::OpenGL } );
    for ( test::RHITestDevice& device : sweep )
    {
        {
            sw::shared_ptr<sw::Material> material = sw::Material::create();
            SW_ASSERT_TRUE( material->initialize( device.get(), "engine/materials/benchtextured.material" ) );
            SW_ASSERT_TRUE( material->getMaterialTextureSrvs().empty() == false );
            const sw::RHIDescriptorIndex before = material->getMaterialTextureSrvs()[0];

            sw::Scene                scene( "TextureReloadScene" );
            sw::shared_ptr<sw::Mesh> cube    = sw::MeshUtil::createUnitCube();
            sw::GameObject*          pObject = scene.getObjectManager()->createGameObject( sw::hashed_string( "TexturedCube" ) );
            SW_ASSERT_NOT_NULL( pObject );
            sw::MeshComponent* pMesh = pObject->addComponent<sw::MeshComponent>();
            SW_ASSERT_NOT_NULL( pMesh );
            pMesh->setMesh( cube );
            pMesh->setMaterial( material.get() );

            sw::GpuSceneBuilder builder;
            const sw::float3    camPos{ 0.0f, 0.0f, -5.0f };
            builder.buildFromScene( &scene, camPos );
            SW_ASSERT_EQUAL( size_t( 1 ), builder.getOpaqueBatches().size() );

            sw::TextureCache& textures = sw::engine::getAssetManager().getTextureManager();
            textures.reload( kTexturePath, device.get() );
            const sw::Texture2D* pTexture = textures.find( kTexturePath );
            SW_ASSERT_NOT_NULL( pTexture );
            const sw::RHIDescriptorIndex after = pTexture->getSrv();
            if ( after != before )
                ++changedIndexCount;

            // 다음 씬 빌드가 새 인덱스를 받게 한다 — 머티리얼(바이트 · 슬롯 목록)과 배치(슬롯 바인딩 백엔드가 값으로 든 SRV).
            builder.buildFromScene( &scene, camPos );
            SW_EXPECT_EQUAL( after, material->getMaterialTextureSrvs()[0] );
            if ( device->supportsNativeBindlessSampling() )
            {
                const sw::MaterialProperty* pAlbedo = material->findProperty( sw::hashed_string( "albedoMap" ) );
                SW_ASSERT_NOT_NULL( pAlbedo );
                uint32 packed{ 0 };
                SW_ASSERT_TRUE( pAlbedo->_offset + sizeof( packed ) <= material->getBuffer().size() );
                sw::Memory::copy( &packed, material->getBuffer().data() + pAlbedo->_offset, sizeof( packed ) );
                SW_EXPECT_EQUAL( after, packed );
            }
            // 배치는 SRV 를 값으로 복사해 든다(슬롯 바인딩 백엔드가 읽는다). 씬이 정지해 있어도 배치를 다시 만들어야 한다.
            SW_ASSERT_EQUAL( size_t( 1 ), builder.getOpaqueBatches().size() );
            SW_EXPECT_EQUAL( after, builder.getOpaqueBatches()[0]._arrMaterialTexSrv[0] );

            builder.clear();
        }
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the texture reload rebinding test" );
    // 지연 해제(DX12 · Vulkan)가 있으면 새 인덱스는 언제나 다르다 — 그것조차 없으면 이 시험은 아무것도 보지 못한 것이다.
    SW_EXPECT_TRUE_MSG( changedIndexCount > 0, "어느 백엔드에서도 다시 올린 텍스처의 인덱스가 바뀌지 않았습니다 — 시험이 결함을 볼 수 없습니다" );
}

/**
 * @brief [RenderPassGpuTest] 부모 레이아웃이 커지면 인스턴스 상수버퍼를 **다시 만든다**
 * @details `updateConstantBuffer( 버퍼, 데이터, 크기 )` 의 크기는 버퍼를 만들 때 준 크기를 넘으면 안 된다.
 *          네 백엔드 중 셋(DX12 · Vulkan · DX11)은 받은 크기를 **그대로 복사**하므로 넘기면 프레임 슬롯 밖까지
 *          쓴다. GL 만 `glBufferSubData` 가 막아 준다 — 한 백엔드에서만 조용히 안전하다.
 *
 *          그런데 부모 머티리얼의 상수버퍼는 **셰이더를 다시 구우면 커질 수 있다**(레이아웃이
 *          바뀐다). `MaterialInstance::updateRhi` 가 `_constant._buffer` 가 0 이 아니라고 그대로
 *          쓰고 새 크기로 갱신하면, 라이브 셰이더 편집 + 파라미터 변경이 겹칠 때 그 자리를 밟는다.
 *
 *          GPU 메모리로 넘치는 것이라 ASan 도 단언도 잡지 못한다. 대신 **버퍼를 다시 만들었는지**
 *          를 본다 — 다시 만들면 버퍼 핸들이 새 세대로 발급되므로 그것으로 가른다. bindless 디스크립터
 *          인덱스로 가르면 안 된다 — DX11·GL 은 인덱스를 즉시 회수해 다음 등록이 **같은 번호**를
 *          받는다(옳은 동작이다).
 */
SW_TEST_CASE( RenderPassGpuTest, InstanceConstantBufferIsRecreatedWhenLayoutGrows )
{
    test::RHIBackendSweep sweep( { sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::DirectX11, sw::RHIBackend::OpenGL } );
    for ( test::RHITestDevice& device : sweep )
    {
        {
            sw::shared_ptr<sw::Material> parent = sw::Material::create();
            SW_ASSERT_TRUE( parent->initialize( device.get(), "engine/materials/defaultmaterial.material" ) );

            sw::shared_ptr<sw::MaterialInstance> instance = sw::MaterialInstance::create( parent.get() );
            SW_ASSERT_TRUE( instance->updateRhi( device.get() ) );

            const sw::RHIBufferHandle firstBuffer = instance->getConstantBufferHandle();
            const size_t              firstSize   = instance->getBuffer().size();
            SW_ASSERT_TRUE( firstBuffer != 0 );
            SW_ASSERT_TRUE( firstSize > 0 );

            // 셰이더를 다시 쿠킹해 레이아웃이 커진 상황을 만든다 — 부모 상수버퍼가 256 을 넘게 한다.
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
            SW_EXPECT_TRUE_MSG( instance->getConstantBufferHandle() != firstBuffer,
                                "상수버퍼를 다시 만들지 않고 더 큰 크기로 갱신했습니다 — 슬롯 밖으로 씁니다" );
        }
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the instance constant buffer growth test" );
}

/**
 * @brief [RenderPassGpuTest] 다시 로드한 머티리얼은 셰이더 레이아웃으로 다시 맞춰지고, 인스턴스는 부모 바이트가 바뀌면 다시 복사한다
 * @details 셋이 함께 지켜져야 한다. (1) 다시 로드(XML 순서로 다시 쌓는다)하면 "이 백엔드는 맞췄다" 는 비트를 지워 다시 맞춘다.
 *          (2) GpuScene 은 인스턴스 CB 를 부모 레이아웃을 맞춘 **뒤에** 올린다 — 앞이면 첫 프레임 인스턴스가 XML 순서 바이트를 든다.
 *          (3) 인스턴스는 부모의 값 변경에도 부모 바이트를 다시 복사한다. 하나라도 어긋나면 화면에서는 "엉뚱한 색" 이다.
 *          XML 의 프로퍼티 순서를 셰이더(forwardlit 의 SwMaterialData: color, roughness, albedoMap)와 다르게 적어 차이가 바이트에 드러나게 한다.
 */
SW_TEST_CASE( RenderPassGpuTest, ReloadedMaterialIsLaidOutByTheShaderAgain )
{
    // roughness 를 먼저 — XML 순서로 쌓으면 roughness 가 0, color 가 16 에 간다. 셰이더는 color 가 0, roughness 가 16.
    const sw::string reorderedXml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<MaterialDesc formatVersion=\"0\" name=\"DefaultMaterial\" shaderPath=\"engine/shaders/forwardlit.hlsl\" blendMode=\"Opaque\">"
        "  <_properties>"
        "    <item name=\"roughness\" type=\"Range\" shaderType=\"Float\" defaultValue=\"0.5\" min=\"0.0\" max=\"1.0\"/>"
        "    <item name=\"color\" type=\"Color\" shaderType=\"Float4\" defaultValue=\"0 0 0 1\"/>"
        "  </_properties>"
        "</MaterialDesc>";
    const auto readFloat = []( const sw::vector<uint8>& bytes, size_t offset )
    {
        float32 value{ -1.0f };
        if ( offset + sizeof( value ) <= bytes.size() )
            sw::Memory::copy( &value, bytes.data() + offset, sizeof( value ) );
        return value;
    };

    test::RHIBackendSweep sweep( { sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::DirectX11, sw::RHIBackend::OpenGL } );
    for ( test::RHITestDevice& device : sweep )
    {

        {
            sw::shared_ptr<sw::Material> parent = sw::Material::create();
            SW_ASSERT_TRUE( parent->initialize( device.get(), "engine/materials/defaultmaterial.material" ) );
            SW_ASSERT_TRUE( parent->ensureShaderLayout( device.get() ) );
            SW_ASSERT_TRUE( parent->isShaderLayoutSynced( device.getBackend() ) );

            // 핫 리로드 — 같은 머티리얼을 프로퍼티 순서만 바꿔 다시 읽는다.
            SW_ASSERT_TRUE( parent->loadFromXml( reorderedXml ) );
            SW_EXPECT_FALSE( parent->isShaderLayoutSynced( device.getBackend() ) );

            // 인스턴스가 먼저 올라가도(GpuScene 의 순서) 셰이더 레이아웃의 바이트를 집는다.
            sw::shared_ptr<sw::MaterialInstance> instance = sw::MaterialInstance::create( parent.get() );
            SW_ASSERT_TRUE( instance->updateRhi( device.get() ) );
            SW_EXPECT_TRUE( parent->isShaderLayoutSynced( device.getBackend() ) );
            SW_EXPECT_NEAR_EQUAL( 0.5f, readFloat( instance->getBuffer(), 16 ), 1e-6f ); // roughness 는 셰이더의 16 자리
            SW_EXPECT_NEAR_EQUAL( 0.0f, readFloat( instance->getBuffer(), 0 ), 1e-6f );  // color.x

            // 부모 값만 바꾼다(인스턴스 오버라이드 없음) — 인스턴스가 따라와야 한다.
            SW_ASSERT_TRUE( parent->setParameter( device.get(), sw::hashed_string( "roughness" ), "0.125" ) );
            SW_ASSERT_TRUE( instance->updateRhi( device.get() ) );
            SW_EXPECT_NEAR_EQUAL( 0.125f, readFloat( instance->getBuffer(), 16 ), 1e-6f );

            // 리플렉션 캐시를 비우면(다시 쿠킹 · 라이브 셰이더 편집) 맞춘 레이아웃은 낡은 것이다 — 다시 맞춘다.
            sw::ShaderReflectionLibrary::clearCache();
            SW_EXPECT_FALSE( parent->isShaderLayoutSynced( device.getBackend() ) );
            SW_EXPECT_TRUE( parent->ensureShaderLayout( device.get() ) );
            SW_EXPECT_TRUE( parent->isShaderLayoutSynced( device.getBackend() ) );
        }
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the material reload layout test" );
}

/**
 * @brief [RenderPassGpuTest] 32비트에 담기지 않는 구조 버퍼는 **만들지 않는다**
 * @details `createStructuredBuffer( elementSize, elementCount )` 는 네 백엔드가 각자 곱한다.
 *          uint32 로 곱해 넘치면 조용히 작은 버퍼가 만들어지고, 셰이더는 원래 개수만큼 쓰므로 그 밖으로 나간다.
 *          DX12 는 `Width` 가 UINT64 라 64비트로 곱하면 되지만, 나머지 셋은 하위 API 가 전부 32비트 크기를 받아
 *          넓힐 수 없으므로 **거절**이 맞다.
 *          네 백엔드가 같은 답(0)을 내는지 여기서 못박는다.
 */
SW_TEST_CASE( RenderPassGpuTest, StructuredBufferRejectsSizeThatOverflows32Bit )
{
    test::RHIBackendSweep sweep( { sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::DirectX11, sw::RHIBackend::OpenGL } );
    for ( test::RHITestDevice& device : sweep )
    {
        {
            sw::IRHIResourceFactory* pResource = device->getResourceFactory();
            SW_ASSERT_TRUE( pResource != nullptr );

            // 64 x 100'000'000 = 6.4e9 — uint32 로 곱하면 약 2.1e9 로 접혀 "성공" 한다.
            sw::RHIBufferHandle overflowed = 0;
            {
                SW_TEST_DEFENSIVE_SCOPE( "createStructuredBuffer rejects a size that does not fit 32 bits" );
                overflowed = pResource->createStructuredBuffer( 64u, 100000000u );
            }

            // **DX12 는 거절하지 않아도 된다.** 그쪽 `D3D12_RESOURCE_DESC::Width` 는 UINT64 라 이 크기를
            // 실제로 표현할 수 있고, 만들지 말지는 드라이버가 정한다. 나머지 셋은 하위 API 가 전부
            // 32비트 크기를 받으므로 **담기지 않으면 만들지 않는 것**이 유일하게 맞는 답이다 —
            // 접힌 크기로 만들면 셰이더가 원래 개수만큼 쓰면서 버퍼 밖으로 나간다.
            if ( device.getBackend() != sw::RHIBackend::DirectX12 )
            {
                SW_EXPECT_TRUE_MSG( overflowed == 0,
                                    "32비트에 담기지 않는 크기로 구조 버퍼를 만들었습니다 — 접힌 크기입니다" );
            }
            if ( overflowed != 0 )
                pResource->destroyBuffer( overflowed );

            // 평범한 크기는 그대로 만들어진다 — "다 막는다" 로 굳지 않는다.
            const sw::RHIBufferHandle normal = pResource->createStructuredBuffer( 64u, 256u );
            SW_EXPECT_TRUE_MSG( normal != 0, "평범한 구조 버퍼를 만들지 못했습니다" );
            if ( normal != 0 )
                pResource->destroyBuffer( normal );
        }
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the structured buffer overflow test" );
}

/**
 * @brief [RenderPassGpuTest] 디퍼드의 Present 는 톤맵 결과를 그대로 화면에 낸다(톤맵을 두 번 걸지 않는다)
 * @details 디퍼드는 Tonemap 패스가 `TonemapColor` 에 톤맵을 끝내고, `_shaderPath` 없는 Present 는 기본 셰이더(`fullscreenblit.hlsl`)로 그것을
 *          화면에 옮긴다. 블릿이 Reinhard 를 한 번 더 걸면 [0,1] 값이 c/(c+1) 로 눌려 화면이 절반 밝기 아래로 내려간다. 화면(Present 캡처)과
 *          `TonemapColor` 를 픽셀로 맞춘다.
 */
SW_TEST_CASE( RenderPassGpuTest, DeferredPresentCopiesTheTonemapResult )
{
    uint32 comparedCount{ 0 };

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::Scene scene( "DeferredPresentScene" );
        bool      bOk = scene.ensureDefaultCameras();
        if ( bOk )
        {
            sw::GameObject* pLightObject = scene.getObjectManager()->createGameObject( sw::hashed_string( "KeyLight" ) );
            if ( pLightObject != nullptr )
            {
                if ( sw::DirectionalLightComponent* pLight = pLightObject->addComponent<sw::DirectionalLightComponent>(); pLight != nullptr )
                {
                    pLight->setIntensity( 6.0f );
                    pLight->setCastShadow( false );
                }
            }
        }

        sw::shared_ptr<sw::Mesh> mesh = bOk ? sw::MeshUtil::createUnitCube() : nullptr;
        if ( bOk && mesh != nullptr )
        {
            sw::GameObject* pObject = scene.getObjectManager()->createGameObject( sw::hashed_string( "Cube" ) );
            if ( sw::MeshComponent* pMesh = ( pObject != nullptr ) ? pObject->addComponent<sw::MeshComponent>() : nullptr; pMesh != nullptr )
            {
                pMesh->setMesh( mesh );
                pMesh->setLocalPosition( sw::float3{ 0.0f, 0.0f, 0.0f } );
            }
            else
            {
                bOk = false;
            }
        }

        sw::vector<uint8>     listPresent;
        sw::vector<uint8>     listTonemap;
        sw::RHITextureMipSpan layoutPresent{};
        sw::RHITextureMipSpan layoutTonemap{};
        sw::RHIFormat         tonemapFormat{};
        if ( bOk )
        {
            sw::FrameRenderer renderer;
            bOk = renderer.initialize( device.get(), "engine/pipeline/deferredpipeline.xml" ) && renderer.isReady();
            if ( bOk )
            {
                renderer.setPresentCaptureEnabled( true );
                // 첫 프레임에는 GpuScene 업로드가 아직이라 그릴 것이 없다 — 몇 장 돌린다.
                for ( uint32 frameIndex = 0; frameIndex < 3 && bOk; ++frameIndex )
                {
                    device->beginFrame( sw::float4{ 0.02f, 0.02f, 0.05f, 1.0f } );
                    bOk = renderer.execute( device.get(), &scene );
                    device->endFrame( false, false );
                    device->waitIdle();
                }
                bOk = bOk && renderer.readbackPresentCapture( listPresent, layoutPresent ) &&
                      renderer.readbackTransient( "TonemapColor", listTonemap, layoutTonemap, tonemapFormat );
            }
            renderer.shutdown();
        }

        const utf8* pName = device->getBackendName();
        if ( bOk == false || layoutPresent._width != layoutTonemap._width || layoutPresent._height != layoutTonemap._height ||
             tonemapFormat != sw::RHIFormat::R8G8B8A8_UNORM )
        {
            SW_LOG_WARNING( "DeferredPresentCopiesTheTonemapResult: %# 에서 비교할 두 장을 얻지 못했습니다.", pName );
            continue;
        }
        ++comparedCount;

        // 두 장 모두 RGBA8 이지만 줄 바이트(_rowBytes)이 다를 수 있다 — 줄 단위로 RGB 를 맞춘다.
        uint32 maxDelta{ 0 };
        uint32 brightCount{ 0 };
        for ( uint32 y = 0; y < layoutTonemap._height; ++y )
        {
            const uint8* pPresentRow = listPresent.data() + static_cast<size_t>( y ) * layoutPresent._rowBytes;
            const uint8* pTonemapRow = listTonemap.data() + static_cast<size_t>( y ) * layoutTonemap._rowBytes;
            for ( uint32 x = 0; x < layoutTonemap._width; ++x )
            {
                for ( uint32 channel = 0; channel < 3; ++channel )
                {
                    const int32  a     = pPresentRow[x * 4u + channel];
                    const int32  b     = pTonemapRow[x * 4u + channel];
                    const uint32 delta = static_cast<uint32>( a > b ? a - b : b - a );
                    if ( delta > maxDelta )
                        maxDelta = delta;
                    if ( b > 40 )
                        ++brightCount;
                }
            }
        }
        // 배경보다 밝은 픽셀(큐브)이 있어야 두 번째 Reinhard(c/(c+1))가 크게 드러난다 — 없으면 비교가 비었다.
        SW_EXPECT_TRUE_MSG( brightCount > 0, ( sw::string( pName ) + ": TonemapColor 에 그린 것이 없어 비교가 비었다" ).c_str() );
        SW_EXPECT_TRUE_MSG( maxDelta <= 1, ( sw::string( pName ) + ": Present 가 톤맵 결과와 다르다 (최대 차이 " + sw::to_string( maxDelta ) + ")" ).c_str() );
    }

    if ( comparedCount == 0 )
        SW_TEST_SKIP( "No RHI backend could run the deferred pipeline" );
}

/**
 * @brief [RenderPassGpuTest] 후처리를 한 패스로 합쳐도 같은 그림이 나온다
 * @details 합치기는 **성능 변경이지 룩 변경이 아니어야 한다.** `forwardpipeline.xml` 은 블룸·외곽선·
 *          톤맵을 Present 한 패스에서 끝내고, `forwardpipelinestaged.xml` 은 패스 셋으로
 *          나눈다. 같은 씬을 둘로 그려 픽셀을 맞춘다.
 *
 *          허용 오차가 1 인 이유: 나눈 판은 중간 타깃(`R8G8B8A8_UNORM`)에 두 번 쓰면서 그때마다
 *          8비트로 반올림하고, 합친 판은 마지막에 한 번만 반올림한다. 그 차이 말고는 없어야 한다.
 *
 *          **최종 화면을 읽으려면 Present 캡처가 필요하다** — 백버퍼는 핸들이 없어 읽을 수 없고,
 *          트랜지언트를 읽으면 Present 패스가 한 일(합친 판에서는 후처리 전부)이 빠진다.
 */
SW_TEST_CASE( RenderPassGpuTest, FusedPostChainMatchesStaged )
{
    uint32 comparedCount{ 0 };

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        // 씬은 한 번만 만든다 — 두 파이프라인이 **같은 입력**을 받아야 비교가 성립한다.
        sw::Scene scene( "FusedPostChainScene" );
        bool      bOk = scene.ensureDefaultCameras();

        // **밝아야 한다.** `postchain.hlsl` 은 블룸 뒤에 `saturate` 를 한다 — 나눈 판이 중간 타깃(UNORM8)에
        // 쓰면서 자르던 것을 재현하는 것이다. 블룸이 1 을 넘는 픽셀이 없으면 그 자름은 아무 일도 하지
        // 않아 빼는 변이가 통과한다. 주광이 채워지고 알베도가 0 이 아니어야 한다 — 흰 머티리얼 + 센 주광이면
        // 큐브 면이 날아가 블룸이 넘친다.
        if ( bOk )
        {
            sw::GameObject* pLightObject = scene.getObjectManager()->createGameObject( sw::hashed_string( "KeyLight" ) );
            if ( pLightObject != nullptr )
            {
                if ( sw::DirectionalLightComponent* pLight = pLightObject->addComponent<sw::DirectionalLightComponent>();
                     pLight != nullptr )
                {
                    pLight->setIntensity( 6.0f );
                    // 그림자 볼륨 기본값(2 유닛)이 큐브를 다 덮지 못해 바깥이 어두워진다 — 여기서는 밝기가 목적이다.
                    pLight->setCastShadow( false );
                }
            }
        }

        // 실제 에셋 + setParameter 로 간다 — 손으로 지은 머티리얼 XML 은 퍼뮤테이션을 빠뜨려 백엔드마다
        // 다르게 무너진다.
        sw::shared_ptr<sw::Material> material = sw::Material::create();
        if ( bOk )
            bOk = material->loadFromFile( "engine/materials/defaultmaterial.material" ) &&
                  material->setParameter( nullptr, sw::hashed_string( "color" ), "1.0 1.0 1.0 1.0" );

        constexpr uint32         kCubeCount = 3;
        sw::shared_ptr<sw::Mesh> arrMesh[kCubeCount];
        for ( uint32 index = 0; index < kCubeCount && bOk; ++index )
        {
            arrMesh[index] = sw::MeshUtil::createUnitCube();
            bOk            = arrMesh[index] != nullptr;
            if ( bOk == false )
                break;

            const sw::string objectName = sw::string( "Cube" ) + sw::to_string( index );
            sw::GameObject*  pObject    = scene.getObjectManager()->createGameObject(
                sw::hashed_string( objectName.c_str(), static_cast<uint32>( objectName.size() ) ) );
            bOk = pObject != nullptr;
            if ( bOk == false )
                break;

            sw::MeshComponent* pMesh = pObject->addComponent<sw::MeshComponent>();
            bOk                      = pMesh != nullptr;
            if ( bOk == false )
                break;
            pMesh->setMesh( arrMesh[index] );
            pMesh->setMaterial( material.get() );
            // 깊이 불연속이 있어야 외곽선이 생긴다 — 서로 겹치지 않게 벌려 둔다.
            pMesh->setLocalPosition( sw::float3{ ( static_cast<float32>( index ) - 1.0f ) * 1.5f, 1.0f, 0.0f } );
        }

        // 파이프라인 하나로 몇 프레임 돌리고 **화면에 나간 그림**을 읽어 온다.
        auto renderThrough = [&]( const utf8* pPipelinePath, sw::vector<uint8>& outByte, sw::RHITextureMipSpan& outLayout ) -> bool
        {
            sw::FrameRenderer renderer;
            if ( renderer.initialize( device.get(), pPipelinePath ) == false || renderer.isReady() == false )
                return false;
            renderer.setPresentCaptureEnabled( true );

            // 첫 프레임에는 GpuScene 업로드가 아직이라 그릴 것이 없다 — 몇 장 돌린다.
            constexpr uint32 kWarmupFrameCount = 3;
            for ( uint32 frameIndex = 0; frameIndex < kWarmupFrameCount; ++frameIndex )
            {
                device->beginFrame( sw::float4{ 0.02f, 0.02f, 0.05f, 1.0f } );
                if ( renderer.execute( device.get(), &scene ) == false )
                {
                    device->endFrame( false, false );
                    return false;
                }
                device->endFrame( false, false );
                device->waitIdle();
            }
            const bool bRead = renderer.readbackPresentCapture( outByte, outLayout );
            renderer.shutdown();
            return bRead;
        };

        sw::vector<uint8>     listFused;
        sw::vector<uint8>     listStaged;
        sw::RHITextureMipSpan layoutFused{};
        sw::RHITextureMipSpan layoutStaged{};
        const utf8*           pName = device->getBackendName();

        if ( bOk )
            bOk = renderThrough( "engine/pipeline/forwardpipeline.xml", listFused, layoutFused );
        if ( bOk )
            bOk = renderThrough( "engine/pipeline/forwardpipelinestaged.xml", listStaged, layoutStaged );

        if ( bOk && layoutFused._width == layoutStaged._width && layoutFused._height == layoutStaged._height )
        {
            ++comparedCount;
            const size_t compareCount = ( listFused.size() < listStaged.size() ) ? listFused.size() : listStaged.size();
            uint32       differCount{ 0 };
            uint32       maxDelta{ 0 };
            for ( size_t index = 0; index < compareCount; ++index )
            {
                const uint32 delta = ( listFused[index] > listStaged[index] )
                                       ? static_cast<uint32>( listFused[index] - listStaged[index] )
                                       : static_cast<uint32>( listStaged[index] - listFused[index] );
                if ( delta == 0 )
                    continue;
                ++differCount;
                if ( delta > maxDelta )
                    maxDelta = delta;
            }

            // 중간 타깃 반올림 말고는 달라질 것이 없다 — 한 칸을 넘으면 합친 셰이더가 다른 계산을 한 것이다.
            SW_EXPECT_TRUE_MSG( maxDelta <= 1,
                                ( sw::string( pName ) + ": 합친 후처리가 나눈 것과 다른 값을 낸다 (최대 차이 " +
                                  sw::to_string( maxDelta ) + ")" )
                                    .c_str() );
            // 반올림 차이는 드물게 흩어져야 한다. 절반이 1 씩 어긋나면 그건 반올림이 아니라 밝기 이동이다.
            const uint32 differPercent = compareCount > 0 ? static_cast<uint32>( differCount * 100u / compareCount ) : 0u;
            SW_EXPECT_TRUE_MSG( differPercent <= 5,
                                ( sw::string( pName ) + ": 합친 후처리가 너무 많은 픽셀에서 다르다 (" +
                                  sw::to_string( differPercent ) + "%)" )
                                    .c_str() );
        }
        else if ( bOk == false )
            SW_LOG_WARNING( "FusedPostChainMatchesStaged: %# 에서 파이프라인을 돌리지 못했습니다.", pName );

        // static Mesh 캐시가 죽은 디바이스를 붙잡지 않도록 디바이스 종료 전에 GPU 자원을 놓는다.
    }

    if ( comparedCount == 0 )
        SW_TEST_SKIP( "No RHI backend could run both pipelines" );
}

/**
 * @brief [RenderPassGpuTest] 첨부 이름만 바꾼 파이프라인이 같은 그림을 낸다 — 지오메트리 패스는 선언한 컬러 출력 · 뎁스에 그린다
 * @details 지오메트리 패스(ForwardOpaque · GBuffer · Transparent)가 컬러 타깃 이름을 코드에 박아(SceneColor …) 쓰면, 다른 이름을 쓰는
 *          파이프라인에서 없는 첨부를 열고, 없는 첨부의 핸들 0 은 백버퍼라 씬이 화면용 백버퍼로 가고 Present 는 아무도 그리지 않은
 *          타깃을 낸다. 뎁스 로드 연산도 바인딩한 뎁스로 정해야 한다(SceneDepth 의 클리어 기록이 아니라). `forwardpipeline.xml` 의 SceneColor ·
 *          SceneDepth 를 다른 이름으로 바꿔 같은 씬을 그리고 픽셀을 맞춘다.
 */
SW_TEST_CASE( RenderPassGpuTest, RenamedAttachmentsRenderTheSameImage )
{
    sw::string pipelineText;
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "engine/pipeline/forwardpipeline.xml", pipelineText ) );
    pipelineText = sw::StringUtil::replace( pipelineText, "SceneColor", "MainColor" );
    pipelineText = sw::StringUtil::replace( pipelineText, "SceneDepth", "MainDepth" );
    // 그림자 맵도 이름을 바꾸고 역할을 적는다 — ForwardOpaque 는 그림자 맵을 이름이 아니라 ShadowMap 역할의 입력으로 찾는다.
    pipelineText = sw::StringUtil::replace( pipelineText, "_name=\"ShadowMap\"", "_name=\"SunShadow\" _role=\"ShadowMap\"" );
    pipelineText = sw::StringUtil::replace( pipelineText, "<item>ShadowMap</item>", "<item>SunShadow</item>" );
    pipelineText = sw::StringUtil::replace( pipelineText, "_depthAttachment=\"ShadowMap\"", "_depthAttachment=\"SunShadow\"" );
    SW_ASSERT_TRUE( pipelineText.find( "_role=\"ShadowMap\"" ) != sw::string::npos );
    const sw::string renamedPath = test::makeTempPath( "renamedforwardpipeline.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( renamedPath, pipelineText ) );

    // 컬러 출력을 뺀 ForwardOpaque — 그릴 곳이 없다. 이름(SceneColor)으로 짐작해 열면 없는 첨부(핸들 0 = 백버퍼)다.
    sw::string       brokenText   = pipelineText;
    const sw::string colorItem    = "<item>MainColor</item>";
    const size_t     forwardBegin = brokenText.find( "_name=\"ForwardOpaque\"" );
    const size_t     outputBegin  = forwardBegin == sw::string::npos ? sw::string::npos : brokenText.find( "<_listOutput>", forwardBegin );
    const size_t     itemBegin    = outputBegin == sw::string::npos ? sw::string::npos : brokenText.find( colorItem, outputBegin );
    SW_ASSERT_TRUE( itemBegin != sw::string::npos );
    brokenText.erase( itemBegin, colorItem.size() );
    const sw::string brokenPath = test::makeTempPath( "nocolorforwardpipeline.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( brokenPath, brokenText ) );

    uint32                comparedCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        // 씬은 한 번만 만든다 — 두 파이프라인이 같은 입력을 받아야 비교가 성립한다.
        LitCubeScene          cube;
        sw::vector<uint8>     listReference;
        sw::vector<uint8>     listRenamed;
        sw::RHITextureMipSpan layoutReference{};
        sw::RHITextureMipSpan layoutRenamed{};
        const utf8*           pName = device->getBackendName();
        bool                  bOk   = cube.populate();
        if ( bOk )
            bOk = renderPresentCaptureOf( device.get(), cube._scene, "engine/pipeline/forwardpipeline.xml", listReference, layoutReference );
        if ( bOk )
            bOk = renderPresentCaptureOf( device.get(), cube._scene, renamedPath.c_str(), listRenamed, layoutRenamed );

        if ( bOk && layoutReference._width == layoutRenamed._width && layoutReference._height == layoutRenamed._height )
        {
            ++comparedCount;
            const CaptureDifference difference = compareCaptures( listReference, listRenamed );
            // 기준 그림에 큐브가 있어야 비교가 뜻이 있다 — 둘 다 배경뿐이면 이름을 무시해도 같게 나온다.
            SW_EXPECT_TRUE_MSG( difference._notBackgroundCount > 0, ( sw::string( pName ) + ": 기준 그림이 배경뿐입니다 — 큐브가 그려지지 않았습니다" ).c_str() );
            SW_EXPECT_TRUE_MSG( difference.isSameImage(), ( sw::string( pName ) + ": 첨부 이름만 바꿨는데 그림이 다르다 (" + difference.describe() + ")" ).c_str() );
        }
        else if ( bOk == false )
            SW_LOG_WARNING( "RenamedAttachmentsRenderTheSameImage: %# 에서 파이프라인을 돌리지 못했습니다.", pName );

        // 컬러 출력이 없는 패스는 그리지 않고 한 번 알린다 — 없는 첨부의 핸들 0(백버퍼)을 열지 않는다.
        if ( bOk )
        {
            SW_TEST_DEFENSIVE_SCOPE( "ForwardOpaque without a colour output" );
            test::ScopedLogCollector collector;
            sw::vector<uint8>        listBroken;
            sw::RHITextureMipSpan    layoutBroken{};
            (void)renderPresentCaptureOf( device.get(), cube._scene, brokenPath.c_str(), listBroken, layoutBroken );
            SW_EXPECT_TRUE_MSG( collector.countContaining( "컬러 타깃 'SceneColor'" ) == 1, ( sw::string( pName ) + ": " + collector.joined() ).c_str() );
        }
    }

    if ( comparedCount == 0 )
        SW_TEST_SKIP( "No RHI backend could run both pipelines" );
}

/**
 * @brief [RenderPassGpuTest] G버퍼 · 그림자 맵 · AO 첨부의 이름을 바꿔도 첨부가 역할을 선언하면 같은 그림이 나온다
 * @details 역할을 **이름으로만** 정하면(GBufferAlbedo · GBufferNormal · ShadowMap · AOColor) 이름을 바꿀 때 Lighting 의 입력이 모두
 *          SourceColor · SceneDepth 로 읽혀 계약이 깨지고 G버퍼가 걸리지 않는다. 언리얼 RDG · 유니티 RenderGraph 처럼 바인딩을 이름에서
 *          떼어, 첨부가 `_role` 로 자기 역할을 선언한다.
 *          `deferredpipeline.xml` 의 다섯 첨부 이름을 바꾸고 역할을 적어 같은 씬을 그려 픽셀을 맞춘다.
 */
SW_TEST_CASE( RenderPassGpuTest, RenamedGBufferAttachmentsRenderTheSameImage )
{
    sw::string pipelineText;
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "engine/pipeline/deferredpipeline.xml", pipelineText ) );
    struct AttachmentRename
    {
        const utf8* _pOldName;
        const utf8* _pNewName;
        const utf8* _pRole; ///< 비면 역할을 적지 않는다(깊이 포맷은 이름 없이도 SceneDepth 다)
    };
    constexpr AttachmentRename arrRename[] = {
        {"GBufferAlbedo", "MainAlbedo",    "GBufferAlbedo"},
        {"GBufferNormal", "MainNormal",    "GBufferNormal"},
        {    "ShadowMap",  "SunShadow",        "ShadowMap"},
        {      "AOColor",  "Occlusion", "AmbientOcclusion"},
        {   "SceneDepth",  "MainDepth",                 ""},
    };
    for ( const AttachmentRename& rename : arrRename )
    {
        const sw::string oldName{ rename._pOldName };
        const sw::string newName{ rename._pNewName };
        const sw::string roleAttribute = ( rename._pRole[0] != 0 ) ? sw::string( " _role=\"" ) + rename._pRole + "\"" : sw::string{};
        pipelineText                   = sw::StringUtil::replace( pipelineText, "_name=\"" + oldName + "\"", "_name=\"" + newName + "\"" + roleAttribute );
        pipelineText                   = sw::StringUtil::replace( pipelineText, "<item>" + oldName + "</item>", "<item>" + newName + "</item>" );
        pipelineText                   = sw::StringUtil::replace( pipelineText, "_depthAttachment=\"" + oldName + "\"", "_depthAttachment=\"" + newName + "\"" );
    }
    // G버퍼 출력의 순서도 뒤집는다(노멀 먼저) — 순서가 아니라 역할로 골라야 MRT 0 번에 알베도가 간다.
    {
        const size_t gbufferBegin = pipelineText.find( "_name=\"GBuffer\"" );
        const size_t outputBegin  = gbufferBegin == sw::string::npos ? sw::string::npos : pipelineText.find( "<_listOutput>", gbufferBegin );
        const size_t outputEnd    = outputBegin == sw::string::npos ? sw::string::npos : pipelineText.find( "</_listOutput>", outputBegin );
        SW_ASSERT_TRUE( outputEnd != sw::string::npos );
        sw::string block = pipelineText.substr( outputBegin, outputEnd - outputBegin );
        block            = sw::StringUtil::replace( block, "<item>MainAlbedo</item>", "<item>@ALBEDO@</item>" );
        block            = sw::StringUtil::replace( block, "<item>MainNormal</item>", "<item>MainAlbedo</item>" );
        block            = sw::StringUtil::replace( block, "<item>@ALBEDO@</item>", "<item>MainNormal</item>" );
        pipelineText     = pipelineText.substr( 0, outputBegin ) + block + pipelineText.substr( outputEnd );
        SW_ASSERT_TRUE( pipelineText.find( "<item>MainNormal</item>", outputBegin ) < pipelineText.find( "<item>MainAlbedo</item>", outputBegin ) );
    }
    // 정본 이름이 남아 있으면 시험이 아무것도 바꾸지 않은 것이다.
    SW_ASSERT_TRUE( pipelineText.find( "<item>GBufferAlbedo</item>" ) == sw::string::npos );
    SW_ASSERT_TRUE( pipelineText.find( "_role=\"GBufferAlbedo\"" ) != sw::string::npos );
    const sw::string renamedPath = test::makeTempPath( "renameddeferredpipeline.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( renamedPath, pipelineText ) );

    uint32                comparedCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        LitCubeScene          cube;
        sw::vector<uint8>     listReference;
        sw::vector<uint8>     listRenamed;
        sw::RHITextureMipSpan layoutReference{};
        sw::RHITextureMipSpan layoutRenamed{};
        const utf8*           pName = device->getBackendName();
        bool                  bOk   = cube.populate();
        if ( bOk )
            bOk = renderPresentCaptureOf( device.get(), cube._scene, "engine/pipeline/deferredpipeline.xml", listReference, layoutReference );
        if ( bOk )
            bOk = renderPresentCaptureOf( device.get(), cube._scene, renamedPath.c_str(), listRenamed, layoutRenamed );

        if ( bOk && layoutReference._width == layoutRenamed._width && layoutReference._height == layoutRenamed._height )
        {
            ++comparedCount;
            const CaptureDifference difference = compareCaptures( listReference, listRenamed );
            SW_EXPECT_TRUE_MSG( difference._notBackgroundCount > 0, ( sw::string( pName ) + ": 기준 그림이 배경뿐입니다 — 큐브가 그려지지 않았습니다" ).c_str() );
            SW_EXPECT_TRUE_MSG( difference.isSameImage(), ( sw::string( pName ) + ": G버퍼 첨부 이름만 바꿨는데 그림이 다르다 (" + difference.describe() + ")" ).c_str() );
        }
        else if ( bOk == false )
            SW_LOG_WARNING( "RenamedGBufferAttachmentsRenderTheSameImage: %# 에서 파이프라인을 돌리지 못했습니다.", pName );
    }

    if ( comparedCount == 0 )
        SW_TEST_SKIP( "No RHI backend could run both deferred pipelines" );
}

/**
 * @brief [RenderPassGpuTest] 머티리얼 인스턴스의 덮어쓰기가 네 백엔드 모두에서 GPU 에 닿는다 — 값 · 텍스처 에셋 · 텍스처 리로드 · 지우기
 * @details 셋이 함께 지켜져야 한다.
 *          (1) 네이티브 bindless(DX12 · Vulkan)는 불투명 배치를 머티리얼끼리 합치고 배치에 인스턴스를 싣지 않는다 — 인스턴스는 머티리얼 원소 표에만
 *              있으므로 누군가 그것을 `updateRhi` 해야 한다. 아니면 원소 업로드가 부모 바이트로 폴백해 **오버라이드가 통째로 사라진다**.
 *          (2) 텍스처 덮어쓰기를 날 디스크립터 인덱스로 들면 텍스처를 다시 올릴 때 돌려준 자리를 읽고, DX11 · GL 에서는 그 인덱스가 슬롯 서수로 읽혀
 *              엉뚱한 슬롯이 된다.
 *          (3) 언리얼 MIC · 유니티 MaterialPropertyBlock 은 텍스처 **자체**를 덮어쓴다. 여기서도 에셋 경로로 덮어쓰고, 값은 그때마다 지금 텍스처에서 읽는다.
 *          앱(EngineLoop)처럼 합치기를 백엔드에 맞춰 켜고 GpuSceneBuilder → 스냅샷 → GpuScene::upload 를 직접 돌려 인스턴스 바이트 · 배치 슬롯을 본다.
 */
SW_TEST_CASE( RenderPassGpuTest, InstanceOverridesReachTheGpuOnEveryBackend )
{
    const sw::string      kOverrideTexture = "engine/textures/perlin.dds";
    test::RHIBackendSweep sweep( { sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::DirectX11, sw::RHIBackend::OpenGL } );
    for ( test::RHITestDevice& device : sweep )
    {
        const bool       bNativeBindless = device->supportsNativeBindlessSampling();
        const sw::string label           = sw::string( device->getBackendName() ) + ": ";

        {
            // 부모는 albedoMap 에 checker.dds 를 빌린다(슬롯 0). 인스턴스는 roughness 와 albedoMap 을 덮어쓴다.
            sw::shared_ptr<sw::Material> parent = sw::Material::create();
            SW_ASSERT_TRUE( parent->initialize( device.get(), "engine/materials/benchtextured.material" ) );
            sw::shared_ptr<sw::MaterialInstance> instance = sw::MaterialInstance::create( parent.get() );
            instance->setScalarParameter( sw::hashed_string( "roughness" ), 0.125f );
            instance->setTextureParameter( sw::hashed_string( "albedoMap" ), kOverrideTexture );
            SW_EXPECT_TRUE( instance->getTextureParameter( sw::hashed_string( "albedoMap" ) ) == kOverrideTexture );

            sw::Scene                scene( "InstanceOverrideScene" );
            sw::shared_ptr<sw::Mesh> cube    = sw::MeshUtil::createUnitCube();
            sw::GameObject*          pObject = scene.getObjectManager()->createGameObject( sw::hashed_string( "InstancedCube" ) );
            SW_ASSERT_NOT_NULL( pObject );
            sw::MeshComponent* pMesh = pObject->addComponent<sw::MeshComponent>();
            SW_ASSERT_NOT_NULL( pMesh );
            pMesh->setMesh( cube );
            pMesh->setMaterial( parent.get() );
            pMesh->setMaterialInstance( instance );

            // 앱과 같은 구성: 네이티브 bindless 에서는 배치를 머티리얼끼리 합친다(EngineLoop · FrameRenderer).
            sw::GpuSceneBuilder builder;
            builder.setMergeBatchesAcrossMaterials( bNativeBindless );
            builder.buildFromScene( &scene, sw::float3{ 0.0f, 0.0f, -5.0f } );
            sw::GpuSceneSnapshot packet;
            builder.exportCpuSnapshot( packet );
            sw::GpuScene rtScene;
            rtScene.adoptCpuSnapshot( packet );
            SW_ASSERT_EQUAL( size_t( 1 ), rtScene.getOpaqueBatches().size() );

            auto readPackedUint = [&]( const utf8* pProperty ) -> uint32
            {
                uint32                      value{ 0xFFFFFFFFu };
                const sw::MaterialProperty* pProp = parent->findProperty( sw::hashed_string( pProperty ) );
                if ( pProp != nullptr && pProp->_offset + sizeof( value ) <= instance->getBuffer().size() )
                    sw::Memory::copy( &value, instance->getBuffer().data() + pProp->_offset, sizeof( value ) );
                return value;
            };
            sw::TextureCache& textures = sw::engine::getAssetManager().getTextureManager();
            // 텍스처가 셰이더에 닿았는가 — 네이티브는 인스턴스 바이트의 SRV 인덱스, 슬롯 바인딩 백엔드는 배치 슬롯(부모의 albedoMap 자리 0).
            auto expectTextureReachesShader = [&]( sw::RHIDescriptorIndex expectedSrv, const utf8* pWhen )
            {
                if ( bNativeBindless )
                    SW_EXPECT_TRUE_MSG( readPackedUint( "albedoMap" ) == expectedSrv, ( label + pWhen + " — 인스턴스 바이트의 albedoMap 이 덮어쓴 텍스처가 아닙니다" ).c_str() );
                else
                    SW_EXPECT_TRUE_MSG( rtScene.getOpaqueBatches()[0]._arrMaterialTexSrv[0] == expectedSrv,
                                        ( label + pWhen + " — 배치 슬롯 0 이 덮어쓴 텍스처가 아닙니다" ).c_str() );
            };

            SW_ASSERT_TRUE( rtScene.upload( device.get() ) );
            const sw::Texture2D* pOverride = textures.find( kOverrideTexture );
            SW_ASSERT_TRUE_MSG( pOverride != nullptr, ( label + "인스턴스가 덮어쓴 텍스처를 빌리지 않았습니다" ).c_str() );
            SW_ASSERT_TRUE_MSG( instance->getBuffer().empty() == false, ( label + "인스턴스 바이트가 만들어지지 않았습니다 — 합친 배치의 인스턴스를 아무도 올리지 않습니다" ).c_str() );
            float32                     roughness{ 0.0f };
            const sw::MaterialProperty* pRoughness = parent->findProperty( sw::hashed_string( "roughness" ) );
            SW_ASSERT_NOT_NULL( pRoughness );
            sw::Memory::copy( &roughness, instance->getBuffer().data() + pRoughness->_offset, sizeof( roughness ) );
            SW_EXPECT_NEAR_EQUAL( 0.125f, roughness, 1e-6f );
            expectTextureReachesShader( pOverride->getSrv(), "처음" );

            // 텍스처를 다시 올리면 같은 객체에 새 SRV 가 붙는다 — 다음 업로드가 새 것을 넣는다.
            textures.reload( kOverrideTexture, device.get() );
            SW_ASSERT_TRUE( rtScene.upload( device.get() ) );
            expectTextureReachesShader( pOverride->getSrv(), "다시 올린 뒤" );

            // 지우면 부모 텍스처로 돌아가고, 빌린 것을 돌려준다(아무도 안 쓰면 캐시에서 빠진다).
            instance->setTextureParameter( sw::hashed_string( "albedoMap" ), "" );
            SW_ASSERT_TRUE( rtScene.upload( device.get() ) );
            SW_EXPECT_TRUE_MSG( textures.find( kOverrideTexture ) == nullptr, ( label + "지운 덮어쓰기의 텍스처를 돌려주지 않았습니다" ).c_str() );
            expectTextureReachesShader( parent->getMaterialTextureSrvs()[0], "지운 뒤" );

            rtScene.releaseGpu( device.get() );
        }
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the instance override test" );
}

/**
 * @brief [RenderPassGpuTest] 깊이 프리패스를 넣어도 같은 그림이 나온다 — 깊이 첨부 이름을 바꿔도
 * @details 깊이 프리패스가 깨지는 방식: (1) 프리패스가 그림자와 같은 셰이더 변형으로 그리면 장면 깊이에 **광원 공간**의 깊이를 쓰고,
 *          (2) 깊이 비교가 Less 면 프리패스 뒤 같은 깊이를 다시 그리는 기본 패스가 모두 탈락한다(LessEqual 이어야 한다). 언리얼 EarlyZ · 유니티
 *          Depth Priming 처럼 `forwardprepasspipeline.xml` 을 두고, 프리패스 없는 포워드와 픽셀을 맞춘다. 깊이 첨부 이름만 바꾼 판도 맞춘다 —
 *          기본 패스가 프리패스의 깊이를 지우지 않고 이어 받아야(Load) 같은 그림이다.
 */
SW_TEST_CASE( RenderPassGpuTest, DepthPrepassRendersTheSameImage )
{
    sw::string prepassText;
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "engine/pipeline/forwardprepasspipeline.xml", prepassText ) );
    SW_ASSERT_TRUE( prepassText.find( "_type=\"DepthPrepass\"" ) != sw::string::npos );
    const sw::string renamedPath = test::makeTempPath( "renamedprepasspipeline.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( renamedPath, sw::StringUtil::replace( prepassText, "SceneDepth", "MainDepth" ) ) );

    uint32                comparedCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        LitCubeScene          cube;
        sw::vector<uint8>     listReference;
        sw::vector<uint8>     listPrepass;
        sw::vector<uint8>     listRenamed;
        sw::RHITextureMipSpan layoutReference{};
        sw::RHITextureMipSpan layoutPrepass{};
        sw::RHITextureMipSpan layoutRenamed{};
        const utf8*           pName = device->getBackendName();
        bool                  bOk   = cube.populate();
        if ( bOk )
            bOk = renderPresentCaptureOf( device.get(), cube._scene, "engine/pipeline/forwardpipeline.xml", listReference, layoutReference );
        if ( bOk )
            bOk = renderPresentCaptureOf( device.get(), cube._scene, "engine/pipeline/forwardprepasspipeline.xml", listPrepass, layoutPrepass );
        if ( bOk )
            bOk = renderPresentCaptureOf( device.get(), cube._scene, renamedPath.c_str(), listRenamed, layoutRenamed );

        if ( bOk && layoutReference._width == layoutPrepass._width && layoutReference._width == layoutRenamed._width )
        {
            ++comparedCount;
            const CaptureDifference prepassDifference = compareCaptures( listReference, listPrepass );
            const CaptureDifference renamedDifference = compareCaptures( listReference, listRenamed );
            SW_EXPECT_TRUE_MSG( prepassDifference._notBackgroundCount > 0, ( sw::string( pName ) + ": 기준 그림이 배경뿐입니다" ).c_str() );
            SW_EXPECT_TRUE_MSG( prepassDifference.isSameImage(),
                                ( sw::string( pName ) + ": 깊이 프리패스를 넣었더니 그림이 다르다 (" + prepassDifference.describe() + ")" ).c_str() );
            SW_EXPECT_TRUE_MSG( renamedDifference.isSameImage(),
                                ( sw::string( pName ) + ": 프리패스의 깊이 첨부 이름만 바꿨는데 그림이 다르다 (" + renamedDifference.describe() + ")" ).c_str() );
        }
        else if ( bOk == false )
            SW_LOG_WARNING( "DepthPrepassRendersTheSameImage: %# 에서 파이프라인을 돌리지 못했습니다.", pName );
    }

    if ( comparedCount == 0 )
        SW_TEST_SKIP( "No RHI backend could run the prepass pipelines" );
}

/**
 * @brief [RenderPassGpuTest] 그림자 패스가 바닥에 그림자를 드리운다 — 네 백엔드 모두
 * @details 깊이 전용 PSO(픽셀 스테이지 없음)의 드로우를 백엔드가 버리면(그리기 때 PS 까지 요구하면) 그림자 맵이 클리어 값뿐이라 그 백엔드
 *          화면에만 그림자가 없다. 두 판을 서로 맞추기만 하는 시험은 둘 다 그림자가 없어도 같아 이것을 못 잡는다.
 *          그래서 그림자 패스의 깊이 쓰기만 끈 판(그림자 맵이 비는 판)과 **달라야** 한다.
 */
SW_TEST_CASE( RenderPassGpuTest, ShadowPassCastsOnEveryBackend )
{
    expectShadowCastsOnEveryBackend( "ShadowPassCastsOnEveryBackend", 0.0f );
}

/**
 * @brief [RenderPassGpuTest] 볼륨이 커도 작은 물체가 그림자를 드리운다 — 네 백엔드 모두
 * @details 바이어스를 NDC 상수로 두면 볼륨 360 m 에서 월드 7.2 m 가 되어, 그보다 낮게 떠 있는 가리는 물체(이 1 m 큐브 · 벤치 · 레일)의
 *          그림자가 통째로 사라진다(ThemePark). 바이어스는 텍셀 수로 정하고 볼륨 크기로 환산한다(`DirectionalShadowProjection::computeShaderParams`).
 */
SW_TEST_CASE( RenderPassGpuTest, ShadowSurvivesLargeShadowVolume )
{
    expectShadowCastsOnEveryBackend( "ShadowSurvivesLargeShadowVolume", 180.0f );
}

/**
 * @brief [RenderPassGpuTest] 비균등 스케일 · 거울 스케일 아래에서도 G버퍼 노멀이 표면에 수직이다 — 네 백엔드, 머티리얼 셰이더(forwardlit)와 엔진 G버퍼 셰이더(gbuffer) 둘 다
 * @details 노멀을 월드 행렬로 옮기면(`mul( float4( n, 0 ), world )`) 균등 스케일 · 회전뿐일 때는 방향이 같아 드러나지 않지만, X 로 세 배
 *          늘린 부모 아래에서 Y 로 돈 쿼드는 노멀이 늘어난 축 쪽으로 50° 넘게 기운다 — 늘린 메시의 조명이 통째로 틀린다. 셰이더는 3x3 의
 *          여인수 행렬(외적 셋)로 옮기고 행렬식의 부호를 곱한다(binding.hlsli `swComputeWorldNormal`) — 부호가 없으면 거울 스케일(-3)에서 노멀이 뒤집힌다.
 *          기대값은 CPU 가 **다른 길**(월드 행렬의 역행렬 → 전치)로 구하고, 월드 행렬로 옮긴 식이 기대와 충분히 다른 배치인지도 먼저 확인한다 — 아니면 이 시험은
 *          눈이 멀어 있다. 노멀은 조명 이전의 값이라 G버퍼에서 직접 읽는다. 거울 배치는 컬 모드를 뒤집어 그리므로(`GpuMeshBatch::_bReverseCulling`)
 *          파이프라인의 후면 컬링을 그대로 두어도 카메라 쪽 면이 G버퍼에 남는다.
 */
SW_TEST_CASE( RenderPassGpuTest, NormalsStayPerpendicularUnderNonUniformScale )
{
    struct NormalCase
    {
        const utf8* _pName;
        sw::float3  _parentScale;
    };
    const NormalCase kArrCase[] = {
        {"stretched",  sw::float3{ 3.0f, 1.0f, 1.0f }},
        { "mirrored", sw::float3{ -3.0f, 1.0f, 1.0f }},
    };
    constexpr float32 kChildYaw = 0.7f;
    // 월드 행렬 식과 기대값 사이의 내적 상한(약 20°)과, 읽은 노멀과 기대값 사이의 내적 하한(약 5°). 쿼드의 노멀은 한 값이라 8 비트 양자화만 남는다.
    constexpr float32 kBlindDotLimit = 0.94f;
    constexpr float32 kMatchDotFloor = 0.996f;
    constexpr uint32  kMinDrawnCount = 1000;
    const sw::float3  localNormal{ 0.0f, 0.0f, 1.0f }; // MeshUtil::createRectMesh 의 노멀

    uint32                comparedCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string backendName( device->getBackendName() );

        sw::shared_ptr<sw::Material> material = sw::Material::create();
        SW_ASSERT_TRUE( material->loadFromFile( "engine/materials/defaultmaterial.material" ) );
        sw::shared_ptr<sw::Mesh> quad = sw::MeshUtil::createRectMesh();
        SW_ASSERT_NOT_NULL( quad.get() );

        for ( const NormalCase& normalCase : kArrCase )
        {
            // 머티리얼이 있으면 그 셰이더(forwardlit)의 G버퍼 퍼뮤테이션이, 없으면 엔진 G버퍼 PSO(gbuffer.hlsl)가 그린다.
            for ( const bool bWithMaterial : { true, false } )
            {
                const sw::string label = backendName + " " + normalCase._pName + ( bWithMaterial ? " forwardlit: " : " gbuffer: " );

                sw::Scene scene( "NonUniformScaleScene" );
                SW_ASSERT_TRUE( scene.ensureDefaultCameras() );
                sw::GameObjectManager* pObjectManager = scene.getObjectManager();
                sw::GameObject*        pParent        = pObjectManager->createGameObject( sw::hashed_string( "Stretch" ) );
                SW_ASSERT_NOT_NULL( pParent );
                sw::SceneComponent* pParentRoot = pParent->addComponent<sw::SceneComponent>();
                SW_ASSERT_NOT_NULL( pParentRoot );
                pParentRoot->setLocalScale( normalCase._parentScale );
                sw::GameObject* pChild = pObjectManager->createGameObject( sw::hashed_string( "Quad" ) );
                SW_ASSERT_NOT_NULL( pChild );
                sw::MeshComponent* pMesh = pChild->addComponent<sw::MeshComponent>();
                SW_ASSERT_NOT_NULL( pMesh );
                pMesh->setMesh( quad );
                if ( bWithMaterial )
                    pMesh->setMaterial( material.get() );
                pMesh->setLocalRotation( sw::float3{ 0.0f, kChildYaw, 0.0f } );
                SW_ASSERT_TRUE( pChild->attachToParent( pParent ) );
                pObjectManager->flushSceneTransforms();

                const sw::float4x4 world             = pMesh->getWorldMatrix();
                const sw::float3   expected          = sw::float3::transformVector( localNormal, world.invert().transpose() ).normalize();
                const sw::float3   worldMatrixNormal = sw::float3::transformVector( localNormal, world ).normalize();
                SW_ASSERT_TRUE_MSG( expected.dot( worldMatrixNormal ) < kBlindDotLimit,
                                    ( label + "배치가 시험이 되지 않는다 — 월드 행렬로 옮긴 노멀이 기대와 거의 같다 " + describeVector( worldMatrixNormal ) ).c_str() );

                sw::FrameRenderer renderer;
                const bool        bReady = renderer.initialize( device.get(), "engine/pipeline/deferredpipeline.xml" ) && renderer.isReady();
                SW_EXPECT_TRUE_MSG( bReady, ( label + "디퍼드 파이프라인을 만들지 못했다" ).c_str() );
                if ( bReady )
                {
                    // 첫 프레임에는 GpuScene 업로드가 아직이라 그릴 것이 없다 — 몇 장 돌린다.
                    constexpr uint32 kWarmupFrameCount = 4;
                    for ( uint32 frameIndex = 0; frameIndex < kWarmupFrameCount; ++frameIndex )
                    {
                        device->beginFrame( sw::float4{ 0.02f, 0.02f, 0.05f, 1.0f } );
                        SW_EXPECT_TRUE( renderer.execute( device.get(), &scene ) );
                        device->endFrame( false, false );
                        device->waitIdle();
                    }

                    uint32           drawnCount{ 0 };
                    const sw::float3 measured = readMeanGBufferNormal( renderer, drawnCount ).normalize();
                    SW_EXPECT_TRUE_MSG( drawnCount >= kMinDrawnCount,
                                        ( label + "쿼드가 G버퍼에 그려지지 않았다 (" + sw::to_string( drawnCount ) + " 픽셀)" ).c_str() );
                    if ( drawnCount >= kMinDrawnCount )
                    {
                        ++comparedCount;
                        SW_EXPECT_TRUE_MSG( measured.dot( expected ) > kMatchDotFloor,
                                            ( label + "G버퍼 노멀 " + describeVector( measured ) + " 이 표면에 수직이 아니다 — 기대 " + describeVector( expected ) +
                                              ", 월드 행렬로 옮기면 " + describeVector( worldMatrixNormal ) )
                                                .c_str() );
                    }
                }
            }
        }
    }

    if ( comparedCount == 0 )
        SW_TEST_SKIP( "No RHI backend could run the deferred pipeline for the normal test" );
}

/**
 * @brief [RenderPassGpuTest] 깊이 첨부를 거는 패스 앞에서 그 깊이를 읽던 SRV 가 떨어진다 — D3D11 해저드 경고 0, 그래프가 깊이 첨부를 안다 (4 백엔드 × 포워드 · 디퍼드)
 * @details 디퍼드의 투명 패스는 SceneDepth 를 쓰지 않고 깊이 테스트에만 DSV 로 건다. 그래프에 읽기로만 선언하면 레벨 프롤로그가 첨부 전이를
 *          내지 않고, D3D11 은 앞 패스(SSAO)가 t3 에 걸어 둔 SceneDepth SRV 를 그대로 둔 채 OMSetRenderTargets 를 받아 프레임마다
 *          "still bound on input" · "Forcing PS shader resource slot 3 to NULL" 을 낸다. 투명 셰이더(forwardlit)는 깊이를 샘플링하지 않아
 *          그림은 같다. 그래서 `FrameRenderer::bindPassCallbacks` 가 깊이 첨부를 그래프의 쓰기로 선언하고(첨부 전이) D3D11 의
 *          `prepareTextureForRenderTarget` 가 그 텍스처가 걸린 PS SRV 슬롯을 뗀다. 해저드 메시지는 디버그 레이어(SW_DEBUG)에서만 로그로 오므로
 *          배포 빌드에서는 그래프 선언만 본다. 디퍼드에서 SSAO 는 투명 패스보다 먼저 선언돼 불투명 깊이를 읽고 Shading 과 같은 레벨에 남는다.
 */
SW_TEST_CASE( RenderPassGpuTest, DepthAttachmentUnbindsItsShaderInputs )
{
    const utf8* const kArrPipeline[] = { "engine/pipeline/forwardpipeline.xml", "engine/pipeline/deferredpipeline.xml" };

    uint32                checkedCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string backendName( device->getBackendName() );

        for ( const utf8* pPipeline : kArrPipeline )
        {
            const sw::string label = backendName + " " + pPipeline + ": ";
            LitCubeScene     cube;
            SW_ASSERT_TRUE_MSG( cube.populate(), ( label + "씬을 만들지 못했다" ).c_str() );

            test::ScopedLogCollector logs;
            sw::FrameRenderer        renderer;
            const bool               bReady = renderer.initialize( device.get(), pPipeline ) && renderer.isReady();
            SW_EXPECT_TRUE_MSG( bReady, ( label + "파이프라인을 만들지 못했다" ).c_str() );
            if ( bReady )
            {
                // 투명 패스는 SceneDepth 를 깊이 첨부로 건다 — 그래프가 그것을 접근(쓰기)으로 알아야 프롤로그가 첨부 전이를 낸다.
                const sw::string graphText = renderer.getGraph().exportToMermaid();
                SW_EXPECT_TRUE_MSG( graphText.find( "Transparent --> SceneDepth" ) != sw::string::npos,
                                    ( label + "그래프가 투명 패스의 깊이 첨부를 모른다" + graphText ).c_str() );

                // 디퍼드의 SSAO 는 불투명 깊이를 읽는다 — 투명 패스의 첨부 쓰기 뒤로 밀리지 않고 Shading 과 같은 레벨에서 기록된다.
                const sw::vector<sw::vector<sw::hashed_string>>& listLevel = renderer.getGraph().getExecutionLevels();
                auto                                             findLevel = [&listLevel]( const utf8* pPass ) -> size_t
                {
                    const sw::hashed_string passName( pPass );
                    for ( size_t levelIndex = 0; levelIndex < listLevel.size(); ++levelIndex )
                    {
                        for ( const sw::hashed_string& name : listLevel[levelIndex] )
                        {
                            if ( name == passName )
                                return levelIndex;
                        }
                    }
                    return listLevel.size();
                };
                if ( findLevel( "SSAO" ) < listLevel.size() )
                {
                    SW_EXPECT_TRUE_MSG( findLevel( "SSAO" ) == findLevel( "Shading" ) && findLevel( "SSAO" ) < findLevel( "Transparent" ),
                                        ( label + "SSAO 가 Shading 과 같은 레벨 · 투명 패스 앞이 아니다\n" + renderer.getGraph().describeCompiledOrder() ).c_str() );
                }

                constexpr uint32 kFrameCount = 3;
                for ( uint32 frameIndex = 0; frameIndex < kFrameCount; ++frameIndex )
                {
                    device->beginFrame( sw::float4{ 0.02f, 0.02f, 0.05f, 1.0f } );
                    SW_EXPECT_TRUE( renderer.execute( device.get(), &cube._scene ) );
                    device->endFrame( false, false );
                    device->waitIdle();
                }
                ++checkedCount;
                const uint32 hazardCount = logs.countContaining( "still bound on input" ) + logs.countContaining( "Forcing PS shader resource" );
                SW_EXPECT_TRUE_MSG( hazardCount == 0,
                                    ( label + "깊이를 첨부로 걸 때 SRV 로도 걸려 있었다 (해저드 " + sw::to_string( hazardCount ) + " 줄)" + logs.joined() ).c_str() );
            }
        }
    }

    if ( checkedCount == 0 )
        SW_TEST_SKIP( "No RHI backend could run the pipelines for the depth hazard test" );
}

/**
 * @brief [RenderPassGpuTest] X 스케일 -1(거울 변환)인 큐브도 바깥 면이 보인다 — 거울이 아닌 큐브와 같은 그림이다 (4 백엔드 × 포워드 · 디퍼드)
 * @details 거울 변환은 삼각형 감김을 뒤집는다. 컬 모드를 그대로 두면 카메라 쪽 면이 후면으로 잘리고 먼 쪽 면이 안에서 보인다. 엔진은 행렬식이 음수인
 *          인스턴스를 따로 배치하고 컬 모드를 뒤집은 PSO 로 그린다(언리얼 `bReverseCulling`) — 그림자 패스도 같다.
 *          큐브는 원점 대칭이고 카메라는 x = 0 에 있어 X 거울로 바뀌는 ±X 면은 보이지 않는다. 그래서 바르게 그리면 두 그림이 같고, 컬링이
 *          뒤집히지 않으면 카메라 쪽 빨간 면(+Z) 대신 파란 면(-Z)의 안쪽이 보여 그림이 크게 다르다. 래스터 규칙 차이로 가장자리 몇 픽셀은
 *          다를 수 있어 "다른 칸 1% 이하" 로 본다.
 */
SW_TEST_CASE( RenderPassGpuTest, MirroredMeshShowsItsOuterFaces )
{
    const utf8* const kArrPipeline[] = { "engine/pipeline/forwardpipeline.xml", "engine/pipeline/deferredpipeline.xml" };
    // 다른 칸 비율의 상한(%). 바르게 그리면 0 이고, 컬링이 뒤집히지 않은 그림은 큐브 면적만큼(수 %) 다르다.
    constexpr uint32 kMaxDifferPercent = 1;

    uint32                comparedCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string backendName( device->getBackendName() );

        for ( const utf8* pPipeline : kArrPipeline )
        {
            const sw::string label = backendName + " " + pPipeline + ": ";
            LitCubeScene     cube;
            SW_ASSERT_TRUE_MSG( cube.populate(), ( label + "씬을 만들지 못했다" ).c_str() );
            SW_ASSERT_NOT_NULL( cube._pCube );

            sw::vector<uint8>     listPlain;
            sw::vector<uint8>     listMirrored;
            sw::RHITextureMipSpan layoutPlain{};
            sw::RHITextureMipSpan layoutMirrored{};
            bool                  bOk = renderPresentCaptureOf( device.get(), cube._scene, pPipeline, listPlain, layoutPlain );
            cube._pCube->setLocalScale( sw::float3{ -1.0f, 1.0f, 1.0f } );
            cube._scene.getObjectManager()->flushSceneTransforms();
            SW_EXPECT_TRUE_MSG( cube._pCube->getWorldMatrix().determinant() < 0.0f, ( label + "큐브가 거울 변환이 아니다" ).c_str() );
            if ( bOk )
                bOk = renderPresentCaptureOf( device.get(), cube._scene, pPipeline, listMirrored, layoutMirrored );
            SW_EXPECT_TRUE_MSG( bOk, ( label + "그리지 못했다" ).c_str() );

            if ( bOk && layoutPlain._width == layoutMirrored._width && layoutPlain._height == layoutMirrored._height )
            {
                ++comparedCount;
                const CaptureDifference difference = compareCaptures( listPlain, listMirrored );
                SW_EXPECT_TRUE_MSG( difference._notBackgroundCount > 0, ( label + "기준 그림이 배경뿐입니다" ).c_str() );
                SW_EXPECT_TRUE_MSG( difference._differCount * 100u <= difference._compareCount * kMaxDifferPercent,
                                    ( label + "거울 큐브의 그림이 거울이 아닌 큐브와 다르다 — 컬 모드가 뒤집히지 않아 안쪽 면이 보인다 (" +
                                      difference.describe() + ")" )
                                        .c_str() );
            }
        }
    }

    if ( comparedCount == 0 )
        SW_TEST_SKIP( "No RHI backend could run the mirrored-cube pipelines" );
}

/**
 * @brief [RenderPassGpuTest] 머티리얼 텍스처는 네 백엔드 모두 선형 필터 · 랩 주소로 읽힌다 — 체커 칸 경계의 섞인 픽셀 수가 같다
 * @details 계약은 네이티브 bindless(DX12 · Vulkan)의 `swSampleMaterialTexture` → `swSampleIndex` = SW_SAMPLER_LINEAR_WRAP 이다. 슬롯 결합
 *          샘플러를 쓰는 백엔드는 엔진이 머티리얼 슬롯(t5..t8)에 샘플러를 직접 건다 — 걸지 않으면 GL 은 기본 샘플러(NEAREST · CLAMP)로 칸 경계가
 *          계단이 되고, CLAMP 면 텍스처 가장자리에서 반대편 텍셀과 섞이지 않는다. 64x64 체커(8 텍셀 칸, 양 끝 열 색이 다르다)를
 *          화면에 크게 깔고 조명을 컴파일 아웃(Unlit)한 뒤, 쿼드 픽셀 가운데 두 색 사이(25~75%)인 픽셀을 센다 — 최근접은 0 이고, CLAMP 는
 *          가장자리의 랩 경계만큼 적다. 기준은 첫 네이티브 bindless 백엔드의 수다.
 */
SW_TEST_CASE( RenderPassGpuTest, MaterialTexturesAreSampledLinearWrap )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    // 쿼드 픽셀 가운데 섞인 픽셀의 하한(%)과, 기준 백엔드와의 섞인 픽셀 수 차이 상한(%). 최근접은 0 %, CLAMP 는 기준보다 10 % 남짓 적다.
    constexpr uint32 kMinMixedPercent = 3;
    constexpr uint32 kMaxDriftPercent = 5;
    constexpr uint32 kMinQuadPixel    = 20000;

    struct SampleCount
    {
        uint32 _quadCount{ 0 };
        uint32 _mixedCount{ 0 };
    };
    // SceneColor 에서 배경이 아닌 픽셀(쿼드)의 초록 채널 범위를 구하고, 그 가운데 절반 구간에 든 픽셀을 센다(파랑 칸 G 0x28 · 흰 칸 G 0xF0).
    auto countMixed = []( sw::FrameRenderer& renderer, SampleCount& outCount ) -> bool
    {
        test::RHITestImage image;
        if ( image.readTransient( renderer, "SceneColor" ) == false )
            return false;
        uint32 minGreen{ 255 };
        uint32 maxGreen{ 0 };
        for ( uint32 row = 0; row < image.getHeight(); ++row )
        {
            for ( uint32 col = 0; col < image.getWidth(); ++col )
            {
                const test::Rgba8 pixel = image.getPixel( col, row );
                if ( test::RHITestImage::isDefaultClearBackground( pixel ) )
                    continue;
                minGreen = sw::MathUtil::min<uint32>( minGreen, pixel._g );
                maxGreen = sw::MathUtil::max<uint32>( maxGreen, pixel._g );
            }
        }
        const uint32 range = ( maxGreen > minGreen ) ? maxGreen - minGreen : 0u;
        const uint32 low   = minGreen + range / 4u;
        const uint32 high  = maxGreen - range / 4u;
        for ( uint32 row = 0; row < image.getHeight(); ++row )
        {
            for ( uint32 col = 0; col < image.getWidth(); ++col )
            {
                const test::Rgba8 pixel = image.getPixel( col, row );
                if ( test::RHITestImage::isDefaultClearBackground( pixel ) )
                    continue;
                ++outCount._quadCount;
                if ( low < pixel._g && pixel._g < high )
                    ++outCount._mixedCount;
            }
        }
        return true;
    };

    uint32     comparedCount{ 0 };
    uint32     referenceMixed{ 0 };
    sw::string referenceName;
    // 기준(네이티브 bindless)을 먼저 잰다. 목록 순서(DX11 이 먼저)를 그대로 쓰면 비교할 기준이 없다.
    sw::vector<sw::RHIBackend> listBackend;
    for ( sw::RHIBackend backend : test::kArrAllRhiBackend )
    {
        if ( backend == sw::RHIBackend::DirectX12 || backend == sw::RHIBackend::Vulkan )
            listBackend.insert( listBackend.begin(), backend );
        else
            listBackend.push_back( backend );
    }
    test::RHIBackendSweep sweep( listBackend );
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string label = sw::string( device->getBackendName() ) + ": ";

        sw::shared_ptr<sw::Material> material = sw::Material::create();
        SW_ASSERT_TRUE( material->initialize( device.get(), "engine/materials/benchtextured.material" ) );
        SW_ASSERT_TRUE( material->getMaterialTextureSrvs().empty() == false );
        sw::shared_ptr<sw::Mesh> quad = sw::MeshUtil::createRectMesh();
        SW_ASSERT_NOT_NULL( quad.get() );

        SampleCount count{};
        bool        bOk{ false };
        {
            sw::Scene scene( "MaterialSamplerScene" );
            SW_ASSERT_TRUE( scene.ensureDefaultCameras() );
            sw::GameObject*    pObject = scene.getObjectManager()->createGameObject( sw::hashed_string( "CheckerQuad" ) );
            sw::MeshComponent* pMesh   = ( pObject != nullptr ) ? pObject->addComponent<sw::MeshComponent>() : nullptr;
            SW_ASSERT_NOT_NULL( pMesh );
            pMesh->setMesh( quad );
            pMesh->setMaterial( material.get() );
            pMesh->setLocalScale( sw::float3{ 2.0f, 2.0f, 1.0f } );
            scene.getObjectManager()->flushSceneTransforms();

            sw::FrameRenderer renderer;
            bOk = renderer.initialize( device.get(), "engine/pipeline/forwardpipeline.xml" ) && renderer.isReady();
            SW_EXPECT_TRUE_MSG( bOk, ( label + "포워드 파이프라인을 만들지 못했다" ).c_str() );
            if ( bOk )
            {
                renderer.setViewMode( sw::RenderViewMode::Unlit );
                sw::engine::getAssetManager().getMaterialManager().initializePending( device.get() );
                constexpr uint32 kWarmupFrameCount = 4;
                for ( uint32 frameIndex = 0; frameIndex < kWarmupFrameCount && bOk; ++frameIndex )
                {
                    bOk = renderSceneFrame( renderer, device.get(), scene, sw::float4{ 0.02f, 0.02f, 0.05f, 1.0f } );
                }
                bOk = bOk && countMixed( renderer, count );
                SW_EXPECT_TRUE_MSG( bOk, ( label + "그리거나 되읽지 못했다" ).c_str() );
            }
        }
        quad->releaseRhi( device.get() );
        material->releaseRhi( device.get() );
        if ( bOk == false )
            continue;

        SW_LOG_INFO( "%#checker quad %# px, mixed %# px", label, count._quadCount, count._mixedCount );
        SW_EXPECT_TRUE_MSG( count._quadCount >= kMinQuadPixel, ( label + "쿼드가 작게 그려졌다 (" + sw::to_string( count._quadCount ) + " 픽셀)" ).c_str() );
        if ( count._quadCount < kMinQuadPixel )
            continue;
        ++comparedCount;
        SW_EXPECT_TRUE_MSG( count._mixedCount * 100u >= count._quadCount * kMinMixedPercent,
                            ( label + "체커 칸 경계가 섞이지 않는다 — 머티리얼 텍스처가 선형 필터로 읽히지 않는다 (섞인 픽셀 " + sw::to_string( count._mixedCount ) +
                              " / " + sw::to_string( count._quadCount ) + ")" )
                                .c_str() );
        if ( referenceName.empty() )
        {
            referenceName  = label;
            referenceMixed = count._mixedCount;
            continue;
        }
        const uint32 drift = ( count._mixedCount > referenceMixed ) ? count._mixedCount - referenceMixed : referenceMixed - count._mixedCount;
        SW_EXPECT_TRUE_MSG( drift * 100u <= referenceMixed * kMaxDriftPercent,
                            ( label + "섞인 픽셀 " + sw::to_string( count._mixedCount ) + " 이 " + referenceName + sw::to_string( referenceMixed ) +
                              " 과 다르다 — 머티리얼 샘플러의 필터 · 주소 모드가 계약(SW_SAMPLER_LINEAR_WRAP)과 다르다" )
                                .c_str() );
    }

    if ( comparedCount == 0 )
        SW_TEST_SKIP( "No RHI backend could draw the checker material" );
}

/**
 * @brief [RenderPassGpuTest] 엔진 텍스처 슬롯(풀스크린 입력)은 네 백엔드 모두 같은 샘플러(선형 · 클램프)로 읽힌다 — 블룸 결과가 기준 백엔드와 같다
 * @details 계약은 `shaderslot::kEngineTextureSampler`(SW_ENGINE_TEXTURE_SAMPLER) 하나다. 네이티브 bindless(DX12 · Vulkan)는 `swSampleIndex` 가 그
 *          샘플러를 고르고, 슬롯 결합 샘플러를 쓰는 DX11 · GL 은 엔진이 t0..t3 에 같은 샘플러를 건다. 블룸은 `swSampleSource` 를 반 텍셀 비낀 두
 *          탭으로 읽어 텍셀 넷을 평균하므로 필터(최근접이면 한 텍셀) · 주소 모드(화면 가장자리에서 랩이면 반대편 텍셀)가 그대로 픽셀에 드러난다.
 *          밝은 큐브 셋 + 화면 왼쪽 가장자리를 넘는 긴 막대(왼쪽 끝만 밝다 — 랩과 클램프가 갈린다)를 나눈 후처리 파이프라인으로 그려,
 *          `SceneColor` 가 기준과 같은 백엔드에서 `BloomColor` 도 같은지 픽셀로 본다.
 */
SW_TEST_CASE( RenderPassGpuTest, EngineTextureSlotsAreSampledLinearClamp )
{
    // 채널당 허용 차이와, 기준과 달라도 되는 픽셀의 상한(그려진 픽셀 대비 ‰). 반올림 말고는 달라질 것이 없다.
    constexpr int32  kChannelTolerance  = 2;
    constexpr uint32 kMaxDifferPermille = 2;

    struct Capture
    {
        test::RHITestImage _sceneColor;
        test::RHITestImage _bloomColor;
    };
    // 두 이미지에서 채널 차이가 허용을 넘는 픽셀 수.
    auto countDiffer = []( const test::RHITestImage& lhs, const test::RHITestImage& rhs ) -> uint32
    {
        uint32 differCount{ 0 };
        for ( uint32 row = 0; row < lhs.getHeight(); ++row )
        {
            for ( uint32 col = 0; col < lhs.getWidth(); ++col )
            {
                const test::Rgba8 left       = lhs.getPixel( col, row );
                const test::Rgba8 right      = rhs.getPixel( col, row );
                const int32       redDelta   = static_cast<int32>( left._r ) - static_cast<int32>( right._r );
                const int32       greenDelta = static_cast<int32>( left._g ) - static_cast<int32>( right._g );
                const int32       blueDelta  = static_cast<int32>( left._b ) - static_cast<int32>( right._b );
                const bool        bSame      = -kChannelTolerance <= redDelta && redDelta <= kChannelTolerance && -kChannelTolerance <= greenDelta &&
                                   greenDelta <= kChannelTolerance && -kChannelTolerance <= blueDelta && blueDelta <= kChannelTolerance;
                if ( bSame == false )
                    ++differCount;
            }
        }
        return differCount;
    };

    // 기준(네이티브 bindless)을 먼저 그린다.
    sw::vector<sw::RHIBackend> listBackend;
    for ( sw::RHIBackend backend : test::kArrAllRhiBackend )
    {
        if ( backend == sw::RHIBackend::DirectX12 || backend == sw::RHIBackend::Vulkan )
            listBackend.insert( listBackend.begin(), backend );
        else
            listBackend.push_back( backend );
    }

    Capture               reference;
    sw::string            referenceName;
    uint32                comparedCount{ 0 };
    test::RHIBackendSweep sweep( listBackend );
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string label = sw::string( device->getBackendName() ) + ": ";

        sw::Scene scene( "EngineSamplerScene" );
        bool      bOk = scene.ensureDefaultCameras();
        if ( bOk )
        {
            // 밝은 면이 있어야 블룸이 퍼진다. 그림자는 끈다 — 에뮬 백엔드는 그림자 비교를 다르게 한다(이 시험의 대상이 아니다).
            sw::GameObject*                pLightObject = scene.getObjectManager()->createGameObject( sw::hashed_string( "KeyLight" ) );
            sw::DirectionalLightComponent* pLight       = ( pLightObject != nullptr ) ? pLightObject->addComponent<sw::DirectionalLightComponent>() : nullptr;
            bOk                                         = pLight != nullptr;
            if ( bOk )
            {
                pLight->setIntensity( 6.0f );
                pLight->setCastShadow( false );
            }
        }
        sw::shared_ptr<sw::Material> material = sw::Material::create();
        if ( bOk )
            bOk = material->loadFromFile( "engine/materials/defaultmaterial.material" ) &&
                  material->setParameter( nullptr, sw::hashed_string( "color" ), "1.0 1.0 1.0 1.0" );

        // 큐브 셋(화면 안) + 왼쪽 가장자리를 넘어가는 긴 막대 하나(오른쪽 가장자리는 배경이다).
        constexpr uint32 kBoxCount = 4;
        const sw::float3 arrPosition[kBoxCount]{
            sw::float3{ -1.5f,  1.0f, 0.0f},
            sw::float3{  0.0f,  1.0f, 0.0f},
            sw::float3{  1.5f,  1.0f, 0.0f},
            sw::float3{-20.0f, -1.0f, 0.0f}
        };
        const sw::float3 arrScale[kBoxCount]{
            sw::float3{ 1.0f, 1.0f, 1.0f},
            sw::float3{ 1.0f, 1.0f, 1.0f},
            sw::float3{ 1.0f, 1.0f, 1.0f},
            sw::float3{36.0f, 0.5f, 0.5f}
        };
        sw::shared_ptr<sw::Mesh> arrMesh[kBoxCount];
        for ( uint32 index = 0; index < kBoxCount && bOk; ++index )
        {
            arrMesh[index]             = sw::MeshUtil::createUnitCube();
            const sw::string   name    = sw::string( "Box" ) + sw::to_string( index );
            sw::GameObject*    pObject = ( arrMesh[index] != nullptr )
                                           ? scene.getObjectManager()->createGameObject( sw::hashed_string( name.c_str(), static_cast<uint32>( name.size() ) ) )
                                           : nullptr;
            sw::MeshComponent* pMesh   = ( pObject != nullptr ) ? pObject->addComponent<sw::MeshComponent>() : nullptr;
            bOk                        = pMesh != nullptr;
            if ( bOk == false )
                break;
            pMesh->setMesh( arrMesh[index] );
            pMesh->setMaterial( material.get() );
            pMesh->setLocalPosition( arrPosition[index] );
            pMesh->setLocalScale( arrScale[index] );
        }
        scene.getObjectManager()->flushSceneTransforms();

        Capture capture;
        if ( bOk )
        {
            sw::FrameRenderer renderer;
            bOk = renderer.initialize( device.get(), "engine/pipeline/forwardpipelinestaged.xml" ) && renderer.isReady();
            SW_EXPECT_TRUE_MSG( bOk, ( label + "나눈 후처리 파이프라인을 만들지 못했다" ).c_str() );
            constexpr uint32 kWarmupFrameCount = 3;
            for ( uint32 frameIndex = 0; frameIndex < kWarmupFrameCount && bOk; ++frameIndex )
            {
                bOk = renderSceneFrame( renderer, device.get(), scene, sw::float4{ 0.02f, 0.02f, 0.05f, 1.0f } );
            }
            bOk = bOk && capture._sceneColor.readTransient( renderer, "SceneColor" ) && capture._bloomColor.readTransient( renderer, "BloomColor" );
            SW_EXPECT_TRUE_MSG( bOk, ( label + "그리거나 되읽지 못했다" ).c_str() );
        }
        material->releaseRhi( device.get() );
        if ( bOk == false )
            continue;

        uint32 drawnCount{ 0 };
        uint32 leftEdgeDrawnCount{ 0 };
        for ( uint32 row = 0; row < capture._sceneColor.getHeight(); ++row )
        {
            for ( uint32 col = 0; col < capture._sceneColor.getWidth(); ++col )
            {
                if ( test::RHITestImage::isDefaultClearBackground( capture._sceneColor.getPixel( col, row ) ) )
                    continue;
                ++drawnCount;
                if ( col == 0 )
                    ++leftEdgeDrawnCount;
            }
        }
        SW_EXPECT_TRUE_MSG( leftEdgeDrawnCount > 0, ( label + "막대가 화면 왼쪽 가장자리에 닿지 않는다 — 랩 · 클램프 차이를 재지 못한다" ).c_str() );
        if ( referenceName.empty() )
        {
            referenceName = label;
            reference     = capture;
            continue;
        }
        if ( capture._sceneColor.getWidth() != reference._sceneColor.getWidth() || capture._sceneColor.getHeight() != reference._sceneColor.getHeight() )
            continue;
        ++comparedCount;

        const uint32 sceneDiffer = countDiffer( capture._sceneColor, reference._sceneColor );
        const uint32 bloomDiffer = countDiffer( capture._bloomColor, reference._bloomColor );
        SW_LOG_INFO( "%#drawn %# px (left edge %#), SceneColor differs %# px, BloomColor differs %# px from %#", label, drawnCount, leftEdgeDrawnCount,
                     sceneDiffer, bloomDiffer, referenceName );
        // 입력이 같아야 블룸 비교가 샘플러를 잰다.
        SW_EXPECT_TRUE_MSG( sceneDiffer * 1000u <= drawnCount * kMaxDifferPermille,
                            ( label + "SceneColor 가 " + referenceName + "과 " + sw::to_string( sceneDiffer ) + " 픽셀 다르다 — 블룸 비교의 전제가 깨졌다" ).c_str() );
        SW_EXPECT_TRUE_MSG( bloomDiffer * 1000u <= drawnCount * kMaxDifferPermille,
                            ( label + "BloomColor 가 " + referenceName + "과 " + sw::to_string( bloomDiffer ) +
                              " 픽셀 다르다 — 엔진 텍스처 슬롯의 샘플러(필터 · 주소)가 계약(SW_ENGINE_TEXTURE_SAMPLER)과 다르다" )
                                .c_str() );
    }

    if ( comparedCount == 0 )
        SW_TEST_SKIP( "Fewer than two RHI backends could run the staged post pipeline" );
}

/**
 * @brief [RenderPassGpuTest] 한 배치로 그린 스프라이트 여섯이 인스턴스마다 다른 아틀라스 프레임과 색을 보이고, 2D 카메라에서 텍스처가 뒤집히지 않는다 (4 백엔드)
 * @details 네 칸 텍스처(왼위 빨강 · 오위 초록 · 왼아래 파랑 · 오아래 흰색)를 나눠 쓰는 스프라이트들이 각자 다른 칸(UV 사각형)과 색을 고른다.
 *          둘은 머티리얼 인스턴스가 아니라 GPU 인스턴스(`instancedata.hlsli` 의 uvStart · uvEnd · tint)에 실리므로 모두 **반투명 배치 하나**다.
 *          인스턴스 칸을 셰이더가 읽지 않으면 모두 텍스처 전체를 보여 가운데가 네 칸의 경계(섞인 색)이고, 색을 곱하지 않으면 자홍이 흰색 ·
 *          반투명 파랑이 불투명 파랑이 된다. 마지막 하나는 텍스처 전체를 보이는데, 2D 카메라(+Z 를 봄, 화면 오른쪽 = +X)에서 왼쪽 위가 빨강 ·
 *          오른쪽 위가 초록이어야 한다 — 감김 방향이 틀리면 이 카메라에서 후면 컬링으로 사라지고, 보이는 쪽에서는 좌우가 뒤집힌다.
 *          가운데 줄을 훑어 그려진 구간 여섯을 찾고 구간 가운데의 평균 색을 본다(톤매핑을 지나므로 우세 채널로 본다).
 */
SW_TEST_CASE( RenderPassGpuTest, SpriteFramesAndTintsArePerInstance )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    constexpr const utf8* kQuadrantTexture = "engine/textures/test/quadrants.dds";
    struct SpriteCase
    {
        const utf8* _pName;
        float32     _x;
        sw::float4  _uvRect;
        sw::float4  _tint;
    };
    const SpriteCase kArrCase[] = {
        {     "Red", -1.25f, sw::float4{ 0.0f, 0.0f, 0.5f, 0.5f }, sw::float4{ 1.0f, 1.0f, 1.0f, 1.0f }},
        {   "Green", -0.75f, sw::float4{ 0.5f, 0.0f, 0.5f, 0.5f }, sw::float4{ 1.0f, 1.0f, 1.0f, 1.0f }},
        { "Magenta", -0.25f, sw::float4{ 0.5f, 0.5f, 0.5f, 0.5f }, sw::float4{ 1.0f, 0.0f, 1.0f, 1.0f }},
        {    "Blue",  0.25f, sw::float4{ 0.0f, 0.5f, 0.5f, 0.5f }, sw::float4{ 1.0f, 1.0f, 1.0f, 1.0f }},
        {"HalfBlue",  0.75f, sw::float4{ 0.0f, 0.5f, 0.5f, 0.5f }, sw::float4{ 1.0f, 1.0f, 1.0f, 0.4f }},
        {    "Full",  1.25f, sw::float4{ 0.0f, 0.0f, 1.0f, 1.0f }, sw::float4{ 1.0f, 1.0f, 1.0f, 1.0f }},
    };
    constexpr uint32 kSpriteCount = 6;
    constexpr int32  kWindow      = 1; // 표본 자리에서 ±1 픽셀 창의 평균

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string label = sw::string( device->getBackendName() ) + ": ";

        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();
        {
            sw::Scene scene( "SpriteInstanceScene" );
            // 2D 카메라 — +Z 를 보므로 화면 오른쪽이 +X, 위가 +Y 다. 원점이 화면 가운데다.
            sw::GameObject*      pCameraObject = scene.getObjectManager()->createGameObject( sw::hashed_string( "SpriteCamera" ) );
            sw::CameraComponent* pCamera       = ( pCameraObject != nullptr ) ? pCameraObject->addComponent<sw::CameraComponent>() : nullptr;
            bOk                                = bOk && pCamera != nullptr;
            if ( bOk )
            {
                pCamera->setRole( sw::CameraRole::Game );
                pCamera->setLocalPosition( sw::float3{ 0.0f, 0.0f, -4.0f } );
                bOk = scene.ensureDefaultCameras();
            }
            for ( const SpriteCase& spriteCase : kArrCase )
            {
                if ( bOk == false )
                    break;
                sw::GameObject*      pObj    = scene.getObjectManager()->createGameObject( sw::hashed_string( spriteCase._pName ) );
                sw::SpriteComponent* pSprite = ( pObj != nullptr ) ? pObj->addComponent<sw::SpriteComponent>() : nullptr;
                bOk                          = pSprite != nullptr;
                if ( bOk == false )
                    break;
                pSprite->setTextureName( kQuadrantTexture );
                pSprite->setLocalPosition( sw::float3{ spriteCase._x, 0.0f, 0.0f } );
                pSprite->setLocalScale( sw::float3{ 0.4f, 0.4f, 1.0f } );
                pSprite->setUvRect( spriteCase._uvRect );
                pSprite->setTint( spriteCase._tint );
                pSprite->resolveRenderAssets();
            }
            if ( bOk )
            {
                scene.getObjectManager()->flushSceneTransforms();
                sw::engine::getAssetManager().getMaterialManager().initializePending( device.get() );
                // 첫 프레임에는 GpuScene 업로드 · 텍스처가 아직이라 몇 장 돌린다.
                constexpr uint32 kWarmupFrameCount = 4;
                for ( uint32 frameIndex = 0; frameIndex < kWarmupFrameCount && bOk; ++frameIndex )
                {
                    bOk = renderSceneFrame( renderer, device.get(), scene, sw::float4{ 0.0f, 0.0f, 0.0f, 1.0f } );
                }
                SW_EXPECT_TRUE_MSG( bOk, ( label + "프레임 실행 실패" ).c_str() );
            }

            if ( bOk )
            {
                // 여섯이 반투명 배치 하나다 — 프레임 · 색이 배치를 가르지 않는다.
                const sw::vector<sw::GpuMeshBatch>& batches = renderer.getGpuScene().getTransparentBatches();
                SW_EXPECT_TRUE_MSG( batches.size() == 1u && batches[0]._instanceCount == kSpriteCount,
                                    ( label + "스프라이트 여섯이 반투명 배치 하나가 아니다 (배치 " + sw::to_string( batches.size() ) + ")" ).c_str() );

                test::RHITestImage image;
                SW_ASSERT_TRUE_MSG( image.readTransient( renderer, "SceneColor" ), ( label + "SceneColor 를 되읽지 못했다" ).c_str() );
                const int32 row = static_cast<int32>( image.getHeight() / 2 );
                struct Span
                {
                    int32 _start;
                    int32 _end;
                };
                sw::vector<Span> listSpan;
                bool             bInside = false;
                for ( uint32 x = 0; x < image.getWidth(); ++x )
                {
                    const bool bDrawn = test::RHITestImage::isDefaultClearBackground( image.getPixel( x, static_cast<uint32>( row ) ) ) == false;
                    if ( bDrawn && bInside == false )
                        listSpan.push_back( Span{ static_cast<int32>( x ), static_cast<int32>( x ) } );
                    if ( bDrawn )
                        listSpan.back()._end = static_cast<int32>( x );
                    bInside = bDrawn;
                }
                SW_EXPECT_TRUE_MSG( listSpan.size() == kSpriteCount,
                                    ( label + "가운데 줄에서 그려진 구간이 " + sw::to_string( listSpan.size() ) + " 개다 (기대 6)" ).c_str() );
                if ( listSpan.size() == kSpriteCount )
                {
                    // (x, y) 둘레 창의 평균 색입니다.
                    const auto meanAround = [&image]( int32 x, int32 y )
                    {
                        sw::float3 sum{};
                        float32    count{ 0.0f };
                        for ( int32 offsetY = -kWindow; offsetY <= kWindow; ++offsetY )
                        {
                            for ( int32 offsetX = -kWindow; offsetX <= kWindow; ++offsetX )
                            {
                                const test::Rgba8 pixel = image.getPixel( static_cast<uint32>( x + offsetX ), static_cast<uint32>( y + offsetY ) );
                                sum += sw::float3{ static_cast<float32>( pixel._r ), static_cast<float32>( pixel._g ), static_cast<float32>( pixel._b ) };
                                count += 1.0f;
                            }
                        }
                        return sum * ( 1.0f / count );
                    };
                    const auto describe = []( const sw::float3& color )
                    {
                        return "(" + sw::to_string( static_cast<int32>( color._x ) ) + ", " + sw::to_string( static_cast<int32>( color._y ) ) + ", " +
                               sw::to_string( static_cast<int32>( color._z ) ) + ")";
                    };
                    // 이 카메라는 화면 오른쪽이 +X 라 구간 순서가 곧 월드 X 순서다.
                    sw::float3 arrMean[kSpriteCount] = {};
                    for ( uint32 spanIndex = 0; spanIndex < kSpriteCount; ++spanIndex )
                        arrMean[spanIndex] = meanAround( ( listSpan[spanIndex]._start + listSpan[spanIndex]._end ) / 2, row );

                    // 우세 채널로 본다: 빨강 · 초록 · 자홍(빨강 + 파랑, 초록 없음) · 파랑. 경계(섞인 색)라면 두 채널 이상이 함께 높다.
                    constexpr float32 kHigh = 120.0f;
                    constexpr float32 kLow  = 60.0f;
                    const sw::float3& red   = arrMean[0];
                    const sw::float3& green = arrMean[1];
                    const sw::float3& mag   = arrMean[2];
                    const sw::float3& blue  = arrMean[3];
                    const sw::float3& half  = arrMean[4];
                    SW_EXPECT_TRUE_MSG( red._x > kHigh && red._y < kLow && red._z < kLow, ( label + "왼위 칸이 빨강이 아니다 " + describe( red ) ).c_str() );
                    SW_EXPECT_TRUE_MSG( green._y > kHigh && green._x < kLow && green._z < kLow, ( label + "오위 칸이 초록이 아니다 " + describe( green ) ).c_str() );
                    SW_EXPECT_TRUE_MSG( mag._x > kHigh && mag._z > kHigh && mag._y < kLow,
                                        ( label + "흰 칸 × 자홍 색이 자홍이 아니다(색을 곱하지 않았다) " + describe( mag ) ).c_str() );
                    SW_EXPECT_TRUE_MSG( blue._z > kHigh && blue._x < kLow && blue._y < kLow, ( label + "왼아래 칸이 파랑이 아니다 " + describe( blue ) ).c_str() );
                    // 알파 0.4 는 같은 파랑보다 확실히 어둡고(배경과 섞였다) 여전히 파랑이 우세하다.
                    SW_EXPECT_TRUE_MSG( half._z < blue._z * 0.85f && half._z > half._x + 20.0f && half._z > half._y + 20.0f,
                                        ( label + "알파 0.4 파랑 " + describe( half ) + " 이 불투명 파랑 " + describe( blue ) + " 과 구별되지 않는다" ).c_str() );

                    // 텍스처 전체를 보이는 스프라이트: 화면에서 왼쪽 위가 빨강, 오른쪽 위가 초록, 왼쪽 아래가 파랑(뒤집히지 않았다).
                    const Span&      full       = listSpan[5];
                    const int32      centerX    = ( full._start + full._end ) / 2;
                    const int32      quarter    = ( full._end - full._start + 1 ) / 4; // 정사각형이라 위아래 사분 거리도 같다
                    const sw::float3 topLeft    = meanAround( centerX - quarter, row - quarter );
                    const sw::float3 topRight   = meanAround( centerX + quarter, row - quarter );
                    const sw::float3 bottomLeft = meanAround( centerX - quarter, row + quarter );
                    SW_EXPECT_TRUE_MSG( topLeft._x > kHigh && topLeft._y < kLow && topRight._y > kHigh && topRight._x < kLow && bottomLeft._z > kHigh,
                                        ( label + "텍스처가 뒤집혔다 — 왼위 " + describe( topLeft ) + " 오위 " + describe( topRight ) + " 왼아래 " + describe( bottomLeft ) +
                                          " (기대: 빨강 · 초록 · 파랑)" )
                                            .c_str() );
                    SW_LOG_INFO( "%#sprite colors red %# green %# magenta %# blue %# alpha0.4 %# | full: top-left %# top-right %# bottom-left %#", label, describe( red ),
                                 describe( green ), describe( mag ), describe( blue ), describe( half ), describe( topLeft ), describe( topRight ), describe( bottomLeft ) );
                }
            }
        }
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend available for the per-instance sprite test" );
}

/**
 * @brief [RenderPassGpuTest] 나눗수 2 로 선언한 첨부는 반 크기로 만들어지고, 그 첨부에 그리는 패스는 첨부 전체를 덮는다 — 4 백엔드
 * @details `RenderPassAttachment::_resolutionDivisor` 는 첨부를 프레임 크기 / 나눗수로 만든다. 패스는 출력 첨부의 크기로 렌더 패스(뷰포트)를
 *          열어야 한다 — 프레임 크기로 열면 반 크기 타깃에 화면의 왼쪽 위 4 분의 1 만 들어간다. 나눈 후처리 파이프라인의 BloomColor 를 반
 *          크기로 바꿔 그리고, SceneColor 와 BloomColor 에서 배경이 아닌 픽셀의 무게중심을 견준다 — BloomColor 의 중심을 두 배 하면
 *          SceneColor 의 중심이어야 한다(블룸은 원본 색 + 밝은 부분이라 배경 판정이 같다).
 */
SW_TEST_CASE( RenderPassGpuTest, HalfResolutionAttachmentCoversItsWholeTarget )
{
    sw::string pipelineText;
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "engine/pipeline/forwardpipelinestaged.xml", pipelineText ) );
    const sw::string bloomDecl = "<RenderPassAttachment _name=\"BloomColor\"";
    SW_ASSERT_TRUE( pipelineText.find( bloomDecl ) != sw::string::npos );
    pipelineText              = sw::StringUtil::replace( pipelineText, bloomDecl, bloomDecl + " _resolutionDivisor=\"2\"" );
    const sw::string halfPath = test::makeTempPath( "halfbloomforwardpipeline.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( halfPath, pipelineText ) );

    struct Centroid
    {
        uint32  _count{ 0 };
        float32 _x{ 0.0f };
        float32 _y{ 0.0f };
    };
    auto computeCentroid = []( const test::RHITestImage& image ) -> Centroid
    {
        Centroid result{};
        float64  sumX{ 0.0 };
        float64  sumY{ 0.0 };
        for ( uint32 row = 0; row < image.getHeight(); ++row )
        {
            for ( uint32 col = 0; col < image.getWidth(); ++col )
            {
                if ( test::RHITestImage::isDefaultClearBackground( image.getPixel( col, row ) ) )
                    continue;
                ++result._count;
                sumX += static_cast<float64>( col ) + 0.5;
                sumY += static_cast<float64>( row ) + 0.5;
            }
        }
        if ( result._count > 0 )
        {
            result._x = static_cast<float32>( sumX / result._count );
            result._y = static_cast<float32>( sumY / result._count );
        }
        return result;
    };

    uint32                comparedCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string label = sw::string( device->getBackendName() ) + ": ";

        sw::Scene                scene( "HalfResolutionScene" );
        bool                     bOk        = scene.ensureDefaultCameras();
        constexpr uint32         kCubeCount = 3;
        sw::shared_ptr<sw::Mesh> arrMesh[kCubeCount];
        for ( uint32 index = 0; index < kCubeCount && bOk; ++index )
        {
            arrMesh[index]             = sw::MeshUtil::createUnitCube();
            const sw::string   name    = sw::string( "Cube" ) + sw::to_string( index );
            sw::GameObject*    pObject = ( arrMesh[index] != nullptr )
                                           ? scene.getObjectManager()->createGameObject( sw::hashed_string( name.c_str(), static_cast<uint32>( name.size() ) ) )
                                           : nullptr;
            sw::MeshComponent* pMesh   = ( pObject != nullptr ) ? pObject->addComponent<sw::MeshComponent>() : nullptr;
            bOk                        = pMesh != nullptr;
            if ( bOk == false )
                break;
            pMesh->setMesh( arrMesh[index] );
            // 화면 가운데에서 비켜 둔다 — 왼쪽 위 4 분의 1 만 담긴 그림(프레임 크기 뷰포트)과 무게중심이 확연히 갈린다.
            pMesh->setLocalPosition( sw::float3{ 0.8f + static_cast<float32>( index ) * 1.2f, 1.0f, 0.0f } );
        }
        scene.getObjectManager()->flushSceneTransforms();

        test::RHITestImage sceneColor;
        test::RHITestImage bloomColor;
        if ( bOk )
        {
            sw::FrameRenderer renderer;
            bOk = renderer.initialize( device.get(), halfPath ) && renderer.isReady();
            SW_EXPECT_TRUE_MSG( bOk, ( label + "반해상도 블룸 파이프라인을 만들지 못했다" ).c_str() );
            constexpr uint32 kWarmupFrameCount = 3;
            for ( uint32 frameIndex = 0; frameIndex < kWarmupFrameCount && bOk; ++frameIndex )
            {
                bOk = renderSceneFrame( renderer, device.get(), scene, sw::float4{ 0.02f, 0.02f, 0.05f, 1.0f } );
            }
            bOk = bOk && sceneColor.readTransient( renderer, "SceneColor" ) && bloomColor.readTransient( renderer, "BloomColor" );
            SW_EXPECT_TRUE_MSG( bOk, ( label + "그리거나 되읽지 못했다" ).c_str() );
        }
        if ( bOk == false )
            continue;
        ++comparedCount;

        SW_EXPECT_EQUAL( sw::TransientAttachmentPool::computeScaledExtent( sceneColor.getWidth(), 2 ), bloomColor.getWidth() );
        SW_EXPECT_EQUAL( sw::TransientAttachmentPool::computeScaledExtent( sceneColor.getHeight(), 2 ), bloomColor.getHeight() );
        const Centroid full = computeCentroid( sceneColor );
        const Centroid half = computeCentroid( bloomColor );
        SW_LOG_INFO( "%#SceneColor %#x%# drawn %# centroid (%#, %#), BloomColor %#x%# drawn %# centroid x2 (%#, %#)", label, sceneColor.getWidth(),
                     sceneColor.getHeight(), full._count, full._x, full._y, bloomColor.getWidth(), bloomColor.getHeight(), half._count, half._x * 2.0f,
                     half._y * 2.0f );
        SW_EXPECT_TRUE_MSG( full._count > 1000, ( label + "SceneColor 에 큐브가 없다" ).c_str() );
        // 반 크기 타깃의 그려진 픽셀은 원본의 4 분의 1 근처다(가장자리 반올림 · 블룸 번짐 몫으로 넉넉히).
        SW_EXPECT_TRUE_MSG( half._count * 3u >= full._count / 2u && half._count <= full._count / 2u,
                            ( label + "BloomColor 의 그려진 픽셀 수 " + sw::to_string( half._count ) + " 가 원본 " + sw::to_string( full._count ) +
                              " 의 4 분의 1 근처가 아니다" )
                                .c_str() );
        constexpr float32 kMaxCentroidDrift = 4.0f;
        const float32     driftX            = sw::MathUtil::abs( half._x * 2.0f - full._x );
        const float32     driftY            = sw::MathUtil::abs( half._y * 2.0f - full._y );
        SW_EXPECT_TRUE_MSG( driftX <= kMaxCentroidDrift && driftY <= kMaxCentroidDrift,
                            ( label + "반 크기 BloomColor 의 그림이 원본과 어긋난다(무게중심 차이 " + sw::to_string( static_cast<int32>( driftX ) ) + ", " +
                              sw::to_string( static_cast<int32>( driftY ) ) + " 픽셀) — 패스가 타깃 크기가 아니라 프레임 크기로 열렸다" )
                                .c_str() );
    }

    if ( comparedCount == 0 )
        SW_TEST_SKIP( "No RHI backend could run the half-resolution bloom pipeline" );
}

/**
 * @brief [RenderPassGpuTest] 반 크기 원본을 읽는 블룸은 그 원본의 텍셀로 비켜 읽는다 — 4 백엔드
 * @details `forwardpipelinestaged.xml` 의 SceneColor · SceneDepth · BloomColor 를 나눗수 2 로 바꾸면 PostBloom 은 반 크기 원본을 읽어 반 크기에 쓴다. 블룸의 블러는
 *          원본 텍셀 반 칸을 비켜 두 번 읽으므로 출력 텍셀마다 이웃 넷의 평균이 된다. 프레임 텍셀로 비키면 반의 반 칸이라 자기 텍셀 쪽으로 기운다.
 *          CPU 로 두 비킴(원본 텍셀 0.5 칸 · 0.25 칸, 바이리니어)을 흉내 내 GPU 결과가 어느 쪽에 가까운지 본다(비킴은 좌우 · 위아래 대칭이라 행 방향이 상관없다).
 */
SW_TEST_CASE( RenderPassGpuTest, HalfResolutionBloomBlursByTheSourceTexel )
{
    sw::string pipelineText;
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( "engine/pipeline/forwardpipelinestaged.xml", pipelineText ) );
    // 깊이도 같이 나눈다 — 한 패스의 출력(색 · 깊이)은 같은 크기여야 한다(파이프라인 검증).
    for ( const utf8* pName : { "SceneColor", "SceneDepth", "BloomColor" } )
    {
        const sw::string declaration = sw::string( "<RenderPassAttachment _name=\"" ) + pName + "\"";
        SW_ASSERT_TRUE( pipelineText.find( declaration ) != sw::string::npos );
        pipelineText = sw::StringUtil::replace( pipelineText, declaration, declaration + " _resolutionDivisor=\"2\"" );
    }
    const sw::string halfPath = test::makeTempPath( "halfsourcebloomforwardpipeline.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( halfPath, pipelineText ) );

    // FrameRendererConstants.cpp 의 kDefaultBloomParams(문턱 · 세기 · 무릎)와 같은 값.
    constexpr float32 kBloomThreshold = 0.55f;
    constexpr float32 kBloomIntensity = 0.65f;
    constexpr float32 kBloomKnee      = 0.25f;
    // 바이리니어(클램프) 한 번 — 텍셀 좌표의 0.5 가 텍셀 중심이다.
    auto sampleBilinear = []( const test::RHITestImage& image, float32 u, float32 v, float32( &outRgb )[3] )
    {
        const float32     texelX          = u * static_cast<float32>( image.getWidth() ) - 0.5f;
        const float32     texelY          = v * static_cast<float32>( image.getHeight() ) - 0.5f;
        const float32     floorX          = sw::MathUtil::floor( texelX );
        const float32     floorY          = sw::MathUtil::floor( texelY );
        const float32     fracX           = texelX - floorX;
        const float32     fracY           = texelY - floorY;
        const int32       maxX            = static_cast<int32>( image.getWidth() ) - 1;
        const int32       maxY            = static_cast<int32>( image.getHeight() ) - 1;
        const int32       x0              = sw::MathUtil::clamp( static_cast<int32>( floorX ), 0, maxX );
        const int32       y0              = sw::MathUtil::clamp( static_cast<int32>( floorY ), 0, maxY );
        const int32       x1              = sw::MathUtil::clamp( static_cast<int32>( floorX ) + 1, 0, maxX );
        const int32       y1              = sw::MathUtil::clamp( static_cast<int32>( floorY ) + 1, 0, maxY );
        const test::Rgba8 p00             = image.getPixel( static_cast<uint32>( x0 ), static_cast<uint32>( y0 ) );
        const test::Rgba8 p10             = image.getPixel( static_cast<uint32>( x1 ), static_cast<uint32>( y0 ) );
        const test::Rgba8 p01             = image.getPixel( static_cast<uint32>( x0 ), static_cast<uint32>( y1 ) );
        const test::Rgba8 p11             = image.getPixel( static_cast<uint32>( x1 ), static_cast<uint32>( y1 ) );
        const uint8       arrChannel00[3] = { p00._r, p00._g, p00._b };
        const uint8       arrChannel10[3] = { p10._r, p10._g, p10._b };
        const uint8       arrChannel01[3] = { p01._r, p01._g, p01._b };
        const uint8       arrChannel11[3] = { p11._r, p11._g, p11._b };
        for ( uint32 channel = 0; channel < 3; ++channel )
        {
            const float32 top    = static_cast<float32>( arrChannel00[channel] ) * ( 1.0f - fracX ) + static_cast<float32>( arrChannel10[channel] ) * fracX;
            const float32 bottom = static_cast<float32>( arrChannel01[channel] ) * ( 1.0f - fracX ) + static_cast<float32>( arrChannel11[channel] ) * fracX;
            outRgb[channel]      = ( top * ( 1.0f - fracY ) + bottom * fracY ) / 255.0f;
        }
    };
    // 블룸 출력 하나(postbloom.hlsli swApplyBloom, AO 없음)를 원본 텍셀 @p shiftInTexels 칸 비킴으로 흉내 내 GPU 값과의 채널 절대차 합을 냅니다.
    auto computeBloomError = [&sampleBilinear]( const test::RHITestImage& source, const test::RHITestImage& bloom, float32 shiftInTexels ) -> float64
    {
        float64       errorSum = 0.0;
        const float32 shiftU   = shiftInTexels / static_cast<float32>( source.getWidth() );
        const float32 shiftV   = shiftInTexels / static_cast<float32>( source.getHeight() );
        for ( uint32 y = 0; y < bloom.getHeight(); ++y )
        {
            for ( uint32 x = 0; x < bloom.getWidth(); ++x )
            {
                const float32 u = ( static_cast<float32>( x ) + 0.5f ) / static_cast<float32>( bloom.getWidth() );
                const float32 v = ( static_cast<float32>( y ) + 0.5f ) / static_cast<float32>( bloom.getHeight() );
                float32       arrBefore[3]{};
                float32       arrAfter[3]{};
                sampleBilinear( source, u - shiftU, v - shiftV, arrBefore );
                sampleBilinear( source, u + shiftU, v + shiftV, arrAfter );
                const float32     arrBlur[3]  = { ( arrBefore[0] + arrAfter[0] ) * 0.5f, ( arrBefore[1] + arrAfter[1] ) * 0.5f, ( arrBefore[2] + arrAfter[2] ) * 0.5f };
                const float32     peak        = sw::MathUtil::max( sw::MathUtil::max( arrBlur[0], arrBlur[1] ), arrBlur[2] );
                const float32     soft        = sw::MathUtil::clamp( ( peak - kBloomThreshold + kBloomKnee ) / kBloomKnee, 0.0f, 1.0f );
                const test::Rgba8 point       = source.getPixel( sw::MathUtil::min( x, source.getWidth() - 1 ), sw::MathUtil::min( y, source.getHeight() - 1 ) );
                const test::Rgba8 gpu         = bloom.getPixel( x, y );
                const float32     arrPoint[3] = { static_cast<float32>( point._r ) / 255.0f, static_cast<float32>( point._g ) / 255.0f, static_cast<float32>( point._b ) / 255.0f };
                const float32     arrGpu[3]   = { static_cast<float32>( gpu._r ) / 255.0f, static_cast<float32>( gpu._g ) / 255.0f, static_cast<float32>( gpu._b ) / 255.0f };
                for ( uint32 channel = 0; channel < 3; ++channel )
                {
                    const float32 expected = sw::MathUtil::clamp( arrPoint[channel] + arrBlur[channel] * soft * soft * kBloomIntensity, 0.0f, 1.0f );
                    errorSum += static_cast<float64>( sw::MathUtil::abs( expected - arrGpu[channel] ) );
                }
            }
        }
        return errorSum;
    };

    uint32                comparedCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string   label = sw::string( device->getBackendName() ) + ": ";
        LitCubeScene       stage;
        bool               bOk = stage.populate();
        test::RHITestImage sceneColor;
        test::RHITestImage bloomColor;
        if ( bOk )
        {
            sw::FrameRenderer renderer;
            bOk = renderer.initialize( device.get(), halfPath ) && renderer.isReady();
            SW_EXPECT_TRUE_MSG( bOk, ( label + "반해상도 원본 · 블룸 파이프라인을 만들지 못했다" ).c_str() );
            for ( uint32 frameIndex = 0; frameIndex < 3 && bOk; ++frameIndex )
                bOk = renderSceneFrame( renderer, device.get(), stage._scene, sw::float4{ 0.02f, 0.02f, 0.05f, 1.0f } );
            bOk = bOk && sceneColor.readTransient( renderer, "SceneColor" ) && bloomColor.readTransient( renderer, "BloomColor" );
            SW_EXPECT_TRUE_MSG( bOk, ( label + "그리거나 되읽지 못했다" ).c_str() );
            renderer.shutdown();
        }
        if ( bOk == false )
            continue;
        ++comparedCount;
        SW_EXPECT_EQUAL( sceneColor.getWidth(), bloomColor.getWidth() );
        SW_EXPECT_EQUAL( sceneColor.getHeight(), bloomColor.getHeight() );
        const float64 sourceTexelError = computeBloomError( sceneColor, bloomColor, 0.5f );
        const float64 frameTexelError  = computeBloomError( sceneColor, bloomColor, 0.25f );
        SW_LOG_INFO( "%#half-res bloom error vs CPU: source texel shift %#, frame texel shift %# (%#x%#)", label, sourceTexelError, frameTexelError,
                     bloomColor.getWidth(), bloomColor.getHeight() );
        SW_EXPECT_TRUE_MSG( sourceTexelError < frameTexelError,
                            ( label + "반 크기 원본의 블룸이 원본 텍셀이 아니라 프레임 텍셀로 비켜 읽는다" ).c_str() );
    }
    if ( comparedCount == 0 )
        SW_TEST_SKIP( "No RHI backend could run the half-resolution source bloom pipeline" );
}

/**
 * @brief [RenderPassGpuTest] 캡처 카메라 둘이 각자의 렌더 텍스처에 각자 본 것을 그린다(4 백엔드)
 * @details 붉은 큐브만 보는 카메라 → `rendertarget/test_red`, 푸른 큐브만 보는 카메라 → `rendertarget/test_blue`. 두 텍스처를 되읽어 한쪽은 붉고
 *          한쪽은 푸른지 본다. 뷰가 컬링 칸 · 상수버퍼 · 풀을 나눠 쓰면 둘이 같은 그림이 되거나(뒤 뷰의 절두체로 거른다) 비어 있다.
 */
SW_TEST_CASE( RenderPassGpuTest, MultiViewRendersEachCaptureCameraToItsTexture )
{
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string  label = sw::string( device->getBackendName() ) + ": ";
        sw::FrameRenderer renderer;
        MultiViewScene    stage;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady() && stage.populate();
        bOk                   = bOk && stage.addViewCamera( "CameraRed", sw::float3{ -MultiViewScene::kCubeDistance, 0.0f, 0.0f },
                                                            MultiViewScene::makeTextureOutput( "rendertarget/test_red", 0.0f ), sw::CameraRole::Capture ) != nullptr;
        bOk                   = bOk && stage.addViewCamera( "CameraBlue", sw::float3{ MultiViewScene::kCubeDistance, 0.0f, 0.0f },
                                                            MultiViewScene::makeTextureOutput( "rendertarget/test_blue", 0.0f ), sw::CameraRole::Capture ) != nullptr;
        SW_EXPECT_TRUE_MSG( bOk, ( label + "무대 준비" ).c_str() );
        for ( uint32 frameIndex = 0; frameIndex < 3 && bOk; ++frameIndex )
            bOk = renderSceneFrame( renderer, device.get(), stage._scene, sw::float4{ 0.0f, 0.0f, 0.0f, 1.0f } );
        SW_EXPECT_EQUAL( 2u, renderer.getExtraViewCount() );
        SW_EXPECT_EQUAL( 2u, renderer.getLastRenderedExtraViewCount() );

        int64  redDiff   = 0;
        int64  blueDiff  = 0;
        uint32 redDrawn  = 0;
        uint32 blueDrawn = 0;
        uint32 width     = 0;
        SW_EXPECT_TRUE_MSG( bOk && MultiViewScene::readTextureRedMinusBlue( device.get(), "rendertarget/test_red", redDiff, redDrawn, width ),
                            ( label + "붉은 뷰 텍스처를 읽지 못했다" ).c_str() );
        SW_EXPECT_EQUAL( 64u, width ); // 카메라가 알린 크기
        SW_EXPECT_TRUE_MSG( bOk && MultiViewScene::readTextureRedMinusBlue( device.get(), "rendertarget/test_blue", blueDiff, blueDrawn, width ),
                            ( label + "푸른 뷰 텍스처를 읽지 못했다" ).c_str() );
        SW_LOG_INFO( "%#red view drawn %# R-B %#, blue view drawn %# R-B %#", label, redDrawn, redDiff, blueDrawn, blueDiff );
        SW_EXPECT_TRUE_MSG( redDrawn > 100 && blueDrawn > 100, ( label + "뷰에 큐브가 그려지지 않았다" ).c_str() );
        // 조명 · 톤맵이 채도를 줄이므로 절댓값보다 두 뷰의 차이를 본다.
        SW_EXPECT_TRUE_MSG( redDiff > 10, ( label + "붉은 뷰가 붉지 않다 (R-B " + sw::to_string( redDiff ) + ")" ).c_str() );
        SW_EXPECT_TRUE_MSG( blueDiff < -10, ( label + "푸른 뷰가 푸르지 않다 (R-B " + sw::to_string( blueDiff ) + ")" ).c_str() );
        SW_EXPECT_TRUE_MSG( redDiff > blueDiff + 40, ( label + "두 뷰가 같은 그림이다" ).c_str() );
        renderer.shutdown();
    }
    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the multi-view test" );
}

/**
 * @brief 후처리를 끈 뷰는 포스트 체인을 건너뛴다 — 같은 큐브를 같은 자리에서 보는 두 뷰(후처리 켬 · 끔)의 그림이 다르다.
 * @details 끈 뷰는 `SW_PASS_FLAG_SKIP_POST` 로 포스트 체인이 원본(톤맵 전 장면 색)을 고른다. 플래그를 무시하면 두 그림이 같아진다.
 */
SW_TEST_CASE( RenderPassGpuTest, PostProcessOffViewSkipsThePostChain )
{
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string       label     = sw::string( device->getBackendName() ) + ": ";
        const sw::float3       redTarget = sw::float3{ -MultiViewScene::kCubeDistance, 0.0f, 0.0f };
        sw::FrameRenderer      renderer;
        MultiViewScene         stage;
        sw::CameraRenderOutput outputOff = MultiViewScene::makeTextureOutput( "rendertarget/test_post_off", 0.0f );
        outputOff._bPostProcess          = false;
        bool bOk                         = renderer.initialize( device.get() ) && renderer.isReady() && stage.populate();
        bOk                              = bOk && stage.addViewCamera( "CameraPostOn", redTarget, MultiViewScene::makeTextureOutput( "rendertarget/test_post_on", 0.0f ),
                                                                       sw::CameraRole::Capture ) != nullptr;
        bOk                              = bOk && stage.addViewCamera( "CameraPostOff", redTarget, outputOff, sw::CameraRole::Capture ) != nullptr;
        SW_EXPECT_TRUE_MSG( bOk, ( label + "무대 준비" ).c_str() );
        for ( uint32 frameIndex = 0; frameIndex < 3 && bOk; ++frameIndex )
            bOk = renderSceneFrame( renderer, device.get(), stage._scene, sw::float4{ 0.0f, 0.0f, 0.0f, 1.0f } );

        int64  onDiff   = 0;
        int64  offDiff  = 0;
        uint32 onDrawn  = 0;
        uint32 offDrawn = 0;
        uint32 width    = 0;
        SW_EXPECT_TRUE_MSG( bOk && MultiViewScene::readTextureRedMinusBlue( device.get(), "rendertarget/test_post_on", onDiff, onDrawn, width ),
                            ( label + "후처리 켠 뷰를 읽지 못했다" ).c_str() );
        SW_EXPECT_TRUE_MSG( bOk && MultiViewScene::readTextureRedMinusBlue( device.get(), "rendertarget/test_post_off", offDiff, offDrawn, width ),
                            ( label + "후처리 끈 뷰를 읽지 못했다" ).c_str() );
        SW_LOG_INFO( "%#post on drawn %# R-B %#, post off drawn %# R-B %#", label, onDrawn, onDiff, offDrawn, offDiff );
        SW_EXPECT_TRUE_MSG( onDrawn > 100 && offDrawn > 100, ( label + "뷰에 큐브가 그려지지 않았다" ).c_str() );
        const int64 gap = onDiff > offDiff ? onDiff - offDiff : offDiff - onDiff;
        SW_EXPECT_TRUE_MSG( gap > 15, ( label + "후처리를 꺼도 그림이 같다 (켬 " + sw::to_string( onDiff ) + " · 끔 " + sw::to_string( offDiff ) + ")" ).c_str() );
        renderer.shutdown();
    }
    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the post-off view test" );
}

#if !defined( SW_SHIPPING ) // 렌더러 시계 고정(`setAnimationTimeOverride`)은 배포본에 없다
/**
 * @brief [RenderPassGpuTest] 갱신 주기가 있는 뷰는 쉬는 프레임에 다시 그리지 않는다 — 텍스처가 지난 그림을 지킨다
 * @details 1 Hz 캡처 카메라. 0 초에 붉은 큐브를 그린 뒤 큐브를 푸르게 바꾸고 0.1 초에 그리면 쉬는 프레임이라 텍스처는 여전히 붉다. 1.1 초에는
 *          다시 그려 푸르다. 쉬지 않으면(주기를 무시하면) 0.1 초에 이미 푸르다.
 */
SW_TEST_CASE( RenderPassGpuTest, ExtraViewSkipsFramesByItsUpdateRate )
{
    test::RHITestDevice device( { sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::DirectX11, sw::RHIBackend::OpenGL } );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "No RHI backend for the update-rate test" );

    sw::FrameRenderer renderer;
    MultiViewScene    stage;
    SW_ASSERT_TRUE( renderer.initialize( device.get() ) && stage.populate() );
    SW_ASSERT_NOT_NULL( stage.addViewCamera( "CameraRed", sw::float3{ -MultiViewScene::kCubeDistance, 0.0f, 0.0f },
                                             MultiViewScene::makeTextureOutput( "rendertarget/test_rate", 1.0f ), sw::CameraRole::Capture ) );
    const sw::float4 clear{ 0.0f, 0.0f, 0.0f, 1.0f };
    int64            diff  = 0;
    uint32           drawn = 0;
    uint32           width = 0;

    renderer.setAnimationTimeOverride( 0.0f );
    SW_ASSERT_TRUE( renderSceneFrame( renderer, device.get(), stage._scene, clear ) );
    SW_EXPECT_EQUAL( 1u, renderer.getLastRenderedExtraViewCount() );
    SW_ASSERT_TRUE( MultiViewScene::readTextureRedMinusBlue( device.get(), "rendertarget/test_rate", diff, drawn, width ) );
    SW_EXPECT_TRUE_MSG( diff > 10, ( "첫 그림이 붉지 않다 (R-B " + sw::to_string( diff ) + ")" ).c_str() );

    stage._pCubeRed->setMaterial( stage._materialBlue.get() );
    renderer.setAnimationTimeOverride( 0.1f );
    SW_ASSERT_TRUE( renderSceneFrame( renderer, device.get(), stage._scene, clear ) );
    SW_EXPECT_EQUAL( 0u, renderer.getLastRenderedExtraViewCount() );
    SW_ASSERT_TRUE( MultiViewScene::readTextureRedMinusBlue( device.get(), "rendertarget/test_rate", diff, drawn, width ) );
    SW_EXPECT_TRUE_MSG( diff > 10, ( "쉬는 프레임에 다시 그렸다 (R-B " + sw::to_string( diff ) + ")" ).c_str() );

    renderer.setAnimationTimeOverride( 1.1f );
    SW_ASSERT_TRUE( renderSceneFrame( renderer, device.get(), stage._scene, clear ) );
    SW_EXPECT_EQUAL( 1u, renderer.getLastRenderedExtraViewCount() );
    SW_ASSERT_TRUE( MultiViewScene::readTextureRedMinusBlue( device.get(), "rendertarget/test_rate", diff, drawn, width ) );
    SW_EXPECT_TRUE_MSG( diff < -10, ( "주기가 지났는데 다시 그리지 않았다 (R-B " + sw::to_string( diff ) + ")" ).c_str() );
    renderer.shutdown();
}
#endif

/**
 * @brief [RenderPassGpuTest] 화면 사각형 뷰(PiP)는 주 출력의 그 사각형 안에만 그린다(4 백엔드, Present 캡처)
 * @details 주 카메라는 빈 쪽을 보고, PiP 카메라는 붉은 큐브를 본다. 캡처의 오른쪽 아래 사각형(0.55..0.95)만 붉어야 한다 — 뷰포트를 안 걸면 화면
 *          전체가 붉고, 위아래가 뒤집히면(GL 원점) 오른쪽 위가 붉다.
 */
SW_TEST_CASE( RenderPassGpuTest, ScreenRectViewDrawsOnlyInsideItsRectangle )
{
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string       label = sw::string( device->getBackendName() ) + ": ";
        sw::FrameRenderer      renderer;
        MultiViewScene         stage;
        bool                   bOk = renderer.initialize( device.get() ) && renderer.isReady() && stage.populate();
        sw::CameraRenderOutput output;
        output._target     = sw::CameraOutputTarget::ScreenRect;
        output._screenRect = sw::float4{ 0.55f, 0.55f, 0.4f, 0.4f };
        bOk                = bOk && stage.addViewCamera( "PictureInPicture", sw::float3{ -MultiViewScene::kCubeDistance, 0.0f, 0.0f }, output, sw::CameraRole::Game ) != nullptr;
        renderer.setPresentCaptureEnabled( true );
        for ( uint32 frameIndex = 0; frameIndex < 3 && bOk; ++frameIndex )
            bOk = renderSceneFrame( renderer, device.get(), stage._scene, sw::float4{ 0.0f, 0.0f, 0.0f, 1.0f } );
        sw::vector<uint8>     bytes;
        sw::RHITextureMipSpan layout{};
        bOk = bOk && renderer.readbackPresentCapture( bytes, layout );
        SW_EXPECT_TRUE_MSG( bOk, ( label + "그리기 · 캡처" ).c_str() );
        if ( bOk )
        {
            test::RHITestImage image;
            image.assign( std::move( bytes ), layout, sw::constant::kBackBufferFormat );
            // 사각형 안 · 밖의 붉은 픽셀(R 이 B 보다 확연히 크다) 수.
            uint32 insideRed  = 0;
            uint32 outsideRed = 0;
            for ( uint32 y = 0; y < image.getHeight(); ++y )
            {
                for ( uint32 x = 0; x < image.getWidth(); ++x )
                {
                    const test::Rgba8 pixel   = image.getPixel( x, y );
                    const bool        bRed    = pixel._r > pixel._b + 60 && pixel._r > pixel._g + 40;
                    const float32     u       = ( static_cast<float32>( x ) + 0.5f ) / static_cast<float32>( image.getWidth() );
                    const float32     v       = ( static_cast<float32>( y ) + 0.5f ) / static_cast<float32>( image.getHeight() );
                    const bool        bInside = 0.55f <= u && u <= 0.95f && 0.55f <= v && v <= 0.95f;
                    if ( bRed && bInside )
                        ++insideRed;
                    else if ( bRed )
                        ++outsideRed;
                }
            }
            SW_LOG_INFO( "%#PiP red inside %#, outside %# (of %#x%#)", label, insideRed, outsideRed, image.getWidth(), image.getHeight() );
            SW_EXPECT_TRUE_MSG( insideRed > image.getPixelCount() / 200, ( label + "사각형 안에 PiP 그림이 없다" ).c_str() );
            SW_EXPECT_TRUE_MSG( outsideRed * 50 < insideRed, ( label + "PiP 가 사각형 밖에 그려졌다 (밖 " + sw::to_string( outsideRed ) + ")" ).c_str() );
        }
        renderer.shutdown();
    }
    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the screen-rect view test" );
}

/**
 * @brief [RenderPassGpuTest] 창으로 나간 그림(백버퍼)이 Present 캡처와 같다 — 4 백엔드
 * @details 스크린샷(`-gv_screenshot`)은 오프스크린 캡처를 읽으므로 캡처 → 창 블릿의 반전 · 잘림은 거기서 보이지 않는다(GL 창은 0 행이 아래).
 *          캡처를 켜고 그린 프레임의 백버퍼를 읽어 캡처와 픽셀로 견준다 — 위아래가 뒤집히면 큐브 · 바닥 자리가 갈린다.
 */
SW_TEST_CASE( RenderPassGpuTest, PresentedBackBufferMatchesTheCapture )
{
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string  label = sw::string( device->getBackendName() ) + ": ";
        sw::FrameRenderer renderer;
        LitCubeScene      stage;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady() && stage.populate();
        // 화면 위쪽에 치우친 큐브 — 상하 반전이 그림을 크게 옮긴다.
        if ( bOk )
        {
            stage._pCube->setLocalPosition( sw::float3{ 0.6f, 0.9f, 0.0f } );
            stage._scene.getObjectManager()->flushSceneTransforms();
        }
        renderer.setPresentCaptureEnabled( true );
        const sw::float4 clear{ 0.0f, 0.0f, 0.0f, 1.0f };
        for ( uint32 frameIndex = 0; frameIndex < 2 && bOk; ++frameIndex )
            bOk = renderSceneFrame( renderer, device.get(), stage._scene, clear );
        test::RHITestImage window;
        bOk = bOk && renderSceneFrameReadingBackBuffer( renderer, device.get(), stage._scene, clear, window );
        sw::vector<uint8>     captureBytes;
        sw::RHITextureMipSpan captureLayout{};
        bOk = bOk && renderer.readbackPresentCapture( captureBytes, captureLayout );
        SW_EXPECT_TRUE_MSG( bOk, ( label + "그리기 · 백버퍼 · 캡처 읽기" ).c_str() );
        if ( bOk )
        {
            test::RHITestImage capture;
            capture.assign( std::move( captureBytes ), captureLayout, sw::constant::kBackBufferFormat );
            SW_EXPECT_EQUAL( capture.getWidth(), window.getWidth() );
            SW_EXPECT_EQUAL( capture.getHeight(), window.getHeight() );
            uint32 differCount = 0;
            uint32 drawnCount  = 0;
            for ( uint32 y = 0; y < capture.getHeight() && y < window.getHeight(); ++y )
            {
                for ( uint32 x = 0; x < capture.getWidth() && x < window.getWidth(); ++x )
                {
                    const test::Rgba8 capturePixel = capture.getPixel( x, y );
                    if ( test::RHITestImage::getColorDistance( capturePixel, capture.getPixel( 0, 0 ) ) >= 24 )
                        ++drawnCount;
                    if ( test::RHITestImage::getColorDistance( capturePixel, window.getPixel( x, y ) ) > 6 )
                        ++differCount;
                }
            }
            SW_LOG_INFO( "%#window vs capture: %# of %# px differ (drawn %#)", label, differCount, capture.getPixelCount(), drawnCount );
            SW_EXPECT_TRUE_MSG( drawnCount > capture.getPixelCount() / 200, ( label + "캡처에 큐브가 없다 — 비교가 뜻이 없다" ).c_str() );
            SW_EXPECT_TRUE_MSG( differCount * 100 < drawnCount, ( label + "창에 나간 그림이 캡처와 다르다 (다른 픽셀 " + sw::to_string( differCount ) + ")" ).c_str() );
        }
        renderer.shutdown();
    }
    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the back buffer readback test" );
}

/**
 * @brief [RenderPassGpuTest] Present 가 백버퍼에 직접 그릴 때(캡처 끔) 화면 사각형 뷰가 백버퍼의 오른쪽 아래에 앉는다 — 4 백엔드
 * @details GL 기본 프레임버퍼는 아래 원점이라 `setViewport` 가 y 를 뒤집는다. 캡처를 켜면 Present 가 오프스크린 FBO 에 그려 이 갈래를 안 지난다
 *          (`ScreenRectViewDrawsOnlyInsideItsRectangle` 은 캡처를 본다). 뒤집기가 빠지면 PiP 가 오른쪽 **위**에 그려진다.
 */
SW_TEST_CASE( RenderPassGpuTest, ScreenRectViewLandsInItsCornerOfTheBackBuffer )
{
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string       label = sw::string( device->getBackendName() ) + ": ";
        sw::FrameRenderer      renderer;
        MultiViewScene         stage;
        bool                   bOk = renderer.initialize( device.get() ) && renderer.isReady() && stage.populate();
        sw::CameraRenderOutput output;
        output._target     = sw::CameraOutputTarget::ScreenRect;
        output._screenRect = sw::float4{ 0.55f, 0.55f, 0.4f, 0.4f };
        bOk                = bOk && stage.addViewCamera( "PictureInPicture", sw::float3{ -MultiViewScene::kCubeDistance, 0.0f, 0.0f }, output, sw::CameraRole::Game ) != nullptr;
        renderer.setPresentCaptureEnabled( false );
        const sw::float4 clear{ 0.0f, 0.0f, 0.0f, 1.0f };
        for ( uint32 frameIndex = 0; frameIndex < 2 && bOk; ++frameIndex )
            bOk = renderSceneFrame( renderer, device.get(), stage._scene, clear );
        test::RHITestImage window;
        bOk = bOk && renderSceneFrameReadingBackBuffer( renderer, device.get(), stage._scene, clear, window );
        SW_EXPECT_TRUE_MSG( bOk, ( label + "그리기 · 백버퍼 읽기" ).c_str() );
        if ( bOk )
        {
            uint32 insideRed  = 0;
            uint32 outsideRed = 0;
            for ( uint32 y = 0; y < window.getHeight(); ++y )
            {
                for ( uint32 x = 0; x < window.getWidth(); ++x )
                {
                    const test::Rgba8 pixel   = window.getPixel( x, y );
                    const bool        bRed    = pixel._r > pixel._b + 60 && pixel._r > pixel._g + 40;
                    const float32     u       = ( static_cast<float32>( x ) + 0.5f ) / static_cast<float32>( window.getWidth() );
                    const float32     v       = ( static_cast<float32>( y ) + 0.5f ) / static_cast<float32>( window.getHeight() );
                    const bool        bInside = 0.55f <= u && u <= 0.95f && 0.55f <= v && v <= 0.95f;
                    if ( bRed && bInside )
                        ++insideRed;
                    else if ( bRed )
                        ++outsideRed;
                }
            }
            SW_LOG_INFO( "%#back buffer PiP red inside %#, outside %#", label, insideRed, outsideRed );
            SW_EXPECT_TRUE_MSG( insideRed > window.getPixelCount() / 200, ( label + "백버퍼의 사각형 안에 PiP 가 없다" ).c_str() );
            SW_EXPECT_TRUE_MSG( outsideRed * 50 < insideRed, ( label + "PiP 가 백버퍼의 사각형 밖(위아래 뒤집힘?)에 그려졌다" ).c_str() );
        }
        renderer.shutdown();
    }
    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the back buffer screen-rect test" );
}

/**
 * @brief [RenderPassGpuTest] 추가 뷰(렌더 텍스처)의 투명 순서는 그 뷰의 눈으로 정한다 — 같은 눈을 주 카메라로 둔 그림과 같다(4 백엔드)
 * @details 투명 큐브 여섯(같은 메시 · 머티리얼 = 한 배치, 회전을 달리해 순서가 그림에 남게)과 다른 머티리얼의 투명 큐브 하나(배치 순서)를 겹쳐 두고,
 *          주 카메라는 앞(+Z)에서, 캡처 카메라는 뒤(-Z)에서 본다. 캡처 텍스처를 "그 캡처 카메라를 주 카메라로 둔 렌더러" 의 Present 캡처(같은 크기)와 견준다.
 *          주 순서로 그리면 뒤에서 본 그림의 겹침이 거꾸로 섞여 색이 갈린다.
 */
SW_TEST_CASE( RenderPassGpuTest, ExtraViewSortsTransparencyFromItsOwnEye )
{
    constexpr uint32      kWidth  = 64;
    constexpr uint32      kHeight = 48;
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string label = sw::string( device->getBackendName() ) + ": ";
        // 씬 하나 — 큐브 일곱과 기본 카메라. 캡처 카메라의 자리 · 방향은 기준 렌더에서 주 카메라로 다시 쓴다.
        auto populate = []( sw::Scene& scene, sw::shared_ptr<sw::Mesh>& outMesh, sw::shared_ptr<sw::Material>& outGlass, sw::shared_ptr<sw::Material>& outTint ) -> bool
        {
            if ( scene.ensureDefaultCameras() == false )
                return false;
            outMesh  = sw::MeshUtil::createUnitCube();
            outGlass = sw::Material::create();
            outTint  = sw::Material::create();
            if ( outMesh == nullptr || outGlass->loadFromFile( "engine/materials/glassmaterial.material" ) == false ||
                 outGlass->setParameter( nullptr, sw::hashed_string( "color" ), "0.2 0.9 0.3 0.55" ) == false ||
                 outTint->loadFromFile( "engine/materials/glassmaterial.material" ) == false ||
                 outTint->setParameter( nullptr, sw::hashed_string( "color" ), "1.0 0.1 0.1 0.6" ) == false )
                return false;
            for ( uint32 cubeIndex = 0; cubeIndex < 7; ++cubeIndex )
            {
                const sw::string   name    = sw::string( "Glass" ) + sw::to_string( cubeIndex );
                sw::GameObject*    pObject = scene.getObjectManager()->createGameObject( sw::hashed_string( name ) );
                sw::MeshComponent* pMesh   = pObject != nullptr ? pObject->addComponent<sw::MeshComponent>() : nullptr;
                if ( pMesh == nullptr )
                    return false;
                pMesh->setMesh( outMesh );
                pMesh->setMaterial( cubeIndex == 6 ? outTint.get() : outGlass.get() ); // 마지막 하나만 다른 배치
                const float32 offset = static_cast<float32>( cubeIndex ) * 0.30f;
                pMesh->setLocalPosition( sw::float3{ offset - 0.9f, 0.0f, offset - 0.9f } );
                pMesh->setLocalRotation( sw::float3{ 0.3f * static_cast<float32>( cubeIndex % 3 ), 0.65f * static_cast<float32>( cubeIndex ), 0.0f } );
            }
            scene.getObjectManager()->flushSceneTransforms();
            return true;
        };
        const sw::float3 captureEye{ 0.4f, 1.0f, -4.0f }; // 주 카메라(기본: +Z 쪽)의 반대편
        const sw::float4 clear{ 0.0f, 0.0f, 0.0f, 1.0f };

        // 1) 추가 뷰로 그린 그림.
        sw::Scene                    viewScene( "ExtraViewTransparency" );
        sw::shared_ptr<sw::Mesh>     viewMesh;
        sw::shared_ptr<sw::Material> viewGlass;
        sw::shared_ptr<sw::Material> viewTint;
        sw::FrameRenderer            viewRenderer;
        bool                         bOk      = viewRenderer.initialize( device.get() ) && viewRenderer.isReady() && populate( viewScene, viewMesh, viewGlass, viewTint );
        sw::CameraComponent*         pCapture = nullptr;
        if ( bOk )
        {
            sw::GameObject* pObject = viewScene.getObjectManager()->createGameObject( sw::hashed_string( "Capture" ) );
            pCapture                = pObject != nullptr ? pObject->addComponent<sw::CameraComponent>() : nullptr;
            bOk                     = pCapture != nullptr;
        }
        if ( bOk )
        {
            sw::CameraRenderOutput output;
            output._target              = sw::CameraOutputTarget::RenderTexture;
            output._renderTexture       = "rendertarget/test_extraviewtransparency";
            output._renderTextureWidth  = kWidth;
            output._renderTextureHeight = kHeight;
            pCapture->setRole( sw::CameraRole::Capture );
            pCapture->setLocalPosition( captureEye );
            pCapture->lookAt( sw::float3{} );
            pCapture->setRenderOutput( output );
            viewScene.getObjectManager()->flushSceneTransforms();
        }
        for ( uint32 frameIndex = 0; frameIndex < 3 && bOk; ++frameIndex )
            bOk = renderSceneFrame( viewRenderer, device.get(), viewScene, clear );
        test::RHITestImage viewImage;
        if ( bOk )
        {
            const sw::Texture2D*  pTexture = sw::engine::getAssetManager().getTextureManager().find( "rendertarget/test_extraviewtransparency" );
            sw::vector<uint8>     bytes;
            sw::RHITextureMipSpan layout{};
            bOk = pTexture != nullptr && device->getResourceFactory()->readbackTexture2D( pTexture->getHandle(), 0, 0, bytes, layout );
            if ( bOk )
                viewImage.assign( std::move( bytes ), layout, pTexture->getFormat() );
        }

        // 2) 같은 눈을 주 카메라로 둔 기준 그림(같은 크기의 Present 캡처).
        sw::Scene                    mainScene( "MainViewTransparency" );
        sw::shared_ptr<sw::Mesh>     mainMesh;
        sw::shared_ptr<sw::Material> mainGlass;
        sw::shared_ptr<sw::Material> mainTint;
        sw::FrameRenderer            mainRenderer;
        bOk = bOk && mainRenderer.initialize( device.get() ) && mainRenderer.isReady() && populate( mainScene, mainMesh, mainGlass, mainTint );
        if ( bOk )
        {
            sw::CameraComponent* pMain = mainScene.getActiveGameCamera();
            bOk                        = pMain != nullptr;
            if ( bOk )
            {
                pMain->setLocalPosition( captureEye );
                pMain->lookAt( sw::float3{} );
                mainScene.getObjectManager()->flushSceneTransforms();
            }
        }
        mainRenderer.setOutputSizeOverride( kWidth, kHeight );
        mainRenderer.setPresentCaptureEnabled( true );
        for ( uint32 frameIndex = 0; frameIndex < 3 && bOk; ++frameIndex )
            bOk = renderSceneFrame( mainRenderer, device.get(), mainScene, clear );
        sw::vector<uint8>     mainBytes;
        sw::RHITextureMipSpan mainLayout{};
        bOk = bOk && mainRenderer.readbackPresentCapture( mainBytes, mainLayout );
        SW_EXPECT_TRUE_MSG( bOk, ( label + "그리기 · 읽기" ).c_str() );
        if ( bOk )
        {
            test::RHITestImage mainImage;
            mainImage.assign( std::move( mainBytes ), mainLayout, sw::constant::kBackBufferFormat );
            SW_EXPECT_EQUAL( mainImage.getWidth(), viewImage.getWidth() );
            SW_EXPECT_EQUAL( mainImage.getHeight(), viewImage.getHeight() );
            uint32 drawnCount  = 0;
            uint32 differCount = 0;
            for ( uint32 y = 0; y < mainImage.getHeight() && y < viewImage.getHeight(); ++y )
            {
                for ( uint32 x = 0; x < mainImage.getWidth() && x < viewImage.getWidth(); ++x )
                {
                    const test::Rgba8 reference = mainImage.getPixel( x, y );
                    if ( test::RHITestImage::getColorDistance( reference, mainImage.getPixel( 0, 0 ) ) >= 24 )
                        ++drawnCount;
                    if ( test::RHITestImage::getColorDistance( reference, viewImage.getPixel( x, y ) ) > 8 )
                        ++differCount;
                }
            }
            SW_LOG_INFO( "%#extra view vs main view of the same eye: %# px differ (drawn %#)", label, differCount, drawnCount );
            SW_EXPECT_TRUE_MSG( drawnCount > kWidth * kHeight / 20, ( label + "기준 그림에 투명 큐브가 없다" ).c_str() );
            SW_EXPECT_TRUE_MSG( differCount * 50 < drawnCount, ( label + "추가 뷰의 투명 순서가 그 뷰의 눈과 다르다 (다른 픽셀 " + sw::to_string( differCount ) + ")" ).c_str() );
        }
        mainRenderer.shutdown();
        viewRenderer.shutdown();
    }
    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the extra-view transparency test" );
}

/**
 * @brief [RenderPassGpuTest] GL 컨텍스트를 다른 스레드가 잠깐 쥐고 있으면 바인딩은 기다려서 잡는다 — [Error] 없음
 * @details 게임 스레드의 자원 생성(ScopedOpenGLContext)이 컨텍스트를 쥔 순간 렌더 스레드가 프레임을 시작하면, 한 번만 시도하던 바인딩이 [Error] 를 남기고
 *          그 프레임을 잃었다(NileCity 자동 플레이 골든 기록에서 한 번). 다른 스레드가 30 ms 쥐었다 놓는 동안 이 스레드의 bindGraphicsContext 가 성공해야 한다.
 */
SW_TEST_CASE( RenderPassGpuTest, GlContextBindWaitsForAShortHolder )
{
    test::RHITestDevice device( sw::RHIBackend::OpenGL );
    if ( device.isReady() == false )
        SW_TEST_SKIP( "OpenGL is not available" );
    sw::IRHIDevice* pDevice = device.get();
    pDevice->unbindGraphicsContext(); // 시험 스레드가 쥐고 있던 것을 놓는다

    std::atomic<bool>  bHeld{ false };
    std::atomic<bool>  bHolderDone{ false };
    std::thread        holder( [pDevice, &bHeld, &bHolderDone]()
    {
        if ( pDevice->bindGraphicsContext() )
        {
            bHeld.store( true );
            std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
            pDevice->unbindGraphicsContext();
        }
        bHolderDone.store( true );
    } );
    const sw::Deadline deadline = sw::Deadline::afterMilliseconds( 5000 );
    while ( bHeld.load() == false && bHolderDone.load() == false && deadline.isExpired() == false )
        std::this_thread::yield();
    const bool bHolderTookIt = bHeld.load();
    const bool bBound        = pDevice->bindGraphicsContext(); // 30 ms 안에 놓이므로 기다리면 된다
    holder.join();
    SW_EXPECT_TRUE_MSG( bHolderTookIt, "the holder thread could not take the GL context" );
    SW_EXPECT_TRUE_MSG( bBound, "bindGraphicsContext gave up while another thread held the context for 30 ms" );
    if ( bBound == false )
        (void)pDevice->bindGraphicsContext(); // 디바이스를 내리는 쪽(이 스레드)이 컨텍스트를 쥐어야 한다
}

/**
 * @brief [RenderPassGpuTest] 소프트웨어 어댑터 스위치(`gv_rhiSoftwareAdapter`)로 DX12 · DX11 이 WARP 로 선다(Windows) — CI 러너 조건을 이 PC 에서 만든다
 * @details 디바이스가 적는 "실제로 선 어댑터가 소프트웨어인가" 를 본다(요청이 아니라 결과). 스위치를 끄면 하드웨어 어댑터로 돌아와야 한다.
 */
SW_TEST_CASE( RenderPassGpuTest, SoftwareAdapterSwitchStartsWarp )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "global variable switches (gv_rhiSoftwareAdapter) are a development-build feature" );
#elif defined( SW_PLATFORM_WINDOWS )
    sw::GlobalVariableInfo* pSwitch = sw::engine::getGlobalVariableManager().findVariable( "gv_rhiSoftwareAdapter" );
    SW_ASSERT_TRUE( pSwitch != nullptr );
    for ( const sw::RHIBackend backend : { sw::RHIBackend::DirectX12, sw::RHIBackend::DirectX11 } )
    {
        SW_EXPECT_TRUE( pSwitch->setValueAsInt( 1 ) );
        bool bSoftware = false;
        bool bReady    = false;
        {
            test::RHITestDevice device( backend );
            bReady    = device.isReady();
            bSoftware = bReady && device->isRunningOnSoftwareAdapter();
        }
        SW_EXPECT_TRUE( pSwitch->setValueAsInt( 0 ) );
        SW_EXPECT_TRUE_MSG( bReady, "the WARP device did not start" );
        SW_EXPECT_TRUE_MSG( bSoftware, "gv_rhiSoftwareAdapter=1 did not put the device on the software adapter" );
        // 환경 변수(SW_RHI_SOFTWARE_ADAPTER=1)로 시험 전체를 WARP 로 돌리는 실행이면 "끄면 하드웨어" 는 볼 수 없다.
        const utf8*         pEnv     = std::getenv( "SW_RHI_SOFTWARE_ADAPTER" );
        const bool          bEnvWarp = pEnv != nullptr && sw::StringUtil::equals( pEnv, "1" );
        test::RHITestDevice hardware( backend );
        if ( hardware.isReady() && bEnvWarp == false )
            SW_EXPECT_FALSE_MSG( hardware->isRunningOnSoftwareAdapter(), "the device stayed on the software adapter after the switch went off" );
    }
#else
    SW_TEST_SKIP( "WARP is a Windows adapter" );
#endif
}

/**
 * @brief [RenderPassGpuTest] 컷 표시는 TAA 기록을 버린다 — 큐브 색을 바꾼 컷 프레임의 TaaColor 에 지난 색이 섞이지 않는다(4 백엔드, 디퍼드)
 * @details TAA 는 이번 원본과 기록을 0.1 : 0.9 로 섞는다. 붉은 큐브를 몇 프레임 그린 뒤 푸르게 바꾸면 컷이 없을 때 TaaColor 는 여전히 붉은 쪽이고,
 *          주 카메라에 컷을 표시하면 그 프레임부터 푸르다.
 */
SW_TEST_CASE( RenderPassGpuTest, CameraCutResetsTaaHistory )
{
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string label = sw::string( device->getBackendName() ) + ": ";
        int64            arrDiff[2]{};
        bool             bOk = true;
        for ( uint32 caseIndex = 0; caseIndex < 2 && bOk; ++caseIndex )
        {
            const bool        bCut = caseIndex == 1;
            sw::FrameRenderer renderer;
            LitCubeScene      stage;
            bOk = renderer.initialize( device.get(), sw::engine::getEngineDefaultAssets()._defaultDeferredPipeline ) && renderer.isReady() && stage.populate();
            bOk = bOk && stage._material->setParameter( nullptr, sw::hashed_string( "color" ), "1.0 0.02 0.02 1.0" );
            for ( uint32 frameIndex = 0; frameIndex < 4 && bOk; ++frameIndex )
                bOk = renderSceneFrame( renderer, device.get(), stage._scene, sw::float4{ 0.0f, 0.0f, 0.0f, 1.0f } );
            bOk                          = bOk && stage._material->setParameter( nullptr, sw::hashed_string( "color" ), "0.02 0.02 1.0 1.0" );
            sw::CameraComponent* pCamera = stage._scene.getActiveGameCamera();
            bOk                          = bOk && pCamera != nullptr;
            if ( bOk && bCut )
                pCamera->markCut();
            bOk = bOk && renderSceneFrame( renderer, device.get(), stage._scene, sw::float4{ 0.0f, 0.0f, 0.0f, 1.0f } );
            test::RHITestImage image;
            bOk = bOk && image.readTransient( renderer, "TaaColor" );
            if ( bOk )
            {
                const test::Rgba8 corner = image.getPixel( 0, 0 );
                int64             sum    = 0;
                uint32            count  = 0;
                for ( uint32 y = 0; y < image.getHeight(); ++y )
                {
                    for ( uint32 x = 0; x < image.getWidth(); ++x )
                    {
                        const test::Rgba8 pixel = image.getPixel( x, y );
                        if ( test::RHITestImage::getColorDistance( pixel, corner ) < 24 )
                            continue;
                        sum += static_cast<int64>( pixel._r ) - static_cast<int64>( pixel._b );
                        ++count;
                    }
                }
                bOk                = count > 100;
                arrDiff[caseIndex] = count > 0 ? sum / static_cast<int64>( count ) : 0;
            }
            renderer.shutdown();
        }
        SW_EXPECT_TRUE_MSG( bOk, ( label + "디퍼드 TAA 그리기 · 되읽기" ).c_str() );
        if ( bOk == false )
            continue;
        SW_LOG_INFO( "%#TaaColor R-B without cut %#, with cut %#", label, arrDiff[0], arrDiff[1] );
        SW_EXPECT_TRUE_MSG( arrDiff[0] > 0, ( label + "컷 없이도 지난 색이 남지 않았다 — 시험이 TAA 기록을 재지 못한다 (R-B " + sw::to_string( arrDiff[0] ) + ")" ).c_str() );
        SW_EXPECT_TRUE_MSG( arrDiff[1] < -20, ( label + "컷 프레임에 지난 색이 섞였다 (R-B " + sw::to_string( arrDiff[1] ) + ")" ).c_str() );
    }
    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the camera-cut TAA test" );
}

/**
 * @brief [RenderPassGpuTest] 초상화 렌더러는 프리팹을 따로 떨어진 스튜디오(자기 씬 · 카메라 · 조명 · 렌더러)에서 그리고, 그 그림에 대상이 화면 가운데를 채운다(4 백엔드)
 * @details 활성 씬은 건드리지 않는다 — 씬 매니저의 활성 씬이 그대로이고 스튜디오 오브젝트가 그 씬에 생기지 않는다. 경계 구에 맞춘 카메라라 대상이 가운데에
 *          있고 가장자리는 배경이다.
 */
SW_TEST_CASE( RenderPassGpuTest, PortraitRendererDrawsAPrefabInIsolation )
{
    constexpr const utf8* kPrefab = "game/abilityarena/prefabs/player.prefab.xml";
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string     label        = sw::string( device->getBackendName() ) + ": ";
        sw::SceneManager&    sceneManager = sw::engine::getSceneManager();
        const sw::Scene*     pActive      = sceneManager.getActiveScene();
        const size_t         activeCount  = pActive != nullptr && pActive->getObjectManager() != nullptr ? pActive->getObjectManager()->getAllGameObjects().size() : 0;
        sw::PortraitRenderer portrait;
        bool                 bOk = portrait.initialize( device.get() );
        sw::PortraitRequest  request;
        request._prefabPath = kPrefab;
        request._width      = 96;
        request._height     = 96;
        sw::vector<uint8> rgba;
        bOk = bOk && portrait.renderPrefab( request, rgba );
        portrait.shutdown();
        SW_EXPECT_TRUE_MSG( bOk, ( label + "초상화 그리기" ).c_str() );
        SW_EXPECT_TRUE( sceneManager.getActiveScene() == pActive );
        const size_t activeCountAfter = pActive != nullptr && pActive->getObjectManager() != nullptr ? pActive->getObjectManager()->getAllGameObjects().size() : 0;
        SW_EXPECT_EQUAL( activeCount, activeCountAfter );
        if ( bOk == false )
            continue;
        SW_ASSERT_EQUAL( size_t{ 96u * 96u * 4u }, rgba.size() );
        // 가운데 사분의 일에 대상(배경과 다른 픽셀)이 많고, 맨 가장자리 줄은 배경이다.
        const uint8* pCorner     = rgba.data();
        uint32       centerDrawn = 0;
        uint32       borderDrawn = 0;
        for ( uint32 y = 0; y < 96; ++y )
        {
            for ( uint32 x = 0; x < 96; ++x )
            {
                const uint8* pPixel   = rgba.data() + ( static_cast<size_t>( y ) * 96 + x ) * 4;
                const int32  distance = sw::MathUtil::abs( static_cast<int32>( pPixel[0] ) - pCorner[0] ) + sw::MathUtil::abs( static_cast<int32>( pPixel[1] ) - pCorner[1] ) +
                                       sw::MathUtil::abs( static_cast<int32>( pPixel[2] ) - pCorner[2] );
                if ( distance < 24 )
                    continue;
                const bool bCenter = 24 <= x && x < 72 && 24 <= y && y < 72;
                const bool bBorder = x == 0 || y == 0 || x == 95 || y == 95;
                centerDrawn += bCenter ? 1u : 0u;
                borderDrawn += bBorder ? 1u : 0u;
            }
        }
        SW_LOG_INFO( "%#portrait center drawn %# / 2304, border drawn %#", label, centerDrawn, borderDrawn );
        // 사람 모양이라 가로로 좁다 — 가운데 사분의 일(2304 픽셀)의 일부를 채운다.
        SW_EXPECT_TRUE_MSG( centerDrawn > 150, ( label + "대상이 가운데에 없다 (" + sw::to_string( centerDrawn ) + ")" ).c_str() );
        SW_EXPECT_TRUE_MSG( borderDrawn < 20, ( label + "대상이 화면을 넘친다 (" + sw::to_string( borderDrawn ) + ")" ).c_str() );
    }
    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the portrait test" );
}

/**
 * @brief [RenderPassGpuTest] 군중 공유 · VAT 의 그림이 캐릭터마다 스키닝한 그림과 같다(네 백엔드)
 * @details 스킨드 큐브(위쪽 정점 = bone1) 셋이 반복 클립(bone1 사인 회전)을 0 · 0.25 · 0.5 초부터 재생한다. 변형 칸 넷(폭 0.25 초)의 가운데라
 *          (A) 캐릭터마다 사본 · 포즈 · 팔레트(공유 끔)와 (B) 묶음 공유(묶음 셋, 결과 구간 셋)가 같은 그림이어야 하고, (C) 모두 VAT(15 fps 표의
 *          정확한 프레임 시각)로 그려도 같아야 한다. (D) 클립 없는 바인드 포즈와는 달라야 한다 — 같으면 팔레트 · 표가 GPU 에 닿지 않는다.
 *          (B) 가 지면 묶음 팔레트를 싣는 쪽(`GpuSceneBuilder::collectSkinPalettes`) · 인스턴스 표(meshskin.hlsl), (C) 가 지면 VAT 정점 셰이더 경로다.
 */
SW_TEST_CASE( RenderPassGpuTest, CrowdSharingAndVertexAnimationMatchPerUnitSkinning )
{
    struct Snapshot
    {
        uint32             _drawnCount{ 0 };
        bool               _bOk{ false };
        test::RHITestImage _image;
    };
    auto countDifferentPixels = []( const Snapshot& a, const Snapshot& b ) -> uint32
    {
        if ( a._image.getWidth() != b._image.getWidth() || a._image.getHeight() != b._image.getHeight() )
            return 0xFFFFFFFFu;
        uint32 count{ 0 };
        for ( uint32 y = 0; y < a._image.getHeight(); ++y )
        {
            for ( uint32 x = 0; x < a._image.getWidth(); ++x )
            {
                const test::Rgba8 pixelA = a._image.getPixel( x, y );
                const test::Rgba8 pixelB = b._image.getPixel( x, y );
                const auto        isFar  = []( uint8 lhs, uint8 rhs )
                { return lhs > rhs + 12 || rhs > lhs + 12; };
                if ( isFar( pixelA._r, pixelB._r ) || isFar( pixelA._g, pixelB._g ) || isFar( pixelA._b, pixelB._b ) )
                    ++count;
            }
        }
        return count;
    };

    enum class CrowdCase : uint8
    {
        PerUnit,
        Shared,
        VertexAnimation,
        BindPose,
    };

    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::shared_ptr<sw::Skeleton> skeleton = sw::make_shared<sw::Skeleton>( test::makeChainSkeleton( 2 ) );
    // 클립 — 반복, 길이 1 초, bone1 이 Z 축으로 크게 돈다.
    const sw::string folder = test::makeTempPath( "crowdgpuclips" );
    {
        sw::AnimClip clip;
        clip.setName( sw::hashed_string( "Wave" ) );
        clip.setLooping( true );
        SW_ASSERT_TRUE( clip.compressFrom( test::makeChainRawClip( *skeleton, 31, 30.0f, 0.9f ), sw::RawAnimCodec::getInstance(), sw::AnimCodecSettings{}, nullptr ) );
        SW_ASSERT_TRUE( clip.saveToFile( sw::FileUtil::joinPath( folder, "wave.animclip" ) ) );
    }
    sw::shared_ptr<sw::Mesh> bindCube = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( bindCube.get() );
    sw::vector<sw::MeshSkinVertex> listSkin;
    for ( const sw::RHIVertex& vertex : bindCube->getVertices() )
    {
        sw::MeshSkinVertex skin{};
        skin._arrJoint[0] = vertex._arrPosition[1] > 0.0f ? 1u : 0u;
        listSkin.push_back( skin );
    }
    sw::shared_ptr<sw::Mesh> skinnedCube = sw::Mesh::create();
    skinnedCube->setVertices( bindCube->getVertices() );
    skinnedCube->setSkin( listSkin, 2 );

    const float32 arrStart[3] = { 0.0f, 0.25f, 0.5f };
    auto          renderCase  = [&]( test::RHITestDevice& device, CrowdCase crowdCase ) -> Snapshot
    {
        Snapshot          result{};
        sw::FrameRenderer renderer;
        if ( renderer.initialize( device.get() ) == false || renderer.isReady() == false )
            return result;
        sw::Scene scene( "CrowdScene" );
        if ( scene.ensureDefaultCameras() == false )
            return result;
        sw::GameObjectManager& manager = *scene.getObjectManager();
        for ( uint32 index = 0; index < 3; ++index )
        {
            const sw::string name    = sw::string( "Crowd" ) + sw::to_string( index ).c_str();
            sw::GameObject*  pObject = manager.createGameObject( sw::hashed_string( name ) );
            if ( pObject == nullptr )
                return result;
            sw::SkeletalMeshComponent* pUnit = pObject->addComponent<sw::SkeletalMeshComponent>();
            if ( pUnit == nullptr )
                return result;
            pUnit->setShareCrowdPose( crowdCase == CrowdCase::Shared || crowdCase == CrowdCase::VertexAnimation );
            pUnit->setMesh( skinnedCube );
            pUnit->resolveRenderAssets();
            pUnit->setSkeleton( skeleton );
            pUnit->setBoundsRadius( 2.0f );
            pUnit->setLocalPosition( sw::float3{ -1.6f + 1.6f * static_cast<float32>( index ), 1.0f, 0.0f } );
            if ( crowdCase == CrowdCase::VertexAnimation )
            {
                sw::AnimationLodState farState{};
                farState._bVertexAnimation = SW_TRUE;
                pUnit->applyAnimationLod( farState );
            }
            if ( crowdCase == CrowdCase::BindPose )
                continue;
            sw::SkeletalAnimatorComponent* pAnimator = pObject->addComponent<sw::SkeletalAnimatorComponent>();
            if ( pAnimator == nullptr )
                return result;
            pAnimator->setClipFolder( folder );
            pAnimator->setInitialState( "Wave" );
            pAnimator->setInitialTime( arrStart[index] );
            pAnimator->dispatchBeginPlay();
        }
        manager.flushSceneTransforms();
        const sw::float4 clear{ 0.02f, 0.02f, 0.05f, 1.0f };
        for ( uint32 frame = 0; frame < 4; ++frame )
        {
            // 팔레트 · 묶음은 애니메이션 시스템이 틱 뒤에 만든다 — 시험은 틱 대신 평가만 부른다(시간은 흐르지 않는다).
            manager.getAnimationSystem().evaluate( 0.0f );
            device->beginFrame( clear );
            if ( renderer.execute( device.get(), &scene ) == false )
                return result;
            device->endFrame( false, false );
            device->waitIdle();
        }
        if ( result._image.readTransient( renderer, "SceneColor" ) == false )
            return result;
        for ( uint32 y = 0; y < result._image.getHeight(); ++y )
        {
            for ( uint32 x = 0; x < result._image.getWidth(); ++x )
            {
                if ( test::RHITestImage::isDefaultClearBackground( result._image.getPixel( x, y ) ) == false )
                    ++result._drawnCount;
            }
        }
        result._bOk = result._drawnCount > 0;
        return result;
    };

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        if ( device->getCapabilities()._bGpuMeshMorph == SW_FALSE )
            continue;
        const sw::string label     = sw::string( "backend " ) + sw::to_string( static_cast<uint32>( device.getBackend() ) );
        const Snapshot   perUnit   = renderCase( device, CrowdCase::PerUnit );
        const Snapshot   shared    = renderCase( device, CrowdCase::Shared );
        const Snapshot   vertexAni = renderCase( device, CrowdCase::VertexAnimation );
        const Snapshot   bindPose  = renderCase( device, CrowdCase::BindPose );
        const bool       bAllRead  = perUnit._bOk && shared._bOk && vertexAni._bOk && bindPose._bOk;
        SW_EXPECT_TRUE_MSG( bAllRead, ( label + ": 그림을 못 읽었다" ).c_str() );
        if ( bAllRead == false )
            continue;
        const uint32 tolerance  = perUnit._drawnCount / 50 + 8;
        const uint32 sharedDiff = countDifferentPixels( perUnit, shared );
        const uint32 vatDiff    = countDifferentPixels( perUnit, vertexAni );
        const uint32 poseDiff   = countDifferentPixels( perUnit, bindPose );
        SW_EXPECT_TRUE_MSG( poseDiff > perUnit._drawnCount / 10,
                            ( label + ": 클립을 재생했는데 바인드 포즈와 같다 (달라진 픽셀 " + sw::to_string( poseDiff ) + ")" ).c_str() );
        SW_EXPECT_TRUE_MSG( sharedDiff <= tolerance, ( label + ": 묶음 공유 그림이 캐릭터마다 스키닝한 그림과 다르다 (달라진 픽셀 " + sw::to_string( sharedDiff ) +
                                                       " / 그려진 " + sw::to_string( perUnit._drawnCount ) + ")" )
                                                         .c_str() );
        SW_EXPECT_TRUE_MSG( vatDiff <= tolerance, ( label + ": VAT 그림이 캐릭터마다 스키닝한 그림과 다르다 (달라진 픽셀 " + sw::to_string( vatDiff ) + " / 그려진 " +
                                                    sw::to_string( perUnit._drawnCount ) + ")" )
                                                      .c_str() );
    }
    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the crowd sharing test" );
}

/**
 * @brief [RenderPassGpuTest] 구조 버퍼를 앞에서부터 일부만 갱신하면 원본은 그 크기만 읽힌다 — 버퍼보다 짧은 원본의 뒤를 넘어 읽지 않는다
 * @details 용량을 남겨 두는 풀(모프 · 스킨 풀)은 내용이 줄어도 버퍼를 다시 만들지 않고 앞에서부터 짧게 올린다. DX11 은 이 "오프셋 0 · 조각 하나" 를
 *          상자 없는 `UpdateSubresource` 로 보냈는데, 상자가 없으면 드라이버가 버퍼 **전체** 길이를 원본에서 읽는다 — 원본 뒤를 넘어 읽다
 *          드라이버 안에서 죽었다(Shooter3D 에서 스켈레톤 시체를 걷을 때). 원본을 페이지 끝에 붙이고 다음 페이지를 접근 불가로 두면, 한 바이트라도
 *          넘어 읽는 순간 죽는다.
 */
SW_TEST_CASE( RenderPassGpuTest, PartialStructuredBufferUploadReadsOnlyTheSourceRange )
{
#if SW_PLATFORM_WINDOWS
    SYSTEM_INFO sysInfo{};
    GetSystemInfo( &sysInfo );
    const size_t pageSize = static_cast<size_t>( sysInfo.dwPageSize );
    uint8*       pBase    = static_cast<uint8*>( VirtualAlloc( nullptr, pageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE ) );
    SW_ASSERT_TRUE( pBase != nullptr );
    DWORD oldProtect{ 0 };
    SW_ASSERT_TRUE( VirtualProtect( pBase + pageSize, pageSize, PAGE_NOACCESS, &oldProtect ) != 0 );
    const uint32 sourceSize = 256;
    uint8*       pSource    = pBase + pageSize - sourceSize; // 원본의 끝 = 접근 불가 페이지의 시작
    for ( uint32 byteIndex = 0; byteIndex < sourceSize; ++byteIndex )
    {
        pSource[byteIndex] = static_cast<uint8>( byteIndex );
    }

    test::RHIBackendSweep sweep( { sw::RHIBackend::DirectX11, sw::RHIBackend::DirectX12, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL } );
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pFactory = device.get()->getResourceFactory();
        // 원본보다 훨씬 큰 버퍼(64 KB) — 풀이 용량을 남겨 둔 모양.
        const sw::RHIBufferHandle buffer = pFactory->createStructuredBuffer( 16, 4096 );
        SW_ASSERT_TRUE( buffer != 0 );
        pFactory->updateStructuredBufferRange( buffer, pSource, sourceSize, 0 );
        pFactory->updateStructuredBuffer( buffer, pSource, sourceSize );
        device.get()->waitIdle();
        pFactory->destroyBuffer( buffer );
    }
    VirtualFree( pBase, 0, MEM_RELEASE );
    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the partial structured buffer upload test" );
#else
    SW_TEST_SKIP( "Guard pages are set up with the Windows memory API" );
#endif
}

/**
 * @brief [RenderPassGpuTest] 물 정점 셰이더의 파도 함수(gerstner.hlsli)가 CPU 질의(WaterWaveMath)와 같은 변위를 낸다 — 네 백엔드
 * @details 컴퓨트 프로브(common/shaders/waterwaveprobe.hlsl)가 water.hlsl 과 같은 `swComputeGerstnerDisplacement` 를 표본 32 자리에서 불러 float 비트를
 *          RGBA8 텍스처에 싣고, 읽어 CPU 값과 견준다. GPU 의 sin · cos 는 정확도가 낮아 비트가 같지는 않다 — 1 mm 안이면 같은 식이다.
 *          식 하나(항의 순서 · Q 나누기 · 분산)라도 갈리면 cm 단위로 벌어진다.
 */
SW_TEST_CASE( RenderPassGpuTest, WaterWaveShaderMatchesCpu )
{
    constexpr uint32  kSampleCount                                = 32;
    constexpr uint32  kTexelPerRow                                = 8;
    constexpr float32 kTime                                       = 2.75f;
    constexpr float32 kGravity                                    = sw::constant::kDefaultGravity;
    const sw::float4  arrWave[sw::shaderslot::kGerstnerWaveCount] = {
        sw::GerstnerWave{ 0.3f, 12.0f, 0.35f, 0.8f}
            .toVector(),
        sw::GerstnerWave{ 2.1f,  5.0f, 0.12f, 0.6f}
            .toVector(),
        sw::GerstnerWave{-1.0f,  2.5f, 0.05f, 0.5f}
            .toVector(),
        sw::float4{ 0.0f,  1.0f,  0.0f, 0.0f}
    };

    uint32                comparedCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        sw::IRHIResourceFactory* pResource = device->getResourceFactory();
        const utf8*              pName     = device->getBackendName();

        sw::RHITextureDesc texDesc{};
        texDesc._width                           = kTexelPerRow;
        texDesc._height                          = kSampleCount;
        texDesc._mipLevels                       = 1;
        texDesc._format                          = sw::RHIFormat::R8G8B8A8_UNORM;
        texDesc._bIsShaderResource               = SW_TRUE;
        texDesc._bIsUnorderedAccess              = SW_TRUE;
        const sw::RHITextureHandle       texture = pResource->createTexture2D( texDesc );
        const sw::RHIDescriptorIndex     uav     = texture != 0 ? pResource->registerBindlessTextureUav( texture ) : sw::kInvalidDescriptorIndex;
        const sw::RHIPipelineStateHandle pso     = pResource->createComputePipelineState( "common/shaders/waterwaveprobe.hlsl" );
        SW_EXPECT_TRUE_MSG( texture != 0 && uav != sw::kInvalidDescriptorIndex && pso != 0, pName );

        if ( texture != 0 && uav != sw::kInvalidDescriptorIndex && pso != 0 )
        {
            sw::unique_ptr<sw::IRHICommandList> cmdList = device->createCommandList();
            SW_ASSERT_NOT_NULL( cmdList );
            uint32 arrRoot[16]{};
            arrRoot[0] = device->supportsNativeBindlessSampling() ? static_cast<uint32>( uav ) : 0u;
            arrRoot[1] = kSampleCount;
            std::memcpy( &arrRoot[2], &kTime, sizeof( float32 ) );
            std::memcpy( &arrRoot[3], &kGravity, sizeof( float32 ) );
            std::memcpy( &arrRoot[4], arrWave, sizeof( sw::float4 ) * 3 );
            cmdList->beginCommandList();
            cmdList->prepareTextureForUnorderedAccess( texture );
            cmdList->setComputePipelineState( pso );
            cmdList->bindComputeUav( uav, sw::shaderslot::kComputeTextureUav0 );
            cmdList->setComputeRootConstants( 0, 16, arrRoot, 0 );
            cmdList->dispatchCompute( 1, 1, 1 );
            cmdList->endCommandList();
            device->executeCommandListImmediate( cmdList.get() );
            device->waitIdle();

            sw::vector<uint8>     bytes;
            sw::RHITextureMipSpan layout{};
            const bool            bRead = pResource->readbackTexture2D( texture, 0, 0, bytes, layout );
            SW_EXPECT_TRUE_MSG( bRead && layout._rowBytes >= kTexelPerRow * 4 && bytes.size() >= static_cast<size_t>( layout._rowBytes ) * kSampleCount, pName );
            if ( bRead && layout._rowBytes >= kTexelPerRow * 4 && bytes.size() >= static_cast<size_t>( layout._rowBytes ) * kSampleCount )
            {
                float32 worstError{ 0.0f };
                for ( uint32 sampleIndex = 0; sampleIndex < kSampleCount; ++sampleIndex )
                {
                    const uint8* pRow = bytes.data() + static_cast<size_t>( sampleIndex ) * layout._rowBytes;
                    float32      arrGpu[3]{};
                    for ( uint32 component = 0; component < 3; ++component )
                    {
                        // 텍셀 하나 = 16 비트(G 위 · A 아래 바이트). 성분 하나 = 아래 · 위 16 비트 텍셀 둘.
                        const uint8* pLow  = pRow + ( component * 2 ) * 4;
                        const uint8* pHigh = pRow + ( component * 2 + 1 ) * 4;
                        const uint32 bits  = ( static_cast<uint32>( pLow[1] ) << 8 | pLow[3] ) | ( ( static_cast<uint32>( pHigh[1] ) << 8 | pHigh[3] ) << 16 );
                        std::memcpy( &arrGpu[component], &bits, sizeof( float32 ) );
                    }
                    const sw::float2 origin{ -20.0f + static_cast<float32>( sampleIndex % 8u ) * 5.25f, -15.0f + static_cast<float32>( sampleIndex / 8u ) * 4.125f };
                    const sw::float3 cpu = sw::WaterWaveMath::computeDisplacement( origin, kTime, kGravity, arrWave );
                    worstError           = sw::MathUtil::max( worstError, sw::MathUtil::abs( cpu._x - arrGpu[0] ) );
                    worstError           = sw::MathUtil::max( worstError, sw::MathUtil::abs( cpu._y - arrGpu[1] ) );
                    worstError           = sw::MathUtil::max( worstError, sw::MathUtil::abs( cpu._z - arrGpu[2] ) );
                }
                SW_EXPECT_TRUE_MSG( worstError < 1.0e-3f, ( sw::string( pName ) + ": GPU wave displacement differs from the CPU by " + sw::to_string( worstError ) ).c_str() );
                ++comparedCount;
            }
        }

        if ( uav != sw::kInvalidDescriptorIndex )
            pResource->unregisterBindlessUav( uav );
        if ( texture != 0 )
            pResource->destroyTexture( texture );
        if ( pso != 0 )
            pResource->destroyPipelineState( pso );
    }
    if ( comparedCount == 0 )
        SW_TEST_SKIP( "No RHI backend could run the water wave probe" );
}

/**
 * @brief [RenderPassGpuTest] 머티리얼을 정점 셰이더만 읽는 셰이더(물 · 식생)도 셰이더의 원소 레이아웃으로 맞춰진다 — 네 백엔드
 * @details 머티리얼 스키마는 픽셀 스테이지 리플렉션에서 찾는다. 물 · 식생은 GL 이 두 단계의 구조버퍼 읽기를 거절하므로 머티리얼을 정점 셰이더만
 *          읽는다 — 픽셀에서 못 찾고 멈추면 stride 0 · XML 순서 패킹이 되어 GpuScene 이 원소마다 엉뚱한 자리를 읽는다(파도 · 바람 값이 섞인다).
 */
SW_TEST_CASE( RenderPassGpuTest, VertexStageMaterialSchemaIsUsed )
{
    uint32                checkedCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::shared_ptr<sw::Material> water = sw::Material::create();
        SW_ASSERT_TRUE( water->initialize( device.get(), "engine/materials/water.material" ) );
        SW_EXPECT_TRUE_MSG( water->ensureShaderLayout( device.get() ), device->getBackendName() );
        // water.hlsl 의 SwMaterialData — float4 아홉(파도 중력 waveParams 포함) = 144 바이트, wave1 은 16 바이트 자리다.
        SW_EXPECT_EQUAL( 144u, water->getElementStride() );
        const sw::MaterialProperty* pWave = water->findProperty( sw::hashed_string( "wave1" ) );
        SW_ASSERT_NOT_NULL( pWave );
        SW_EXPECT_EQUAL( 16u, pWave->_offset );
        ++checkedCount;
    }
    if ( checkedCount == 0 )
        SW_TEST_SKIP( "No RHI backend could reflect the water shader" );
}

/**
 * @brief [RenderPassGpuTest] 픽셀 아트 스프라이트는 자산 픽셀 격자에 붙고(픽셀 스냅) 텍셀 경계가 번지지 않는다(점 필터) — 4 백엔드
 * @details PPU 8 · 배율 4 라 자산 픽셀 하나가 화면 픽셀 4 칸이다(`PixelPerfectCameraComponent`). 8 × 8 줄무늬(흰 · 파랑 번갈아) 스프라이트를
 *          x = 0 · 0.3 · 0.7 자산 픽셀에 놓는다. 스냅이 켜져 있으면 0.3 은 0 과 같은 화면 픽셀에서 시작하고 0.7 은 정확히 4 픽셀(자산 픽셀 하나) 옆이다.
 *          스냅을 끄면 0.3 자산 픽셀 = 1.2 화면 픽셀만큼 밀린다 — 래스터화는 화면 픽셀로만 반올림하므로 스냅이 없으면 아트 픽셀이 어긋난다.
 *          점 필터(`sprite2dpixel.material`)면 스프라이트 안의 모든 픽셀이 두 색 중 하나이고 줄무늬 한 칸이 정확히 4 픽셀이다. 선형 필터면 경계에
 *          섞인 색이 생긴다.
 */
SW_TEST_CASE( RenderPassGpuTest, PixelArtSpritesSnapToTheAssetPixelGrid )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    constexpr const utf8* kStripeTexture = "engine/textures/test/pixelstripes.dds";
    constexpr float32     kPixelsPerUnit = 8.0f;
    constexpr int32       kZoom          = 4;
    const test::Rgba8     white{ 255, 255, 255, 255 };
    const test::Rgba8     blue{ 20, 40, 160, 255 };

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string  label = sw::string( device->getBackendName() ) + ": ";
        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene                        scene( "PixelArtScene" );
        sw::PixelPerfectCameraComponent* pPixel  = nullptr;
        sw::SpriteComponent*             pSprite = nullptr;
        if ( bOk )
        {
            sw::GameObject*      pCameraObject = scene.getObjectManager()->createGameObject( sw::hashed_string( "PixelCamera" ) );
            sw::CameraComponent* pCamera       = pCameraObject->addComponent<sw::CameraComponent>();
            pCamera->setRole( sw::CameraRole::Game );
            pCamera->setLocalPosition( sw::float3{ 0.0f, 0.0f, -4.0f } );
            pPixel = pCameraObject->addComponent<sw::PixelPerfectCameraComponent>();
            bOk    = pPixel != nullptr && scene.ensureDefaultCameras();
        }
        if ( bOk )
        {
            sw::GameObject* pSpriteObject = scene.getObjectManager()->createGameObject( sw::hashed_string( "Stripes" ) );
            pSprite                       = pSpriteObject->addComponent<sw::SpriteComponent>();
            pSprite->setMaterialPath( "engine/materials/sprite2dpixel.material" );
            pSprite->setTextureName( kStripeTexture );
            pSprite->resolveRenderAssets();
            sw::engine::getAssetManager().getMaterialManager().initializePending( device.get() );
            // 첫 프레임으로 그리는 크기를 안다 — 배율 4 가 되게 기준 해상도를 그 크기의 4 분의 1 로 둔다.
            bOk = renderSceneFrame( renderer, device.get(), scene, sw::float4{ 0.0f, 0.0f, 0.0f, 1.0f } );
        }
        test::RHITestImage firstImage;
        bOk = bOk && firstImage.readTransient( renderer, "SceneColor" );
        if ( bOk == false )
        {
            SW_EXPECT_TRUE_MSG( bOk, ( label + "준비 실패" ).c_str() );
            continue;
        }
        const uint32 width  = firstImage.getWidth();
        const uint32 height = firstImage.getHeight();
        pPixel->setPixelsPerUnit( kPixelsPerUnit );
        pPixel->setReferenceResolution( sw::float2{ static_cast<float32>( width / kZoom ), static_cast<float32>( height / kZoom ) } );
        const sw::PixelPerfectLayout layout = pPixel->applyToCamera( width, height );
        SW_EXPECT_EQUAL( kZoom, layout._zoom );

        // 스프라이트를 x(자산 픽셀)에 놓고 그린 뒤 가운데 줄에서 그려진 구간의 시작 픽셀 · 그 줄을 돌려준다.
        const int32 row        = static_cast<int32>( height / 2 );
        const auto  renderSpan = [&]( float32 assetPixelX, sw::vector<test::Rgba8>& outListPixel )
        {
            pSprite->setLocalPosition( sw::float3{ assetPixelX / kPixelsPerUnit, 0.0f, 0.0f } );
            scene.getObjectManager()->flushSceneTransforms();
            sw::engine::getAssetManager().getMaterialManager().initializePending( device.get() );
            for ( uint32 frame = 0; frame < 3; ++frame )
                (void)renderSceneFrame( renderer, device.get(), scene, sw::float4{ 0.0f, 0.0f, 0.0f, 1.0f } );
            test::RHITestImage image;
            outListPixel.clear();
            if ( image.readTransient( renderer, "SceneColor" ) == false )
                return -1;
            int32 start = -1;
            for ( uint32 x = 0; x < image.getWidth(); ++x )
            {
                const test::Rgba8 pixel = image.getPixel( x, static_cast<uint32>( row ) );
                if ( test::RHITestImage::isDefaultClearBackground( pixel ) )
                    continue;
                if ( start < 0 )
                    start = static_cast<int32>( x );
                outListPixel.push_back( pixel );
            }
            return start;
        };

        sw::vector<test::Rgba8> listAtZero;
        sw::vector<test::Rgba8> listScratch;
        const int32             startZero = renderSpan( 0.0f, listAtZero );
        const int32             startNear = renderSpan( 0.3f, listScratch );
        const int32             startNext = renderSpan( 0.7f, listScratch );
        SW_EXPECT_TRUE_MSG( startZero >= 0, ( label + "스프라이트가 그려지지 않았다" ).c_str() );
        SW_EXPECT_TRUE_MSG( startNear == startZero,
                            ( label + "스냅을 켰는데 0.3 자산 픽셀이 다른 화면 픽셀에서 시작한다 (" + sw::to_string( startNear ) + " vs " + sw::to_string( startZero ) + ")" ).c_str() );
        SW_EXPECT_TRUE_MSG( startNext == startZero + kZoom,
                            ( label + "0.7 자산 픽셀이 자산 픽셀 하나(4 화면 픽셀) 옆이 아니다 (" + sw::to_string( startNext ) + " vs " + sw::to_string( startZero ) + ")" ).c_str() );

        // 점 필터: 스프라이트 안의 모든 픽셀이 두 색 중 하나이고 줄무늬 한 칸이 배율만큼(4 픽셀)이다.
        uint32 blendedCount = 0;
        int32  runLength    = 0;
        bool   bRunsExact   = true;
        for ( size_t index = 0; index < listAtZero.size(); ++index )
        {
            const test::Rgba8& pixel  = listAtZero[index];
            const bool         bWhite = test::RHITestImage::getColorDistance( pixel, white ) < 24;
            const bool         bBlue  = test::RHITestImage::getColorDistance( pixel, blue ) < 24;
            if ( bWhite == false && bBlue == false )
                ++blendedCount;
            const bool bSameAsPrevious = index > 0 && test::RHITestImage::getColorDistance( pixel, listAtZero[index - 1] ) < 24;
            if ( index > 0 && bSameAsPrevious == false )
            {
                if ( runLength != kZoom )
                    bRunsExact = false;
                runLength = 0;
            }
            ++runLength;
        }
        SW_EXPECT_TRUE_MSG( blendedCount == 0, ( label + "점 필터인데 섞인 색이 " + sw::to_string( blendedCount ) + " 픽셀 있다" ).c_str() );
        SW_EXPECT_TRUE_MSG( bRunsExact && listAtZero.size() == static_cast<size_t>( 8 * kZoom ),
                            ( label + "줄무늬 칸이 4 픽셀씩이 아니다 (폭 " + sw::to_string( listAtZero.size() ) + ")" ).c_str() );

        // 스냅을 끄면 0.3 자산 픽셀(1.2 화면 픽셀)만큼 밀린다 — 래스터화만으로는 아트 픽셀이 격자에 서지 않는다.
        pPixel->setPixelSnapping( false );
        (void)pPixel->applyToCamera( width, height );
        const int32 startUnsnapped = renderSpan( 0.3f, listScratch );
        SW_EXPECT_TRUE_MSG( startUnsnapped == startZero + 1,
                            ( label + "스냅을 끈 0.3 자산 픽셀이 1 화면 픽셀 밀리지 않았다 (" + sw::to_string( startUnsnapped ) + ")" ).c_str() );

        // 선형 필터(기본 스프라이트 머티리얼)면 줄무늬 경계에 섞인 색이 생긴다 — 점 필터가 실제로 일을 했다는 대조.
        pSprite->setMaterialPath( "engine/materials/sprite2d.material" );
        sw::vector<test::Rgba8> listLinear;
        (void)renderSpan( 0.0f, listLinear );
        uint32 linearBlended = 0;
        for ( const test::Rgba8& pixel : listLinear )
        {
            if ( test::RHITestImage::getColorDistance( pixel, white ) >= 24 && test::RHITestImage::getColorDistance( pixel, blue ) >= 24 )
                ++linearBlended;
        }
        SW_EXPECT_TRUE_MSG( linearBlended > 0, ( label + "선형 필터인데 섞인 색이 없다 — 점 필터 대조가 눈을 감았다" ).c_str() );
        SW_LOG_INFO( "%#pixel art: start %# / %# / %# (unsnapped %#), point-filter blended %#, linear blended %#", label, startZero, startNear, startNext,
                     startUnsnapped, blendedCount, linearBlended );
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend available for the pixel art test" );
}

/**
 * @brief [RenderPassGpuTest] 2D 점광의 감쇠가 식과 같고, 2D 가림막 뒤는 어둡고 가림막 안쪽은 밝다 — 4 백엔드
 * @details 직교 카메라(높이 4) 앞의 흰 빛 받는 스프라이트(`sprite2dlit.material`) 6 × 6 을 원점의 점광(바깥 반경 2, 안 0, 지수 1)이 비춘다. 가운데 줄의 픽셀을
 *          월드 X 로 옮겨 `PointLight2DComponent::computeAttenuation` × 255 와 견준다. 그다음 (1, 0) 에 0.2 × 1 상자 가림막을 두면 x > 1.1 은 0 이 되고,
 *          가림막 안(x = 1)은 빛 쪽 변에 가려지지 않아 식 그대로다(자기 그림자 없음). 빛 쪽(x < 0.9)은 바뀌지 않는다.
 */
SW_TEST_CASE( RenderPassGpuTest, Light2DFalloffAndShadowOnEveryBackend )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    constexpr float32 kOrthoHeight = 4.0f;
    constexpr float32 kOuterRadius = 2.0f;

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string  label = sw::string( device->getBackendName() ) + ": ";
        sw::FrameRenderer renderer;
        bool              bOk = renderer.initialize( device.get() ) && renderer.isReady();

        sw::Scene                    scene( "Light2DGpuScene" );
        sw::ShadowCaster2DComponent* pCaster = nullptr;
        if ( bOk )
        {
            sw::GameObject*      pCameraObject = scene.getObjectManager()->createGameObject( sw::hashed_string( "Camera2D" ) );
            sw::CameraComponent* pCamera       = pCameraObject->addComponent<sw::CameraComponent>();
            pCamera->setRole( sw::CameraRole::Game );
            pCamera->setOrthographic( true );
            pCamera->setOrthoHeight( kOrthoHeight );
            pCamera->setLocalPosition( sw::float3{ 0.0f, 0.0f, -4.0f } );

            sw::GameObject*      pGround = scene.getObjectManager()->createGameObject( sw::hashed_string( "LitGround" ) );
            sw::SpriteComponent* pSprite = pGround->addComponent<sw::SpriteComponent>();
            pSprite->setMaterialPath( "engine/materials/sprite2dlit.material" );
            pSprite->setLocalScale( sw::float3{ 6.0f, 6.0f, 1.0f } );
            pSprite->resolveRenderAssets();

            sw::GameObject*            pLightObject = scene.getObjectManager()->createGameObject( sw::hashed_string( "Lamp" ) );
            sw::PointLight2DComponent* pLight       = pLightObject->addComponent<sw::PointLight2DComponent>();
            pLight->setColor( sw::float3{ 1.0f, 1.0f, 1.0f } );
            pLight->setIntensity( 1.0f );
            pLight->setRadius( 0.0f, kOuterRadius );
            pLight->setFalloffExponent( 1.0f );

            sw::GameObject*     pCasterObject = scene.getObjectManager()->createGameObject( sw::hashed_string( "Pillar" ) );
            sw::SceneComponent* pCasterRoot   = pCasterObject->addComponent<sw::SceneComponent>();
            pCasterRoot->setLocalPosition( sw::float3{ 1.0f, 0.0f, 0.0f } );
            pCaster = pCasterObject->addComponent<sw::ShadowCaster2DComponent>();
            pCaster->setSize( sw::float2{ 0.2f, 1.0f } );
            pCaster->setActive( false );

            bOk = scene.ensureDefaultCameras();
            scene.getObjectManager()->flushSceneTransforms();
            sw::engine::getAssetManager().getMaterialManager().initializePending( device.get() );
        }

        // 가운데 줄을 그려 읽고, 픽셀 X → 월드 X 로 옮긴 값의 빨강을 돌려준다.
        const auto renderRow = [&]( sw::vector<float32>& outListWorldX, sw::vector<int32>& outListRed )
        {
            outListWorldX.clear();
            outListRed.clear();
            for ( uint32 frame = 0; frame < 4 && bOk; ++frame )
                bOk = renderSceneFrame( renderer, device.get(), scene, sw::float4{ 0.0f, 0.0f, 0.0f, 1.0f } );
            test::RHITestImage image;
            if ( bOk == false || image.readTransient( renderer, "SceneColor" ) == false )
                return false;
            const float32 unit = kOrthoHeight / static_cast<float32>( image.getHeight() );
            const uint32  row  = image.getHeight() / 2;
            for ( uint32 x = 0; x < image.getWidth(); ++x )
            {
                outListWorldX.push_back( ( static_cast<float32>( x ) + 0.5f - 0.5f * static_cast<float32>( image.getWidth() ) ) * unit );
                outListRed.push_back( static_cast<int32>( image.getPixel( x, row )._r ) );
            }
            return true;
        };

        sw::vector<float32> listWorldX;
        sw::vector<int32>   listOpen;
        sw::vector<int32>   listShadowed;
        bOk = bOk && renderRow( listWorldX, listOpen );
        if ( bOk )
        {
            pCaster->setActive( true );
            bOk = renderRow( listWorldX, listShadowed );
        }
        if ( bOk == false )
        {
            SW_EXPECT_TRUE_MSG( bOk, ( label + "그리기 실패" ).c_str() );
            continue;
        }

        int32 worstFalloffError  = 0;
        int32 worstLitSideChange = 0;
        int32 brightestBehind    = 0;
        int32 insideValue        = -1;
        int32 insideExpected     = -1;
        for ( size_t index = 0; index < listWorldX.size(); ++index )
        {
            const float32 worldX   = listWorldX[index];
            const int32   expected = static_cast<int32>( sw::PointLight2DComponent::computeAttenuation( sw::MathUtil::abs( worldX ), 0.0f, kOuterRadius, 1.0f ) * 255.0f + 0.5f );
            worstFalloffError      = sw::MathUtil::max( worstFalloffError, sw::MathUtil::abs( listOpen[index] - expected ) );
            if ( worldX < 0.85f )
                worstLitSideChange = sw::MathUtil::max( worstLitSideChange, sw::MathUtil::abs( listShadowed[index] - listOpen[index] ) );
            if ( worldX > 1.2f )
                brightestBehind = sw::MathUtil::max( brightestBehind, listShadowed[index] );
            if ( insideValue < 0 && worldX >= 0.98f )
            {
                insideValue    = listShadowed[index];
                insideExpected = expected;
            }
        }
        SW_EXPECT_TRUE_MSG( worstFalloffError <= 6, ( label + "감쇠가 식과 다르다 (최대 차이 " + sw::to_string( worstFalloffError ) + "/255)" ).c_str() );
        SW_EXPECT_TRUE_MSG( worstLitSideChange <= 2, ( label + "가림막이 빛 쪽 픽셀을 바꿨다 (" + sw::to_string( worstLitSideChange ) + ")" ).c_str() );
        SW_EXPECT_TRUE_MSG( brightestBehind <= 2, ( label + "가림막 뒤가 밝다 (" + sw::to_string( brightestBehind ) + ")" ).c_str() );
        SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( insideValue - insideExpected ) <= 6,
                            ( label + "가림막 안쪽이 자기 그림자를 받았다 (" + sw::to_string( insideValue ) + " vs " + sw::to_string( insideExpected ) + ")" ).c_str() );
        SW_LOG_INFO( "%#2D light: falloff max error %#, lit side change %#, behind caster max %#, inside caster %# (expected %#)", label, worstFalloffError,
                     worstLitSideChange, brightestBehind, insideValue, insideExpected );
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend available for the 2D light test" );
}

/**
 * @brief [RenderPassGpuTest] 셀 셰이딩(toon.material)의 밝기 단계 수가 램버트(기본 머티리얼)보다 확실히 적다 — 네 백엔드
 * @details 같은 구 · 같은 색 · 같은 빛을 두 머티리얼로 그리고, 그려진 픽셀(모서리 기준 배경 제거)의 밝기 히스토그램에서 1 % 이상이 든 칸을 센다.
 *          램버트는 표면을 따라 고르게 퍼지고, 계단을 칼같이(toony 1) 둔 툰은 빛 · 그늘 두 무리에 몰린다. 툰이 단계 넷을 넘거나 램버트의 절반을
 *          넘으면 계단(linearstep)이 GPU 에 닿지 않은 것이다(머티리얼 버퍼 레이아웃이 어긋나 shadingToony 가 0 으로 읽혀도 그렇다).
 */
SW_TEST_CASE( RenderPassGpuTest, ToonShadingHasFewerBrightnessLevelsThanLit )
{
    constexpr uint32 kMaxToonLevelCount = 4;

    uint32                comparedCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string label = sw::string( device->getBackendName() ) + ": ";
        ToonSphereScene  litScene;
        ToonSphereScene  toonScene;
        SW_ASSERT_TRUE_MSG( litScene.populate( ToonSphereScene::makeLitMaterial() ), ( label + "램버트 무대를 못 만들었다" ).c_str() );
        SW_ASSERT_TRUE_MSG( toonScene.populate( ToonSphereScene::makeToonMaterial( false ) ), ( label + "툰 무대를 못 만들었다" ).c_str() );

        // 씬마다 렌더러를 따로 둔다(한 렌더러의 씬 빌더는 그리던 씬의 수집 캐시를 든다).
        sw::FrameRenderer  litRenderer;
        sw::FrameRenderer  toonRenderer;
        test::RHITestImage litImage;
        test::RHITestImage toonImage;
        const bool         bOk = litRenderer.initialize( device.get() ) && toonRenderer.initialize( device.get() ) &&
                         renderAndReadAttachment( litRenderer, device.get(), litScene._scene, "SceneColor", litImage ) &&
                         renderAndReadAttachment( toonRenderer, device.get(), toonScene._scene, "SceneColor", toonImage );
        SW_EXPECT_TRUE_MSG( bOk, ( label + "그리거나 되읽지 못했다" ).c_str() );
        if ( bOk == false )
            continue;

        const DrawnMask litMask( litImage );
        const DrawnMask toonMask( toonImage );
        SW_EXPECT_TRUE_MSG( litMask._drawnCount > 2000 && toonMask._drawnCount > 2000,
                            ( label + "구가 그려지지 않았다 (램버트 " + sw::to_string( litMask._drawnCount ) + " · 툰 " + sw::to_string( toonMask._drawnCount ) + " px)" ).c_str() );
        if ( litMask._drawnCount <= 2000 || toonMask._drawnCount <= 2000 )
            continue;

        ++comparedCount;
        const uint32 litLevelCount  = countBrightnessLevels( litImage, litMask );
        const uint32 toonLevelCount = countBrightnessLevels( toonImage, toonMask );
        SW_LOG_INFO( "%#brightness levels lit %# · toon %#", label, litLevelCount, toonLevelCount );
        SW_EXPECT_TRUE_MSG( toonLevelCount <= kMaxToonLevelCount,
                            ( label + "툰 구의 밝기 단계가 " + sw::to_string( toonLevelCount ) + " 개다 — 계단이 아니다" ).c_str() );
        SW_EXPECT_TRUE_MSG( toonLevelCount * 2u < litLevelCount,
                            ( label + "툰 " + sw::to_string( toonLevelCount ) + " 단계 · 램버트 " + sw::to_string( litLevelCount ) + " 단계 — 툰이 확실히 적지 않다" ).c_str() );
    }

    if ( comparedCount == 0 )
        SW_TEST_SKIP( "No RHI backend could render the toon shading test" );
}

/**
 * @brief [RenderPassGpuTest] 메시 외곽선 패스(뒤집은 껍질)가 실루엣 둘레에 어두운 고리를 그리고 안쪽은 건드리지 않는다 — 네 백엔드 × 포워드 · 디퍼드
 * @details 같은 툰 구를 외곽선 스위치만 바꿔 그린다. 고리 = 끈 그림의 배경이 켠 그림에서 그려진 픽셀이다.
 *          (1) 고리가 있다 — 둘레 × 두께의 절반 이상. 외곽선을 끈 머티리얼까지 패스가 그리면(머티리얼 거르기가 빠지면) 끈 그림에도 고리가 생겨 0 이 된다.
 *          (2) 고리는 실루엣 띠(구의 화면 반지름 R ~ R + 두께) 안에 있다. (3) 어둡다(외곽선 색 검정, 빛 섞기 0).
 *          (4) 구 안쪽은 같다 — 앞면 컬링이 빠지면 부풀린 껍질의 앞면이 구를 덮는다.
 */
SW_TEST_CASE( RenderPassGpuTest, MeshOutlineDrawsDarkRingAroundSilhouette )
{
    struct PipelineCase
    {
        const utf8* _pPath;
        const utf8* _pColor;
    };
    const PipelineCase kArrCase[] = {
        { "engine/pipeline/forwardpipeline.xml", "SceneColor"},
        {"engine/pipeline/deferredpipeline.xml",   "LitColor"},
    };
    constexpr float32 kOutlineWidth  = 0.01f; // makeToonMaterial 의 화면 높이 비율
    constexpr float64 kMaxRingLuma   = 40.0;
    constexpr uint32  kBandPercent   = 95;
    constexpr uint32  kInteriorRatio = 100; // 안쪽 차이는 구 픽셀의 1 % 이하

    uint32                comparedCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        for ( const PipelineCase& pipelineCase : kArrCase )
        {
            const sw::string label = sw::string( device->getBackendName() ) + " " + pipelineCase._pPath + ": ";
            ToonSphereScene  offScene;
            ToonSphereScene  onScene;
            SW_ASSERT_TRUE_MSG( offScene.populate( ToonSphereScene::makeToonMaterial( false ) ) && onScene.populate( ToonSphereScene::makeToonMaterial( true ) ),
                                ( label + "무대를 못 만들었다" ).c_str() );

            sw::FrameRenderer  offRenderer;
            sw::FrameRenderer  onRenderer;
            test::RHITestImage offImage;
            test::RHITestImage onImage;
            const bool         bOk = offRenderer.initialize( device.get(), pipelineCase._pPath ) && onRenderer.initialize( device.get(), pipelineCase._pPath ) &&
                             renderAndReadAttachment( offRenderer, device.get(), offScene._scene, pipelineCase._pColor, offImage ) &&
                             renderAndReadAttachment( onRenderer, device.get(), onScene._scene, pipelineCase._pColor, onImage );
            SW_EXPECT_TRUE_MSG( bOk, ( label + "그리거나 되읽지 못했다" ).c_str() );
            if ( bOk == false )
                continue;

            const float32     widthPixel = kOutlineWidth * static_cast<float32>( onImage.getHeight() );
            const OutlineRing ring       = OutlineRing::measure( offImage, onImage, widthPixel );
            SW_LOG_INFO( "%#%#", label, ring.describe() );
            SW_EXPECT_TRUE_MSG( ring._sphereCount > 2000, ( label + "구가 그려지지 않았다 — " + ring.describe() ).c_str() );
            if ( ring._sphereCount <= 2000 )
                continue;

            ++comparedCount;
            const float32 expectedRing = 2.0f * sw::MathUtil::kPi * ring._radius * widthPixel;
            SW_EXPECT_TRUE_MSG( static_cast<float32>( ring._ringCount ) >= expectedRing * 0.5f,
                                ( label + "외곽선 고리가 없다(기대 약 " + sw::to_string( expectedRing ) + " px) — " + ring.describe() ).c_str() );
            SW_EXPECT_TRUE_MSG( ring._inBandCount * 100u >= ring._ringCount * kBandPercent, ( label + "고리가 실루엣 띠 밖에 있다 — " + ring.describe() ).c_str() );
            SW_EXPECT_TRUE_MSG( ring._ringMeanLuma <= kMaxRingLuma, ( label + "고리가 어둡지 않다 — " + ring.describe() ).c_str() );
            SW_EXPECT_TRUE_MSG( ring._interiorDiffer * kInteriorRatio <= ring._sphereCount, ( label + "외곽선이 구 안쪽을 덮었다 — " + ring.describe() ).c_str() );
        }
    }

    if ( comparedCount == 0 )
        SW_TEST_SKIP( "No RHI backend could render the mesh outline test" );
}

/**
 * @brief [RenderPassGpuTest] 스킨드 메시의 외곽선이 스키닝된 자리를 따른다 — GPU 스키닝으로 옮긴 구의 고리가 CPU 로 옮긴 정적 구의 고리와 같다(네 백엔드)
 * @details 구의 모든 정점을 본 1 에 묶고 본 1 을 옆으로 옮긴다. 외곽선 껍질이 레스트 정점(입력 스트림)으로 밀면 고리가 옛 자리에 남아
 *          옮긴 구의 고리와 어긋난다. 두 그림의 어두운 고리 마스크(그려졌고 밝기 40 이하)의 차이를 고리 크기와 견준다.
 */
SW_TEST_CASE( RenderPassGpuTest, MeshOutlineFollowsSkinnedPose )
{
    /**
     * @class ShiftTask
     * @brief 기본 포즈 단계에서 본 1 을 옆으로 옮기는 일입니다.
     */
    class ShiftTask final : public sw::IAnimationPhaseTask
    {
    public:
        explicit ShiftTask( const sw::float3& offset )
            : _offset{ offset }
        {
        }
        bool isAnimationActive() const override { return true; }
        void runAnimationPhase( sw::AnimationPhase phase, sw::SkeletalMeshComponent& unit, const sw::AnimationFrameContext& context ) override
        {
            (void)context;
            if ( phase != sw::AnimationPhase::BasePose )
                return;
            sw::BoneTransform bone = unit.getLocalPose().getBoneTransform( 1 );
            bone._translation      = _offset;
            unit.getLocalPose().setBoneTransform( 1, bone );
        }
        sw::float3 _offset;
    };

    /// @brief 어두운 고리 마스크(그려졌고 밝기 40 이하)를 셉니다. @p pOther 가 있으면 둘 중 한쪽만 고리인 픽셀 수입니다.
    auto countDarkRing = []( const test::RHITestImage& image, const test::RHITestImage* pOther ) -> uint32
    {
        const test::Rgba8 corner      = image.getPixel( 0, 0 );
        const test::Rgba8 otherCorner = pOther != nullptr ? pOther->getPixel( 0, 0 ) : corner;
        uint32            count{ 0 };
        for ( uint32 y = 0; y < image.getHeight(); ++y )
        {
            for ( uint32 x = 0; x < image.getWidth(); ++x )
            {
                const test::Rgba8 pixel = image.getPixel( x, y );
                const bool        bRing = test::RHITestImage::getColorDistance( pixel, corner ) > DrawnMask::kBackgroundDistance && computeLuma( pixel ) <= 40u;
                if ( pOther == nullptr )
                {
                    count += bRing ? 1u : 0u;
                    continue;
                }
                const test::Rgba8 otherPixel = pOther->getPixel( x, y );
                const bool        bOtherRing = test::RHITestImage::getColorDistance( otherPixel, otherCorner ) > DrawnMask::kBackgroundDistance && computeLuma( otherPixel ) <= 40u;
                count += ( bRing != bOtherRing ) ? 1u : 0u;
            }
        }
        return count;
    };

    sw::shared_ptr<sw::Skeleton> skeleton = sw::make_shared<sw::Skeleton>();
    (void)skeleton->addBone( sw::hashed_string( "base" ), -1, sw::BoneTransform{}, sw::float4x4::Identity );
    (void)skeleton->addBone( sw::hashed_string( "body" ), 0, sw::BoneTransform{}, sw::float4x4::Identity );
    skeleton->computeInverseBindFromReference();
    // 메시 공간 이동이다(컴포넌트 스케일 전) — 화면에서 0.45 m 옆이다.
    constexpr float32 kLocalShift = 0.45f / ToonSphereScene::kSphereScale;

    uint32                comparedCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string  label = sw::string( device->getBackendName() ) + ": ";
        sw::FrameRenderer skinnedRenderer;
        sw::FrameRenderer staticRenderer;
        const bool        bReady = skinnedRenderer.initialize( device.get() ) && staticRenderer.initialize( device.get() );
        SW_EXPECT_TRUE_MSG( bReady, ( label + "렌더러를 못 만들었다" ).c_str() );
        if ( bReady == false || device->getCapabilities()._bGpuMeshMorph == SW_FALSE )
            continue;

        // 스킨드 구(모든 정점이 본 1)와, 같은 이동을 CPU 로 정점에 걸어 둔 정적 구.
        ToonSphereScene skinnedScene;
        ToonSphereScene staticScene;
        SW_ASSERT_TRUE_MSG( skinnedScene.populate( ToonSphereScene::makeToonMaterial( true ) ) && staticScene.populate( ToonSphereScene::makeToonMaterial( true ) ),
                            ( label + "무대를 못 만들었다" ).c_str() );
        const sw::vector<sw::RHIVertex>& listBind = skinnedScene._mesh->getVertices();
        sw::vector<sw::MeshSkinVertex>   listSkin( listBind.size() );
        sw::vector<sw::RHIVertex>        listShifted = listBind;
        for ( size_t index = 0; index < listBind.size(); ++index )
        {
            listSkin[index]._arrJoint[0] = 1u;
            listShifted[index]._arrPosition[0] += kLocalShift;
        }
        sw::shared_ptr<sw::Mesh> skinnedMesh = sw::Mesh::create();
        skinnedMesh->setVertices( listBind );
        skinnedMesh->setSkin( listSkin, 2 );
        sw::shared_ptr<sw::Mesh> shiftedMesh = sw::Mesh::create();
        shiftedMesh->setVertices( listShifted );

        // 스킨드 구는 스켈레탈 메시 컴포넌트로 그린다(무대의 정적 구는 숨긴다).
        skinnedScene._pSphere->setVisible( false );
        sw::GameObject*            pSkinnedObject = skinnedScene._scene.getObjectManager()->createGameObject( sw::hashed_string( "SkinnedSphere" ) );
        sw::SkeletalMeshComponent* pSkinned       = pSkinnedObject != nullptr ? pSkinnedObject->addComponent<sw::SkeletalMeshComponent>() : nullptr;
        SW_ASSERT_NOT_NULL( pSkinned );
        pSkinned->setSkeleton( skeleton );
        pSkinned->setMesh( skinnedMesh );
        pSkinned->setMaterial( skinnedScene._material.get() );
        pSkinned->setBoundsRadius( 2.0f );
        pSkinned->setLocalScale( sw::float3{ ToonSphereScene::kSphereScale, ToonSphereScene::kSphereScale, ToonSphereScene::kSphereScale } );
        ShiftTask task( sw::float3{ kLocalShift, 0.0f, 0.0f } );
        pSkinned->addAnimationPhaseTask( &task );
        staticScene._pSphere->setMesh( shiftedMesh );

        test::RHITestImage skinnedImage;
        test::RHITestImage staticImage;
        const bool         bOk = renderAndReadAttachment( skinnedRenderer, device.get(), skinnedScene._scene, "SceneColor", skinnedImage ) &&
                         renderAndReadAttachment( staticRenderer, device.get(), staticScene._scene, "SceneColor", staticImage );
        pSkinned->removeAnimationPhaseTask( &task );
        SW_EXPECT_TRUE_MSG( bOk, ( label + "그리거나 되읽지 못했다" ).c_str() );
        if ( bOk == false )
            continue;

        const uint32 staticRingCount = countDarkRing( staticImage, nullptr );
        const uint32 ringMismatch    = countDarkRing( skinnedImage, &staticImage );
        SW_LOG_INFO( "%#outline ring %# px, skinned vs static mismatch %# px", label, staticRingCount, ringMismatch );
        SW_EXPECT_TRUE_MSG( staticRingCount > 300, ( label + "정적 구에 외곽선 고리가 없다 (" + sw::to_string( staticRingCount ) + " px)" ).c_str() );
        if ( staticRingCount <= 300 )
            continue;
        ++comparedCount;
        SW_EXPECT_TRUE_MSG( ringMismatch * 5u <= staticRingCount,
                            ( label + "스킨드 구의 외곽선이 옮긴 자리와 어긋난다 (어긋난 " + sw::to_string( ringMismatch ) + " / 고리 " + sw::to_string( staticRingCount ) +
                              " px)" )
                                .c_str() );
    }

    if ( comparedCount == 0 )
        SW_TEST_SKIP( "No RHI backend could render the skinned outline test" );
}

/**
 * @brief [RenderPassGpuTest] 양면 머티리얼(`MATERIAL_TWO_SIDED`)은 뒷면도 그린다 — 머티리얼 변형 PSO 가 후면 컬링을 끈다(네 백엔드)
 * @details 카메라를 등진 사각형을 툰 머티리얼로 그린다. 스위치를 끄면 후면 컬링으로 아무것도 안 그려지고, 켜면 사각형이 보인다
 *          (머리카락 카드 · 치마 같은 VRM 양면 머티리얼이 뒤에서 사라지지 않게).
 */
SW_TEST_CASE( RenderPassGpuTest, TwoSidedMaterialDrawsBackFaces )
{
    struct QuadCase
    {
        bool    _bTwoSided;
        float32 _yaw;
    };
    // [0] 등진 단면 · [1] 등진 양면 · [2] 마주 본 단면(기준 셰이딩)
    const QuadCase kArrCase[] = {
        {false, sw::MathUtil::kPi},
        { true, sw::MathUtil::kPi},
        {false,              0.0f},
    };
    constexpr uint32 kMaxMeanDelta = 6;

    uint32                comparedCount{ 0 };
    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const sw::string label = sw::string( device->getBackendName() ) + ": ";
        uint32           arrDrawn[3]{};
        uint32           arrMeanLuma[3]{};
        bool             bOk = true;
        for ( uint32 caseIndex = 0; caseIndex < 3 && bOk; ++caseIndex )
        {
            ToonSphereScene              scene;
            sw::shared_ptr<sw::Material> material = ToonSphereScene::makeToonMaterial( false );
            bOk                                   = material != nullptr && scene.populate( material );
            if ( bOk == false )
                break;
            material->setStaticSwitch( sw::hashed_string( "TwoSided" ), kArrCase[caseIndex]._bTwoSided );
            // 사각형(앞면 +Z)을 반 바퀴 돌리면 카메라(+Z 쪽)를 등진다.
            scene._mesh = sw::MeshUtil::createRectMesh();
            scene._pSphere->setMesh( scene._mesh );
            scene._pSphere->setLocalRotation( sw::float3{ 0.0f, kArrCase[caseIndex]._yaw, 0.0f } );

            sw::FrameRenderer  renderer;
            test::RHITestImage image;
            bOk = renderer.initialize( device.get() ) && renderAndReadAttachment( renderer, device.get(), scene._scene, "SceneColor", image );
            if ( bOk == false )
                break;
            const DrawnMask mask( image );
            uint64          lumaSum{ 0 };
            for ( uint32 y = 0; y < mask._height; ++y )
            {
                for ( uint32 x = 0; x < mask._width; ++x )
                {
                    if ( mask.isDrawn( x, y ) )
                        lumaSum += computeLuma( image.getPixel( x, y ) );
                }
            }
            arrDrawn[caseIndex]    = mask._drawnCount;
            arrMeanLuma[caseIndex] = mask._drawnCount > 0 ? static_cast<uint32>( lumaSum / mask._drawnCount ) : 0u;
        }
        SW_EXPECT_TRUE_MSG( bOk, ( label + "그리거나 되읽지 못했다" ).c_str() );
        if ( bOk == false )
            continue;
        ++comparedCount;
        SW_LOG_INFO( "%#quad one-sided back %# px · two-sided back %# px (luma %#) · front %# px (luma %#)", label, arrDrawn[0], arrDrawn[1], arrMeanLuma[1],
                     arrDrawn[2], arrMeanLuma[2] );
        SW_EXPECT_TRUE_MSG( arrDrawn[0] < 50, ( label + "단면 머티리얼인데 뒷면이 그려졌다 (" + sw::to_string( arrDrawn[0] ) + " px)" ).c_str() );
        SW_EXPECT_TRUE_MSG( arrDrawn[1] > 2000, ( label + "양면 머티리얼의 뒷면이 그려지지 않았다 (" + sw::to_string( arrDrawn[1] ) + " px)" ).c_str() );
        // 뒷면은 노멀을 뒤집어 칠한다 — 뒤집은 노멀은 마주 본 사각형의 노멀과 같으므로 밝기도 같아야 한다.
        const uint32 meanDelta = arrMeanLuma[1] > arrMeanLuma[2] ? arrMeanLuma[1] - arrMeanLuma[2] : arrMeanLuma[2] - arrMeanLuma[1];
        SW_EXPECT_TRUE_MSG( meanDelta <= kMaxMeanDelta, ( label + "뒷면 셰이딩이 앞면과 다르다 — 노멀을 뒤집지 않았다 (" + sw::to_string( arrMeanLuma[1] ) + " vs " +
                                                          sw::to_string( arrMeanLuma[2] ) + ")" )
                                                            .c_str() );
    }

    if ( comparedCount == 0 )
        SW_TEST_SKIP( "No RHI backend could render the two-sided material test" );
}
