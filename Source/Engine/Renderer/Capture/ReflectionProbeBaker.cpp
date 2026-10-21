#include "pch.h"

#include "Engine/Renderer/Capture/ReflectionProbeBaker.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/RHI/IRHIContextThreadBinding.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/Component/3D/ReflectionProbeComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    SW_LOG_CALLER( "ReflectionProbeBaker" );

    namespace
    {
        struct ReflectionProbeBakerInternal
        {
            /// @brief 첫 프레임은 GPUScene 업로드와 머티리얼 준비가 아직이라 몇 장 그린 뒤 읽는다(`PortraitRenderer` 와 같은 수).
            static constexpr uint32 kWarmupFrameCount = 3;
            /// @brief 찍는 카메라의 가까운 면 · 먼 면(m) — 프로브 둘레의 벽이 잘리지 않을 만큼 가깝고, 방 몇 개를 덮을 만큼 멀다.
            static constexpr float32 kCaptureNearPlane = 0.05f;
            static constexpr float32 kCaptureFarPlane  = 500.0f;
            /// @brief 거칠기 단계 수 — 0(거울) · 0.2 · 0.4 · 0.6 · 0.8 · 1(언리얼 반사 캡처의 밉 수와 비슷하다). 셰이더는 데이터의 단계 수를 읽는다.
            static constexpr uint32 kLevelCount = 6;
            /// @brief 읽는 첨부 — 디퍼드 파이프라인의 조명 결과(하늘 · 반투명 뒤, 블룸 · 톤맵 앞, RGBA16F).
            static constexpr string_view kCaptureAttachment = "LitColor";

            /** @brief 면 여섯의 보는 방향과 위 — +X −X +Y −Y +Z −Z 순. 위아래 면은 위 방향을 +Z 로 둔다(보는 방향과 나란하면 lookAt 이 정해지지 않는다). */
            struct FaceBasis
            {
                float3 _forward;
                float3 _up;
            };
            static constexpr FaceBasis kArrFaceBasis[ReflectionProbeFilter::kFaceCount] = {
                { float3{ 1.0f, 0.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f }},
                {float3{ -1.0f, 0.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f }},
                { float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.0f, 0.0f, 1.0f }},
                {float3{ 0.0f, -1.0f, 0.0f }, float3{ 0.0f, 0.0f, 1.0f }},
                { float3{ 0.0f, 0.0f, 1.0f }, float3{ 0.0f, 1.0f, 0.0f }},
                {float3{ 0.0f, 0.0f, -1.0f }, float3{ 0.0f, 1.0f, 0.0f }},
            };

            /** @brief IEEE 반정밀도 하나를 float32 로 폅니다(RGBA16F 첨부 읽기). */
            static float32 decodeHalf( uint16 bits )
            {
                const uint32 sign     = static_cast<uint32>( bits & 0x8000u ) << 16;
                const uint32 exponent = ( bits >> 10 ) & 0x1Fu;
                const uint32 mantissa = bits & 0x3FFu;
                float32      value    = 0.0f;
                if ( exponent == 0u )
                {
                    value = static_cast<float32>( mantissa ) * ( 1.0f / 16777216.0f ); // 비정규수: 가수 × 2^-24
                }
                else if ( exponent == 31u )
                {
                    value = MathUtil::kMaxFloat; // 무한대 · NaN 은 가장 큰 값으로 — 거르기가 NaN 을 퍼뜨리지 않게
                }
                else
                {
                    const uint32 floatBits = ( ( exponent + 112u ) << 23 ) | ( mantissa << 13 );
                    Memory::copy( &value, &floatBits, sizeof( value ) );
                }
                return sign != 0u ? -value : value;
            }

            static void setError( string* pOutError, const utf8* pMessage )
            {
                if ( pOutError != nullptr )
                    *pOutError = pMessage;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ReflectionProbeBaker::ReflectionProbeBaker()
        : _pDevice{ nullptr }
        , _renderer{ nullptr }
    {
    }

    ReflectionProbeBaker::~ReflectionProbeBaker()
    {
        shutdown();
    }

    bool ReflectionProbeBaker::initialize( IRHIDevice* pDevice )
    {
        shutdown();
        if ( pDevice == nullptr )
            return false;
        _pDevice  = pDevice;
        _renderer = make_unique<FrameRenderer>();
        if ( _renderer->initialize( pDevice, engine::getSubsystem<EngineDefaultAssets>()._defaultDeferredPipeline.get() ) == false )
        {
            _renderer.reset();
            _pDevice = nullptr;
            return false;
        }
        return true;
    }

    void ReflectionProbeBaker::shutdown()
    {
        if ( _renderer != nullptr )
            _renderer->shutdown();
        _renderer.reset();
        _pDevice = nullptr;
    }

    bool ReflectionProbeBaker::captureFace( Scene& scene, uint32 size, vector<float3>& outListPixel, string* pOutError )
    {
        using Internal = ReflectionProbeBakerInternal;
        outListPixel.clear();
        bool bRendered = true;
        for ( uint32 frameIndex = 0; frameIndex < Internal::kWarmupFrameCount && bRendered; ++frameIndex )
        {
            _pDevice->beginFrame();
            bRendered = _renderer->execute( _pDevice, scene );
            _pDevice->endFrame( false, false );
            _pDevice->waitIdle();
        }
        vector<uint8>     bytes;
        RHITextureMipSpan layout{};
        RHIFormat         format{ RHIFormat::R8G8B8A8_UNORM };
        if ( bRendered == false || _renderer->getCapture().readbackTransient( Internal::kCaptureAttachment, bytes, layout, format ) == false )
        {
            Internal::setError( pOutError, "the capture renderer could not draw or read LitColor" );
            return false;
        }
        const bool bExpectedLayout = format == RHIFormat::R16G16B16A16_FLOAT && layout._width == size && layout._height == size;
        if ( bExpectedLayout == false )
        {
            Internal::setError( pOutError, "LitColor is not an RGBA16F target of the capture size - the deferred pipeline changed" );
            return false;
        }
        outListPixel.resize( static_cast<size_t>( size ) * size );
        for ( uint32 row = 0; row < size; ++row )
        {
            for ( uint32 column = 0; column < size; ++column )
            {
                uint16 arrHalf[4]{};
                Memory::copy( arrHalf, bytes.data() + static_cast<size_t>( row ) * layout._rowBytes + static_cast<size_t>( column ) * sizeof( arrHalf ), sizeof( arrHalf ) );
                outListPixel[static_cast<size_t>( row ) * size + column] =
                    float3{ Internal::decodeHalf( arrHalf[0] ), Internal::decodeHalf( arrHalf[1] ), Internal::decodeHalf( arrHalf[2] ) };
            }
        }
        return true;
    }

    bool ReflectionProbeBaker::bakeProbe( Scene& scene, const ReflectionProbeComponent& probe, ReflectionProbeData& outData, string* pOutError )
    {
        using Internal = ReflectionProbeBakerInternal;
        outData.clear();
        const bool bCannotBake = _renderer == nullptr || _pDevice == nullptr || scene.getObjectManager() == nullptr;
        if ( bCannotBake )
        {
            Internal::setError( pOutError, "the reflection probe baker is not initialized" );
            return false;
        }
        // 찍는 카메라는 찍는 동안만 씬에 둔다. 씬의 경로로 얻은 머티리얼은 게임 루프가 틱마다 올리지만 베이크는 루프 밖이다 — 여기서 올린다.
        engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>().initializePending( _pDevice );
        GameObjectManager& manager       = *scene.getObjectManager();
        GameObject*        pCameraObject = manager.createGameObject( hashed_string( "ReflectionProbeCaptureCamera" ) );
        CameraComponent*   pCamera       = pCameraObject != nullptr ? pCameraObject->addComponent<CameraComponent>() : nullptr;
        if ( pCamera == nullptr )
        {
            if ( pCameraObject != nullptr )
                manager.destroyObject( pCameraObject );
            Internal::setError( pOutError, "the capture camera could not be created" );
            return false;
        }
        const float3 capturePosition = probe.getWorldPosition();
        const uint32 faceSize        = probe.getCaptureResolution();
        pCamera->setFieldOfViewY( MathUtil::kHalfPi );
        pCamera->setNearPlane( Internal::kCaptureNearPlane );
        pCamera->setFarPlane( Internal::kCaptureFarPlane );
        scene.setActiveGameCamera( pCamera );
        _renderer->setOutputSizeOverride( faceSize, faceSize );

        ReflectionProbeFace arrFace[ReflectionProbeFilter::kFaceCount];
        bool                bCaptured = true;
        for ( uint32 faceIndex = 0; faceIndex < ReflectionProbeFilter::kFaceCount && bCaptured; ++faceIndex )
        {
            const Internal::FaceBasis& basis = Internal::kArrFaceBasis[faceIndex];
            pCamera->setLocalPosition( capturePosition );
            pCamera->lookAt( capturePosition + basis._forward, basis._up );
            manager.flushSceneTransforms();
            ReflectionProbeFace& face = arrFace[faceIndex];
            face._size                = faceSize;
            face._viewProj            = pCamera->getViewProjectionMatrix( 1.0f );
            face._forward             = pCamera->getCameraForward();
            bCaptured                 = captureFace( scene, faceSize, face._listPixel, pOutError );
        }
        // 카메라를 지우면 직접 고른 카메라가 풀린다(클래스 설명).
        manager.destroyObject( pCameraObject );
        if ( bCaptured == false )
            return false;
        return ReflectionProbeFilter::makeFromFaces( capturePosition, arrFace, probe.getResolution(), Internal::kLevelCount, probe.getBakeSampleCount(), outData, pOutError );
    }

    bool ReflectionProbeBaker::bakeProbes( IRHIDevice& device, RenderThread* pRenderThread, Scene& scene, const vector<ReflectionProbeComponent*>& listProbe,
                                           vector<ReflectionProbeData>& outListData, string* pOutError )
    {
        using Internal = ReflectionProbeBakerInternal;
        outListData.clear();
        outListData.resize( listProbe.size() );
        // 렌더 스레드가 받은 일을 모두 끝내게 하고, 컨텍스트가 스레드에 묶이는 백엔드(GL, DX11)는 이 스레드가 잡는다(PortraitBatchUtil 과 같은 길).
        if ( pRenderThread != nullptr )
            pRenderThread->waitIdle();
        IRHIContextThreadBinding* pContextBinding = device.findContextThreadBinding();
        if ( pContextBinding != nullptr && pContextBinding->bindGraphicsContext() == false )
        {
            Internal::setError( pOutError, "could not bind the graphics context for the reflection bake" );
            return false;
        }
        ReflectionProbeBaker baker;
        bool                 bAllBaked = baker.initialize( &device );
        if ( bAllBaked == false )
            Internal::setError( pOutError, "the capture renderer (deferred pipeline) could not be created" );
        for ( size_t probeIndex = 0; probeIndex < listProbe.size() && bAllBaked; ++probeIndex )
        {
            const ReflectionProbeComponent* pProbe = listProbe[probeIndex];
            if ( pProbe == nullptr || baker.bakeProbe( scene, *pProbe, outListData[probeIndex], pOutError ) == false )
                bAllBaked = false;
        }
        baker.shutdown();
        if ( pContextBinding != nullptr )
            pContextBinding->unbindGraphicsContext();
        return bAllBaked;
    }
} // namespace sw
