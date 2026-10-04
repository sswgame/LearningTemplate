#include "pch.h"

#include "Engine/Graphics/Renderer/Capture/PortraitRenderer.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Physics/AABB.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    SW_LOG_CALLER( "PortraitRenderer" );

    namespace
    {
        struct PortraitRendererInternal
        {
            /** @brief 첫 프레임은 GpuScene 업로드 · 머티리얼 준비가 아직이라 몇 장 그린 뒤 읽는다. */
            static constexpr uint32 kWarmupFrameCount = 3;

            /** @brief 스튜디오 씬의 그릴 메시(로컬 경계 상자를 월드로 옮긴 것)를 모두 덮는 구(가운데 · 반지름)입니다. 메시가 없으면 false 입니다. */
            static bool computeSubjectSphere( GameObjectManager& manager, float3& outCenter, float32& outRadius )
            {
                vector<GameObject*> listObject;
                manager.getAllGameObjects( listObject );
                float3 boundsMin{ MathUtil::MaxFloat, MathUtil::MaxFloat, MathUtil::MaxFloat };
                float3 boundsMax{ MathUtil::MinFloat, MathUtil::MinFloat, MathUtil::MinFloat };
                bool   bAny = false;
                for ( GameObject* pObject : listObject )
                {
                    for ( Component* pComponent : pObject->getComponents() )
                    {
                        const MeshComponent* pMesh = castTo<MeshComponent>( pComponent );
                        if ( pMesh == nullptr || pMesh->getMesh() == nullptr )
                            continue;
                        // 메시의 실제 경계 상자를 월드로 옮긴다 — 컴포넌트의 경계 구(단위 상자 반지름일 수 있다)는 사람 모양처럼 좁은 대상을 크게 잡는다.
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
    PortraitRenderer::PortraitRenderer()
        : _pDevice{ nullptr }
        , _renderer{ nullptr }
    {
    }

    PortraitRenderer::~PortraitRenderer()
    {
        shutdown();
    }

    bool PortraitRenderer::initialize( IRHIDevice* pDevice )
    {
        shutdown();
        if ( pDevice == nullptr )
            return false;
        _pDevice  = pDevice;
        _renderer = make_unique<FrameRenderer>();
        // 포워드 파이프라인 — TAA 가 없어(시간 누적 없음) 한 장으로 끝나는 그림이다.
        if ( _renderer->initialize( pDevice, engine::getEngineDefaultAssets()._defaultForwardPipeline ) == false )
        {
            _renderer.reset();
            return false;
        }
        _renderer->setPresentCaptureEnabled( true );
        return true;
    }

    void PortraitRenderer::shutdown()
    {
        if ( _renderer != nullptr )
            _renderer->shutdown();
        _renderer.reset();
        _pDevice = nullptr;
    }

    bool PortraitRenderer::renderPrefab( const PortraitRequest& request, vector<uint8>& outRgbaBytes )
    {
        outRgbaBytes.clear();
        if ( _renderer == nullptr || _pDevice == nullptr || request._prefabPath.empty() || engine::areEngineServicesBound() == false )
            return false;

        // 스튜디오 — 씬 매니저에 등록하지 않는 씬이라 틱하지 않고 게임 화면에 나오지 않는다.
        Scene              studio( "PortraitStudio" );
        GameObjectManager* pManager = studio.getObjectManager();
        GameObject*        pSubject = engine::getAssetManager().getPrefabCache().spawn( pManager, request._prefabPath, "PortraitSubject" );
        if ( pSubject == nullptr )
        {
            SW_LOG_ERROR( "Portrait: prefab '%#' could not be spawned", request._prefabPath.c_str() );
            return false;
        }
        // 스폰이 경로로 잡은 머티리얼을 GPU 에 올린다 — 게임 루프에서는 틱마다 하는 일(`MaterialCache::initializePending`)인데 스튜디오는 틱하지 않는다.
        engine::getAssetManager().getMaterialManager().initializePending( _pDevice );
        float3  center{};
        float32 radius = 1.0f;
        if ( PortraitRendererInternal::computeSubjectSphere( *pManager, center, radius ) == false )
        {
            SW_LOG_ERROR( "Portrait: prefab '%#' has no mesh to draw", request._prefabPath.c_str() );
            return false;
        }

        // 카메라 — 대상 정면(+Z) 쪽에서 요 · 피치만큼 돌아 경계 구가 시야각 안에 들도록 물러난다.
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

        // 조명 — 카메라 쪽 위에서 비스듬히 오는 키 라이트 하나 + 환경광. 스튜디오의 빛만 대상에 닿는다. 빛의 기본 방향은 정면(+Z) 위에서 오므로
        // 카메라와 같은 요만큼 돌린다.
        GameObject*                pLightObject = pManager->createGameObject( hashed_string( "PortraitKeyLight" ) );
        DirectionalLightComponent* pKeyLight    = pLightObject != nullptr ? pLightObject->addComponent<DirectionalLightComponent>() : nullptr;
        if ( pKeyLight != nullptr )
        {
            pKeyLight->setLocalRotation( float3{ 0.0f, request._yaw, 0.0f } );
            pKeyLight->setIntensity( 2.2f );
            pKeyLight->setAmbient( 0.35f );
        }

        _renderer->setOutputSizeOverride( request._width, request._height );
        bool bRendered = true;
        for ( uint32 frameIndex = 0; frameIndex < PortraitRendererInternal::kWarmupFrameCount && bRendered; ++frameIndex )
        {
            _pDevice->beginFrame( request._backgroundColor );
            bRendered = _renderer->execute( _pDevice, &studio );
            _pDevice->endFrame( false, false );
            _pDevice->waitIdle();
        }
        RHITextureMipSpan layout{};
        vector<uint8>     captureBytes;
        if ( bRendered == false || _renderer->readbackPresentCapture( captureBytes, layout ) == false )
        {
            SW_LOG_ERROR( "Portrait: rendering '%#' failed", request._prefabPath.c_str() );
            return false;
        }
        // 캡처는 계약 포맷(RGBA8)이고 행 바이트가 너비 × 4 보다 클 수 있다 — 빈틈없이 옮긴다.
        outRgbaBytes.resize( static_cast<size_t>( layout._width ) * layout._height * 4u );
        for ( uint32 row = 0; row < layout._height; ++row )
            Memory::copy( outRgbaBytes.data() + static_cast<size_t>( row ) * layout._width * 4u, captureBytes.data() + static_cast<size_t>( row ) * layout._rowBytes,
                          static_cast<size_t>( layout._width ) * 4u );
        // 알파는 불투명으로 — 캡처의 알파는 셰이더마다 달라 썸네일에서 뜻이 없다.
        for ( size_t index = 3; index < outRgbaBytes.size(); index += 4 )
            outRgbaBytes[index] = 255;
        return true;
    }
} // namespace sw
