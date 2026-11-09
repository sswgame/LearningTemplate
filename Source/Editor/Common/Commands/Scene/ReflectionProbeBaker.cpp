#include "pch.h"

#include "Editor/Common/Commands/Scene/ReflectionProbeBaker.h"

#include "Core/Log/Logger.h"
#include "Core/Math/Half.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Frame/View/RenderViewCollector.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Visual/Component/ReflectionProbeComponent.h"

namespace sw
{
    namespace
    {
        struct ReflectionProbeBakerInternal
        {
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
            static constexpr FaceBasis kArrFaceBasis[ReflectionProbeAsset::kFaceCount] = {
                { float3{ 1.0f, 0.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f }},
                {float3{ -1.0f, 0.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f }},
                { float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.0f, 0.0f, 1.0f }},
                {float3{ 0.0f, -1.0f, 0.0f }, float3{ 0.0f, 0.0f, 1.0f }},
                { float3{ 0.0f, 0.0f, 1.0f }, float3{ 0.0f, 1.0f, 0.0f }},
                {float3{ 0.0f, 0.0f, -1.0f }, float3{ 0.0f, 1.0f, 0.0f }},
            };

            /** @brief IEEE 반정밀도 하나를 float32 로 폅니다(RGBA16F 첨부 읽기). 무한대 · NaN 은 가장 큰 값으로 — 거르기가 NaN 을 퍼뜨리지 않게(`half::toFiniteFloat`). */
            static float32 decodeHalf( uint16 bits ) { return half::fromBits( bits ).toFiniteFloat(); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ReflectionProbeBaker::ReflectionProbeBaker()
        : _capture{}
    {
    }

    ReflectionProbeBaker::~ReflectionProbeBaker()
    {
        shutdown();
    }

    bool ReflectionProbeBaker::initialize( RenderThread& renderThread )
    {
        return _capture.initialize( renderThread, editor::getSubsystem<EngineDefaultAssets>()._defaultDeferredPipeline.get() );
    }

    void ReflectionProbeBaker::shutdown()
    {
        _capture.shutdown();
    }

    expected<void> ReflectionProbeBaker::captureFace( Scene& scene, CameraComponent& camera, uint32 size, ReflectionProbeFaceCapture& outFace )
    {
        using Internal = ReflectionProbeBakerInternal;
        outFace._bytes.clear();
        // 씬 → 패킷은 이 스레드가, 그리기 · 읽기는 렌더 스레드가 한다(`SceneCapture`). 읽는 것은 후처리 앞의 조명 결과(`LitColor`)다.
        SceneCaptureRequest request{};
        request._view._settings  = RenderViewCollector::makeMainSettings( &camera, RenderViewCollector::kCaptureAutoScale );
        request._view._viewProj  = camera.getViewProjectionMatrix( 1.0f );
        request._view._cameraPos = camera.getCameraPosition();
        request._view._width     = size;
        request._view._height    = size;
        request._attachmentName  = string( Internal::kCaptureAttachment );
        SceneCaptureResult result;
        if ( _capture.capture( scene, request, result ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the capture renderer could not draw or read LitColor" } );
        const bool bExpectedLayout = result._format == RHIFormat::R16G16B16A16_FLOAT && result._layout._width == size && result._layout._height == size;
        if ( bExpectedLayout == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "LitColor is not an RGBA16F target of the capture size - the deferred pipeline changed" } );
        // 풀기(half → float)는 여기서 하지 않는다 - GPU 가 없어도 되는 CPU 일이라 워커가 접기 · 거르기와 함께 한다(`makeProbeData`).
        outFace._bytes    = std::move( result._bytes );
        outFace._rowBytes = result._layout._rowBytes;
        outFace._size     = size;
        return {};
    }

    expected<void> ReflectionProbeBaker::makeProbeData( const ReflectionProbeCapture& capture, ReflectionProbeAsset& outData )
    {
        using Internal = ReflectionProbeBakerInternal;
        outData.clear();
        ReflectionProbeFace arrFace[ReflectionProbeAsset::kFaceCount];
        for ( uint32 faceIndex = 0; faceIndex < ReflectionProbeAsset::kFaceCount; ++faceIndex )
        {
            const ReflectionProbeFaceCapture& source = capture._arrFace[faceIndex];
            ReflectionProbeFace&              face   = arrFace[faceIndex];
            const size_t                      size   = source._size;
            face._size                               = source._size;
            face._viewProj                           = source._viewProj;
            face._forward                            = source._forward;
            if ( source._bytes.size() < size * source._rowBytes )
                return unexpected( Error{ ErrorKind::InvalidArgument, "a captured face is shorter than its layout" } );
            face._listPixel.resize( size * size );
            for ( size_t row = 0; row < size; ++row )
            {
                for ( size_t column = 0; column < size; ++column )
                {
                    uint16 arrHalf[4]{};
                    Memory::copy( arrHalf, source._bytes.data() + row * source._rowBytes + column * sizeof( arrHalf ), sizeof( arrHalf ) );
                    face._listPixel[row * size + column] = float3{ Internal::decodeHalf( arrHalf[0] ), Internal::decodeHalf( arrHalf[1] ), Internal::decodeHalf( arrHalf[2] ) };
                }
            }
        }
        return ReflectionProbeAsset::makeFromFaces( capture._capturePosition, arrFace, capture._resolution, Internal::kLevelCount, capture._sampleCount, outData );
    }

    expected<void> ReflectionProbeBaker::captureProbe( Scene& scene, const ReflectionProbeComponent& probe, ReflectionProbeCapture& outCapture )
    {
        using Internal         = ReflectionProbeBakerInternal;
        outCapture             = ReflectionProbeCapture{};
        const bool bCannotBake = _capture.getDevice() == nullptr || scene.getObjectManager() == nullptr;
        if ( bCannotBake )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the reflection probe baker is not initialized" } );
        // 찍는 카메라는 찍는 동안만 씬에 둔다. 씬의 경로로 얻은 머티리얼은 게임 루프가 틱마다 올리지만 베이크는 루프 밖이다 — 여기서 올린다.
        editor::getSubsystem<AssetManager>().getAssetCache<MaterialCache>().initializePending( _capture.getDevice() );
        GameObjectManager& manager       = *scene.getObjectManager();
        GameObject*        pCameraObject = manager.createGameObject( hashed_string( "ReflectionProbeCaptureCamera" ) );
        CameraComponent*   pCamera       = pCameraObject != nullptr ? pCameraObject->addComponent<CameraComponent>() : nullptr;
        if ( pCamera == nullptr )
        {
            if ( pCameraObject != nullptr )
                manager.destroyGameObject( pCameraObject );
            return unexpected( Error{ ErrorKind::ExternalFailure, "the capture camera could not be created" } );
        }
        const float3 capturePosition = probe.getWorldPosition();
        const uint32 faceSize        = probe.getCaptureResolution();
        pCamera->setFieldOfViewY( MathUtil::kHalfPi );
        pCamera->setNearPlane( Internal::kCaptureNearPlane );
        pCamera->setFarPlane( Internal::kCaptureFarPlane );
        scene.setActiveGameCamera( pCamera );

        outCapture._capturePosition = capturePosition;
        outCapture._resolution      = probe.getResolution();
        outCapture._sampleCount     = probe.getBakeSampleCount();
        expected<void> captured{};
        for ( uint32 faceIndex = 0; faceIndex < ReflectionProbeAsset::kFaceCount && captured.has_value(); ++faceIndex )
        {
            const Internal::FaceBasis& basis = Internal::kArrFaceBasis[faceIndex];
            pCamera->setLocalPosition( capturePosition );
            pCamera->lookAt( capturePosition + basis._forward, basis._up );
            manager.flushSceneTransforms();
            ReflectionProbeFaceCapture& face = outCapture._arrFace[faceIndex];
            face._viewProj                   = pCamera->getViewProjectionMatrix( 1.0f );
            face._forward                    = pCamera->getCameraForward();
            captured                         = captureFace( scene, *pCamera, faceSize, face );
        }
        // 카메라를 지우면 직접 고른 카메라가 풀린다(클래스 설명).
        manager.destroyGameObject( pCameraObject );
        return captured;
    }

    expected<void> ReflectionProbeBaker::captureProbes( RenderThread& renderThread, Scene& scene, const vector<ReflectionProbeComponent*>& listProbe,
                                                        vector<ReflectionProbeCapture>& outListCapture )
    {
        outListCapture.clear();
        outListCapture.resize( listProbe.size() );
        ReflectionProbeBaker baker;
        expected<void>       baked = baker.initialize( renderThread ) ? expected<void>{}
                                                                      : expected<void>{ unexpected( Error{ ErrorKind::ExternalFailure, "the capture renderer (deferred pipeline) could not be created" } ) };
        for ( size_t probeIndex = 0; probeIndex < listProbe.size() && baked.has_value(); ++probeIndex )
        {
            const ReflectionProbeComponent* pProbe = listProbe[probeIndex];
            baked                                  = pProbe != nullptr ? baker.captureProbe( scene, *pProbe, outListCapture[probeIndex] ) : expected<void>{ unexpected( Error{ ErrorKind::InvalidArgument, "a reflection probe in the list is null" } ) };
        }
        baker.shutdown();
        return baked;
    }
} // namespace sw
