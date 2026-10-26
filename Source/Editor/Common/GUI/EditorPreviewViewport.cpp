#include "pch.h"

#include "Editor/Common/GUI/EditorPreviewViewport.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Viewport/EditorOrbitMath.h"

#include "Engine/Environment/Sky/SkyComponent.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Scene/Scene.h"

#include <imgui.h>

namespace sw::editor
{
    /**
     * @brief `-gv_editorPreviewScreenshot=<경로>`: 처음 열린 미리보기 뷰포트가 충분히 그린 뒤 자기 렌더 타깃을 이 경로에 한 번 찍습니다(PPM · PNG).
     * @details 시나리오의 `<Screenshot>` 은 주 출력만 찍는다 — 미리보기 씬이 그려지는지, 메인 씬과 섞이지 않는지는 이 그림으로 본다.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorPreviewScreenshot, "", "미리보기 씬 뷰포트의 렌더 타깃을 이 경로에 한 번 찍는다 (시나리오 눈 확인용, 비우면 안 함)" );
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct EditorPreviewViewportInternal
        {
            static constexpr float32 kDefaultDistance       = 3.0f;
            static constexpr float32 kDefaultYaw            = 0.6f;
            static constexpr float32 kDefaultPitch          = 0.35f;
            static constexpr float32 kDefaultFieldOfViewY   = 0.8f;
            static constexpr float32 kNearPlane             = 0.05f;
            static constexpr float32 kFarPlane              = 200.0f;
            static constexpr float32 kMinDistance           = 0.2f;
            static constexpr float32 kMaxPitch              = 1.5f;
            static constexpr float32 kRotateRadiansPerPixel = 0.01f;
            /** @brief 스크린샷을 찍기 전에 그릴 프레임 수입니다 — 첫 프레임은 클리어 색이고 렌더러 · PSO 를 준비하는 데 몇 프레임 걸린다. */
            static constexpr uint32 kScreenshotWarmupFrames = 30;
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorPreviewViewport" );

    EditorPreviewViewport::EditorPreviewViewport()
        : _pScene{ nullptr }
        , _imagePosition{}
        , _imageSize{}
        , _orbitTarget{ 0.0f, 0.0f, 0.0f }
        , _orbitDistance{ EditorPreviewViewportInternal::kDefaultDistance }
        , _orbitYaw{ EditorPreviewViewportInternal::kDefaultYaw }
        , _orbitPitch{ EditorPreviewViewportInternal::kDefaultPitch }
        , _fieldOfViewY{ EditorPreviewViewportInternal::kDefaultFieldOfViewY }
        , _sceneKey{ kInvalidSceneKey }
        , _targetID{ 0 }
        , _submittedFrameCount{ 0 }
        , _bOwnsScene{ false }
    {
    }

    EditorPreviewViewport::~EditorPreviewViewport()
    {
        shutdown();
    }

    bool EditorPreviewViewport::initialize( string_view sceneName, const ScenePreviewPolicy& policy, const EditorPreviewEnvironment& environment )
    {
        if ( _pScene != nullptr )
            return true;
        SceneManager& sceneManager = editor::getSubsystem<SceneManager>();
        _pScene                    = sceneManager.createPreviewScene( sceneName, policy );
        if ( _pScene == nullptr )
            return false;
        _sceneKey   = sceneManager.getPreviewSceneKey( _pScene );
        _bOwnsScene = true;
        (void)spawnEnvironment( *_pScene, environment ); // 환경이 없어도 씬은 그려진다(키 라이트 폴백)

        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        _targetID                    = editorContext.getSubsystem<EditorRenderViews>().allocatePreviewTarget();
        return true;
    }

    bool EditorPreviewViewport::attachScene( Scene& scene, uint32 sceneKey )
    {
        if ( _pScene != nullptr || sceneKey == kInvalidSceneKey )
            return false;
        _pScene                      = &scene;
        _sceneKey                    = sceneKey;
        _bOwnsScene                  = false;
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        _targetID                    = editorContext.getSubsystem<EditorRenderViews>().allocatePreviewTarget();
        return true;
    }

    void EditorPreviewViewport::shutdown()
    {
        EditorContext* pContext = editor::findSubsystem<EditorContext>();
        if ( _targetID != 0 && pContext != nullptr )
            pContext->getSubsystem<EditorRenderViews>().freePreviewTarget( _targetID );
        _targetID = 0;

        SceneManager* pSceneManager      = findSubsystem<SceneManager>();
        const bool    bDestroyOwnedScene = _bOwnsScene && _pScene != nullptr && pSceneManager != nullptr; // 붙인 씬은 가진 쪽이 정리한다
        if ( bDestroyOwnedScene )
            (void)pSceneManager->destroyPreviewScene( _pScene ); // 매니저가 먼저 종료했으면 그 shutdown 이 이미 정리했다
        _pScene              = nullptr;
        _bOwnsScene          = false;
        _sceneKey            = kInvalidSceneKey;
        _submittedFrameCount = 0;
    }

    uint64 EditorPreviewViewport::getRenderTarget() const
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        if ( _targetID == 0 )
            return 0;
        const EditorPreviewViewTarget* pTarget = editorContext.getSubsystem<EditorRenderViews>().findPreviewTarget( _targetID );
        return pTarget != nullptr ? pTarget->_target._renderTarget : 0;
    }

    void EditorPreviewViewport::setOrbit( const float3& target, float32 distance, float32 yaw, float32 pitch )
    {
        _orbitTarget   = target;
        _orbitDistance = MathUtil::max( EditorPreviewViewportInternal::kMinDistance, distance );
        _orbitYaw      = yaw;
        _orbitPitch    = MathUtil::clamp( pitch, -EditorPreviewViewportInternal::kMaxPitch, EditorPreviewViewportInternal::kMaxPitch );
    }

    void EditorPreviewViewport::frameSphere( const float3& center, float32 radius )
    {
        const float32 distance = MathUtil::max( radius, EditorPreviewViewportInternal::kMinDistance ) / MathUtil::sin( _fieldOfViewY * 0.5f );
        setOrbit( center, distance, _orbitYaw, _orbitPitch );
    }

    void EditorPreviewViewport::computeViewProjection( float32 aspectRatio, float4x4& outViewProj, float3& outEye ) const
    {
        float4x4 view{};
        float4x4 projection{};
        computeViewAndProjection( aspectRatio, view, projection, outEye );
        outViewProj = view * projection;
    }

    void EditorPreviewViewport::computeViewAndProjection( float32 aspectRatio, float4x4& outView, float4x4& outProjection, float3& outEye ) const
    {
        const float3 toCamera{ MathUtil::sin( _orbitYaw ) * MathUtil::cos( _orbitPitch ), MathUtil::sin( _orbitPitch ),
                               MathUtil::cos( _orbitYaw ) * MathUtil::cos( _orbitPitch ) };
        outEye        = _orbitTarget + toCamera * _orbitDistance;
        outView       = float4x4::makeLookAt( outEye, _orbitTarget, float3{ 0.0f, 1.0f, 0.0f } );
        outProjection = float4x4::makePerspectiveFieldOfView( _fieldOfViewY, aspectRatio, EditorPreviewViewportInternal::kNearPlane, EditorPreviewViewportInternal::kFarPlane );
    }

    void EditorPreviewViewport::draw( const float2& size, bool bInputActive )
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        const bool     bHasSize      = size._x > 1.0f && size._y > 1.0f;
        const bool     bHasTarget    = _targetID != 0;
        if ( bHasTarget == false || bHasSize == false )
            return;

        EditorRenderViews&             views        = editorContext.getSubsystem<EditorRenderViews>();
        const uint32                   targetWidth  = static_cast<uint32>( size._x );
        const uint32                   targetHeight = static_cast<uint32>( size._y );
        const EditorPreviewViewTarget* pTarget      = views.findPreviewTarget( _targetID );
        if ( pTarget != nullptr && EditorViewTargetUtil::needsResize( pTarget->_target._width, pTarget->_target._height, targetWidth, targetHeight ) )
            views.ensurePreviewTargetSize( _targetID, targetWidth, targetHeight );
        pTarget = views.findPreviewTarget( _targetID );
        if ( pTarget == nullptr || pTarget->_target._pTextureID == nullptr )
            return;

        const ImVec2 imagePosition = ImGui::GetCursorScreenPos();
        _imagePosition             = float2{ imagePosition.x, imagePosition.y };
        _imageSize                 = size;
        ImGui::Image( reinterpret_cast<ImTextureID>( pTarget->_target._pTextureID ), ImVec2{ size._x, size._y } );
        const bool bImageHovered = ImGui::IsItemHovered();
        if ( bInputActive && bImageHovered )
        {
            const EditorPointerState pointer = EditorInputGUI::readPointer();
            if ( pointer.isDown( EditorMouseButton::Left ) )
            {
                _orbitYaw -= pointer._delta._x * EditorPreviewViewportInternal::kRotateRadiansPerPixel;
                _orbitPitch = MathUtil::clamp( _orbitPitch + pointer._delta._y * EditorPreviewViewportInternal::kRotateRadiansPerPixel,
                                               -EditorPreviewViewportInternal::kMaxPitch, EditorPreviewViewportInternal::kMaxPitch );
            }
            // 씬 뷰와 같은 휠 줌과 가운데 단추 팬이다(`EditorOrbitMath`). 궤도 중심이 같이 움직여 이어지는 회전이 같은 점을 돈다.
            if ( pointer._wheel != 0.0f )
                _orbitDistance = EditorOrbitMath::dolly( _orbitDistance, pointer._wheel, EditorPreviewViewportInternal::kMinDistance );
            if ( pointer.isDown( EditorMouseButton::Middle ) )
            {
                // 시선 f 는 눈에서 대상으로, 오른쪽은 위쪽 축과 f 의 외적(왼손 좌표계), 카메라 위쪽은 f 와 오른쪽의 외적이다.
                const float3  forward{ -MathUtil::sin( _orbitYaw ) * MathUtil::cos( _orbitPitch ), -MathUtil::sin( _orbitPitch ), -MathUtil::cos( _orbitYaw ) * MathUtil::cos( _orbitPitch ) };
                const float3  right = float3{ forward._z, 0.0f, -forward._x }.normalize();
                const float3  up{ forward._y * right._z, forward._z * right._x - forward._x * right._z, -forward._y * right._x };
                const float32 scale = EditorOrbitMath::panWorldPerPixel( _orbitDistance, _fieldOfViewY, size._y );
                _orbitTarget += right * ( -pointer._delta._x * scale ) + up * ( pointer._delta._y * scale );
            }
        }

        // 렌더 타깃의 실제 크기로 비율을 잡는다(다시 만들기 전 프레임은 옛 크기다).
        const float32 aspect = static_cast<float32>( pTarget->_target._width ) / static_cast<float32>( MathUtil::max( 1u, pTarget->_target._height ) );
        float4x4      viewProj{};
        float3        eye{};
        computeViewProjection( aspect, viewProj, eye );
        views.submitPreviewView( _targetID, _sceneKey, viewProj, eye );
        ++_submittedFrameCount;
        requestScreenshotIfAsked();
    }

    void EditorPreviewViewport::requestScreenshotIfAsked()
    {
        if ( gv_editorPreviewScreenshot.empty() || _submittedFrameCount < EditorPreviewViewportInternal::kScreenshotWarmupFrames )
            return;
        if ( requestImageFile( string_view( gv_editorPreviewScreenshot.c_str() ) ) == false )
            return;
        SW_LOG_INFO( "Preview viewport screenshot (scene key %#) -> %#", _sceneKey, gv_editorPreviewScreenshot.c_str() );
        gv_editorPreviewScreenshot = string{}; // 한 번만
    }

    bool EditorPreviewViewport::requestImageFile( string_view path )
    {
        const uint64 renderTarget = getRenderTarget();
        const bool   bNotDrawn    = renderTarget == 0 || _submittedFrameCount == 0;
        if ( path.empty() || bNotDrawn )
            return false;
        RenderThread::requestScreenshot( path, renderTarget, RHIFormat::R8G8B8A8_UNORM );
        return true;
    }

    uint32 EditorPreviewViewport::spawnEnvironment( Scene& scene, const EditorPreviewEnvironment& environment )
    {
        GameObjectManager* pManager = scene.getObjectManager();
        if ( pManager == nullptr )
            return 0;
        uint32 spawnedCount = 0;
        if ( environment._bDirectionalLight )
        {
            GameObject*                pLightObject = pManager->createGameObject( hashed_string( "PreviewKeyLight" ) );
            DirectionalLightComponent* pLight       = pLightObject != nullptr ? pLightObject->addComponent<DirectionalLightComponent>() : nullptr;
            if ( pLight != nullptr )
            {
                pLight->setLocalRotation( environment._lightRotation );
                pLight->setColor( environment._lightColor );
                pLight->setIntensity( environment._lightIntensity );
                pLight->setAmbient( environment._ambient );
                ++spawnedCount;
            }
        }
        if ( environment._bFloor )
        {
            GameObject*    pFloorObject = pManager->createGameObject( hashed_string( "PreviewFloor" ) );
            MeshComponent* pFloor       = pFloorObject != nullptr ? pFloorObject->addComponent<MeshComponent>() : nullptr;
            if ( pFloor != nullptr )
            {
                pFloor->setMeshID( "Plane" );
                pFloor->setLocalPosition( float3{ 0.0f, environment._floorHeight, 0.0f } );
                pFloor->setLocalScale( float3{ environment._floorSize, 1.0f, environment._floorSize } );
                ++spawnedCount;
            }
        }
        if ( environment._bSky )
        {
            GameObject*   pSkyObject = pManager->createGameObject( hashed_string( "PreviewSky" ) );
            SkyComponent* pSky       = pSkyObject != nullptr ? pSkyObject->addComponent<SkyComponent>() : nullptr;
            if ( pSky != nullptr )
                ++spawnedCount;
        }
        return spawnedCount;
    }
} // namespace sw::editor
