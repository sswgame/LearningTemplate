#include "pch.h"

#include "Engine/Renderer/Capture/PortraitStudio.h"

#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Renderer/Frame/View/RenderViewCollector.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Visual/Component/DirectionalLightComponent.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw
{
    namespace
    {
        struct PortraitStudioInternal
        {
            /**
             * @brief 도착 단계(`PrimitiveRegistry::updateAssetWaiters`)에 한 번에 넘기는 시간(초)입니다. 메시 등장 디더 시간의 상한(`MeshComponent::_appearSeconds` 최대 5 초)보다 길다.
             * @details 스튜디오는 시간을 흘리지 않는다 — 도착한 메시는 등장 비율 0 에서 오르는데 그대로 두면 0 이라 그려지지 않는다. 사진에는 다 나타난 모습을 찍는다.
             */
            static constexpr float32 kSettleSeconds = 10.0f;

            /** @brief 첫 픽셀(구석 — 배경)과 RGB 가 다른 픽셀이 하나라도 있으면 true 입니다. 캡처는 계약 포맷 RGBA8 이고 행 사이에 빈틈이 있을 수 있다. */
            static bool hasVisibleSubject( const PortraitCapture& capture )
            {
                if ( capture._width == 0 || capture._height == 0 || capture._bytes.size() < static_cast<size_t>( capture._height ) * capture._rowBytes )
                    return false;
                const uint8* pCorner = capture._bytes.data();
                for ( uint32 row = 0; row < capture._height; ++row )
                {
                    const uint8* pRow = capture._bytes.data() + static_cast<size_t>( row ) * capture._rowBytes;
                    for ( uint32 column = 0; column < capture._width; ++column )
                    {
                        const uint8* pPixel = pRow + static_cast<size_t>( column ) * 4u;
                        if ( pPixel[0] != pCorner[0] || pPixel[1] != pCorner[1] || pPixel[2] != pCorner[2] )
                            return true;
                    }
                }
                return false;
            }

            /** @brief 스튜디오 씬의 그릴 메시(로컬 경계 상자를 월드로 옮긴 것)를 모두 덮는 구(중심과 반지름)입니다. 메시가 없으면 false 입니다. */
            static bool computeSubjectSphere( SceneIterationKey key, GameObjectManager& manager, float3& outCenter, float32& outRadius )
            {
                vector<GameObject*> listObject;
                manager.getAllGameObjects( key, listObject );
                float3 boundsMin{ MathUtil::kMaxFloat, MathUtil::kMaxFloat, MathUtil::kMaxFloat };
                float3 boundsMax{ MathUtil::kMinFloat, MathUtil::kMinFloat, MathUtil::kMinFloat };
                bool   bAny = false;
                for ( GameObject* pObject : listObject )
                {
                    for ( Component* pComponent : pObject->getComponents() )
                    {
                        const MeshComponent* pMesh = castTo<MeshComponent>( pComponent );
                        if ( pMesh == nullptr || pMesh->getMesh() == nullptr )
                            continue;
                        // 메시의 실제 경계 상자를 월드로 옮긴다. 컴포넌트의 경계 구(단위 상자 반지름일 수 있다)는 사람 모양처럼 좁은 대상을 크게 잡는다.
                        const Mesh& mesh  = *pMesh->getMesh();
                        const AABB  world = AABB{ mesh.getLocalBoundsMin(), mesh.getLocalBoundsMax() }.transformedBy( pMesh->getWorldMatrix() );
                        boundsMin         = float3::min( boundsMin, world._min );
                        boundsMax         = float3::max( boundsMax, world._max );
                        bAny              = true;
                    }
                }
                if ( bAny == false )
                    return false;
                outCenter = ( boundsMin + boundsMax ) * 0.5f;
                outRadius = MathUtil::max( 0.05f, ( boundsMax - boundsMin ).getLength() * 0.5f );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PortraitStudio::PortraitStudio()
        : _capture{}
        , _services{}
    {
    }

    PortraitStudio::~PortraitStudio()
    {
        shutdown();
    }

    bool PortraitStudio::initialize( RenderThread& renderThread, const SceneEngineServices& services )
    {
        shutdown();
        if ( services._pAssetManager == nullptr || services._pDefaultAssets == nullptr )
        {
            SW_LOG_ERROR_ID( "PortraitStudio.PortraitSceneServicesNo", "Portrait: the scene services have no asset manager or default assets" );
            return false;
        }
        _services = services;
        // 포워드 파이프라인이다. TAA 가 없어(시간 누적 없음) 한 장으로 끝나는 이미지다.
        return _capture.initialize( renderThread, services._pDefaultAssets->_defaultForwardPipeline.get() );
    }

    void PortraitStudio::shutdown()
    {
        _capture.shutdown();
        _services = SceneEngineServices{};
    }

    bool PortraitStudio::renderPrefab( const PortraitRequest& request, PortraitCapture& outCapture )
    {
        outCapture               = PortraitCapture{};
        const bool bCannotRender = _capture.getDevice() == nullptr || request._prefabPath.empty();
        if ( bCannotRender )
            return false;

        // 스튜디오: 씬 매니저에 등록하지 않는 씬이라 틱하지 않고 게임 화면에 나오지 않는다.
        AssetManager&      assetManager = *_services._pAssetManager;
        Scene              studio( "PortraitStudio", _services );
        GameObjectManager* pManager = studio.getObjectManager();
        GameObject*        pSubject = assetManager.getAssetCache<PrefabCache>().spawn( pManager, request._prefabPath, "PortraitSubject" );
        if ( pSubject == nullptr )
        {
            SW_LOG_ERROR_ID( "PortraitStudio.PortraitPrefabCouldNot", "Portrait: prefab '%#' could not be spawned", request._prefabPath.c_str() );
            return false;
        }
        // 스튜디오는 틱하지 않으므로 게임 루프가 프레임마다 하는 일을 여기서 차례로 한다 — 스폰이 건 에셋(메시 · 텍스처) 일을 끝내고(`finishPendingAssetJobs`),
        // 도착한 메시를 컴포넌트에 넣고(씬 프레임의 `AssetArrival` 단계), 경로로 얻은 머티리얼을 초기화한다(게임 스레드 — 상수 버퍼는 렌더 스레드의 프록시가 만든다).
        // 예전에는 앞의 둘이 빠져 비동기 메시(`.mesh` 경로)를 쓰는 프리팹은 "그릴 메시 없음" 으로 실패했다(그 길을 도는 테스트가 없었다 — `Bake.Portraits`).
        assetManager.finishPendingAssetJobs();
        pManager->getPrimitiveRegistry().updateAssetWaiters( PortraitStudioInternal::kSettleSeconds );
        assetManager.getAssetCache<MaterialCache>().initializePending( _capture.getDevice() );
        float3  center{};
        float32 radius = 1.0f;
        if ( PortraitStudioInternal::computeSubjectSphere( SceneIterationKey{}, *pManager, center, radius ) == false )
        {
            SW_LOG_ERROR_ID( "PortraitStudio.PortraitPrefabNoMesh", "Portrait: prefab '%#' has no mesh to draw", request._prefabPath.c_str() );
            return false;
        }

        // 카메라: 대상 정면(+Z) 쪽에서 요와 피치만큼 돌아 경계 구가 시야각 안에 들도록 물러난다.
        const float32    fieldOfViewY = MathUtil::clamp( request._fieldOfViewY, 0.1f, 2.5f );
        const float32    distance     = radius * MathUtil::max( 1.0f, request._padding ) / MathUtil::sin( fieldOfViewY * 0.5f );
        const float3     toCamera{ MathUtil::sin( request._yaw ) * MathUtil::cos( request._pitch ), MathUtil::sin( request._pitch ),
                               MathUtil::cos( request._yaw ) * MathUtil::cos( request._pitch ) };
        GameObject*      pCameraObject = pManager->createGameObject( hashed_string( "PortraitCamera" ) );
        CameraComponent* pCamera       = pCameraObject != nullptr ? pCameraObject->addComponent<CameraComponent>() : nullptr;
        if ( pCamera == nullptr )
            return false;
        pCamera->setFieldOfViewY( fieldOfViewY );
        pCamera->setNearPlane( MathUtil::max( 0.01f, distance - radius * 2.0f ) );
        pCamera->setFarPlane( distance + radius * 2.0f );
        pCamera->setLocalPosition( center + toCamera * distance );
        pCamera->lookAt( center );
        studio.setActiveGameCamera( pCamera );

        // 조명: 카메라 쪽 위에서 비스듬히 오는 키 라이트 하나와 환경광이다. 스튜디오의 빛만 대상에 닿는다. 요 0 의 키 라이트 방향을
        // 카메라와 같은 요만큼 돌린다.
        GameObject*                pLightObject = pManager->createGameObject( hashed_string( "PortraitKeyLight" ) );
        DirectionalLightComponent* pKeyLight    = pLightObject != nullptr ? pLightObject->addComponent<DirectionalLightComponent>() : nullptr;
        if ( pKeyLight != nullptr )
        {
            pKeyLight->setLocalLightDirection( DirectionalLightComponent::kSlantedKeyDirection );
            pKeyLight->setLocalRotation( pKeyLight->getLocalRotation() + float3{ 0.0f, request._yaw, 0.0f } ); // 요는 마지막에 도는 축이라 더하면 된다
            pKeyLight->setIntensity( 2.2f );
            pKeyLight->setAmbient( 0.35f );
        }

        // 스튜디오 씬 → 패킷은 게임 스레드가, 그리기 · 읽기는 렌더 스레드가 한다(`SceneCapture`).
        pManager->flushSceneTransforms(); // 방금 둔 카메라 · 빛의 월드 행렬을 시점을 구하기 전에 반영한다
        SceneCaptureRequest capture{};
        capture._view._settings  = RenderViewCollector::makeMainSettings( pCamera, RenderViewCollector::kCaptureAutoScale );
        capture._view._viewProj  = pCamera->getViewProjectionMatrix( RenderViewCollector::computeAspect( capture._view._settings, request._width, request._height ) );
        capture._view._cameraPos = pCamera->getCameraPosition();
        capture._view._width     = request._width;
        capture._view._height    = request._height;
        capture._clearColor      = request._backgroundColor;
        SceneCaptureResult result;
        if ( _capture.capture( studio, capture, result ) == false )
        {
            SW_LOG_ERROR_ID( "PortraitStudio.PortraitRenderingFailed", "Portrait: rendering '%#' failed", request._prefabPath.c_str() );
            return false;
        }
        outCapture._bytes               = std::move( result._bytes );
        const RHITextureMipSpan& layout = result._layout;
        // 옮기기 · 쓰기는 여기서 하지 않는다 - GPU 가 없어도 되는 CPU · IO 일이라 부른 쪽이 워커에 맡긴다(`RenderTargetImageFile::unpackRGBA8`).
        outCapture._width    = layout._width;
        outCapture._height   = layout._height;
        outCapture._rowBytes = layout._rowBytes;
        // 대상이 한 픽셀도 보이지 않는 사진(전부 배경색)은 실패다 — 성공으로 쓰면 빈 썸네일이 조용히 남는다(등장 디더 · 시점 결함이 그렇게 숨었다).
        if ( PortraitStudioInternal::hasVisibleSubject( outCapture ) == false )
        {
            SW_LOG_ERROR_ID( "PortraitStudio.PortraitRenderedOnlyBackground", "Portrait: '%#' rendered only the background - the subject is not visible", request._prefabPath.c_str() );
            return false;
        }
        return true;
    }
} // namespace sw
