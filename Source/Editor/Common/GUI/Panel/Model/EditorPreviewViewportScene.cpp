// 미리보기 뷰포트의 씬 · 렌더 타깃 · 카메라 계산 · 스크린숏입니다(ImGui 없음). 그리기는 `EditorPreviewViewport.cpp`(MVVM View)다.
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/Panel/EditorPreviewViewport.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorUICommandQueue.h"

#include "Engine/Environment/Sky/SkyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Renderer/Capture/CaptureReadiness.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Visual/Component/DirectionalLightComponent.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw::editor
{
    /**
     * @brief `-gv_editorPreviewScreenshot=<경로>`: 처음 열린 미리보기 뷰포트가 충분히 그린 뒤 자기 렌더 타깃을 이 경로에 한 번 찍습니다(PPM · PNG).
     * @details 시나리오의 `<Screenshot>` 은 주 출력만 찍는다 — 미리보기 씬이 그려지는지, 메인 씬과 섞이지 않는지는 이 그림으로 본다.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::GlobalDiskPath, gv_editorPreviewScreenshot, "", "미리보기 씬 뷰포트의 렌더 타깃을 이 경로에 한 번 찍는다 (시나리오 눈 확인용, 비우면 안 함)" );
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct EditorPreviewViewportInternal
        {
            static constexpr float32 kDefaultDistance     = 3.0f;
            static constexpr float32 kDefaultYaw          = 0.6f;
            static constexpr float32 kDefaultPitch        = 0.35f;
            static constexpr float32 kDefaultFieldOfViewY = 0.8f;
            static constexpr float32 kNearPlane           = 0.05f;
            static constexpr float32 kFarPlane            = 200.0f;
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorPreviewViewport::EditorPreviewViewport()
        : _pScene{ nullptr }
        , _imagePosition{}
        , _imageSize{}
        , _orbitTarget{ 0.0f, 0.0f, 0.0f }
        , _freeEye{ 0.0f, 0.0f, 0.0f }
        , _orbitDistance{ EditorPreviewViewportInternal::kDefaultDistance }
        , _orbitYaw{ EditorPreviewViewportInternal::kDefaultYaw }
        , _orbitPitch{ EditorPreviewViewportInternal::kDefaultPitch }
        , _fieldOfViewY{ EditorPreviewViewportInternal::kDefaultFieldOfViewY }
        , _sceneKey{ kInvalidSceneKey }
        , _targetID{ 0 }
        , _submittedFrameCount{ 0 }
        , _bFreeCamera{ SW_FALSE }
        , _bOwnsScene{ SW_FALSE }
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
        _bOwnsScene = SW_TRUE;
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
        _bOwnsScene                  = SW_FALSE;
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        _targetID                    = editorContext.getSubsystem<EditorRenderViews>().allocatePreviewTarget();
        return true;
    }

    void EditorPreviewViewport::shutdown()
    {
        EditorContext* pContext = editor::findSubsystem<EditorContext>();
        if ( _targetID != 0 && pContext != nullptr )
            EditorUICommandQueue::postToEditor( EditorUICommand::makeFreePreviewTarget( _targetID ) ); // 타깃은 게임 스레드가 가진다 — 놓기도 명령이다
        _targetID = 0;

        SceneManager* pSceneManager      = findSubsystem<SceneManager>();
        const bool    bDestroyOwnedScene = _bOwnsScene && _pScene != nullptr && pSceneManager != nullptr; // 붙인 씬은 가진 쪽이 정리한다
        if ( bDestroyOwnedScene )
            (void)pSceneManager->destroyPreviewScene( _pScene ); // 매니저가 먼저 종료했으면 그 shutdown 이 이미 정리했다
        _pScene              = nullptr;
        _bOwnsScene          = SW_FALSE;
        _sceneKey            = kInvalidSceneKey;
        _submittedFrameCount = 0;
    }

    uint64 EditorPreviewViewport::getRenderTarget() const
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        if ( _targetID == 0 )
            return 0;
        const EditorPreviewTargetState* pTarget = editorContext.getSubsystem<EditorRenderViews>().findUIPreviewTargetState( _targetID );
        return pTarget != nullptr ? pTarget->_state._renderTarget : 0;
    }

    void EditorPreviewViewport::setOrbit( const float3& target, float32 distance, float32 yaw, float32 pitch )
    {
        _bFreeCamera   = SW_FALSE;
        _orbitTarget   = target;
        _orbitDistance = MathUtil::max( EditorPreviewViewportInternal::kMinDistance, distance );
        _orbitYaw      = yaw;
        _orbitPitch    = MathUtil::clamp( pitch, -EditorPreviewViewportInternal::kMaxPitch, EditorPreviewViewportInternal::kMaxPitch );
    }

    void EditorPreviewViewport::setFreeCamera( const float3& eye, float32 yaw, float32 pitch, float32 fieldOfViewY )
    {
        _bFreeCamera = SW_TRUE;
        _freeEye     = eye;
        _orbitYaw    = yaw;
        _orbitPitch  = MathUtil::clamp( pitch, -EditorPreviewViewportInternal::kMaxPitch, EditorPreviewViewportInternal::kMaxPitch );
        if ( fieldOfViewY > 0.0f )
            _fieldOfViewY = fieldOfViewY;
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
        // 자유 카메라는 눈이 고정이고 오빗 대상 자리에 눈 + 시선(= -toCamera)을 둔다.
        outEye                  = _bFreeCamera ? _freeEye : _orbitTarget + toCamera * _orbitDistance;
        const float3 lookTarget = _bFreeCamera ? _freeEye - toCamera : _orbitTarget;
        outView                 = float4x4::makeLookAt( outEye, lookTarget, float3{ 0.0f, 1.0f, 0.0f } );
        outProjection           = float4x4::makePerspectiveFieldOfView( _fieldOfViewY, aspectRatio, EditorPreviewViewportInternal::kNearPlane, EditorPreviewViewportInternal::kFarPlane );
    }

    void* EditorPreviewViewport::acquireTargetTexture( const float2& size )
    {
        EditorRenderViews&              views        = editor::getSubsystem<EditorContext>().getSubsystem<EditorRenderViews>();
        const uint32                    targetWidth  = static_cast<uint32>( size._x );
        const uint32                    targetHeight = static_cast<uint32>( size._y );
        const EditorPreviewTargetState* pTarget      = views.findUIPreviewTargetState( _targetID );
        // 타깃은 게임 스레드가 만든다 — 크기가 다르거나 아직 없으면 명령을 보내고, 다음 UI 프레임부터 받은 값을 읽는다(같은 크기를 두 번 보내도 적용 쪽이 견준다).
        const bool bNeedsFit = _targetID != 0 && ( pTarget == nullptr || EditorViewTargetUtil::needsResize( pTarget->_state._requestedWidth, pTarget->_state._requestedHeight, targetWidth, targetHeight ) );
        if ( bNeedsFit )
            EditorUICommandQueue::postToEditor( EditorUICommand::makeFitPreviewTarget( _targetID, targetWidth, targetHeight ) );
        return pTarget != nullptr ? pTarget->_state._pTextureID : nullptr;
    }

    void EditorPreviewViewport::submitView()
    {
        EditorRenderViews&              views   = editor::getSubsystem<EditorContext>().getSubsystem<EditorRenderViews>();
        const EditorPreviewTargetState* pTarget = views.findUIPreviewTargetState( _targetID );
        if ( pTarget == nullptr )
            return;
        // 렌더 타깃의 실제 크기로 비율을 잡는다(다시 만들기 전 프레임은 옛 크기다).
        const float32 aspect = static_cast<float32>( pTarget->_state._width ) / static_cast<float32>( MathUtil::max( 1u, pTarget->_state._height ) );
        float4x4      viewProj{};
        float3        eye{};
        computeViewProjection( aspect, viewProj, eye );
        views.submitPreviewView( _targetID, _sceneKey, viewProj, eye );
        ++_submittedFrameCount;
        requestScreenshotIfAsked();
    }

    void EditorPreviewViewport::requestScreenshotIfAsked()
    {
        // 다 그려진 그림만 찍는다 — 주 화면 캡처와 같은 판정(`CaptureReadiness` — 이 씬의 에셋 일, 이 씬을 그린 하위 렌더러가 PSO 때문에 빠뜨린 배치). 프레임 수를 어림하지 않는다.
        const bool   bNotReady = _pScene == nullptr || CaptureReadiness::countPendingJobs( _pScene->getObjectManager(), _sceneKey ) != 0;
        const string path      = gv_editorPreviewScreenshot.get();
        if ( path.empty() || bNotReady )
            return;
        if ( requestImageFile( path ) == 0 )
            return;
        SW_LOG_INFO_ID( "PreviewViewport.Screenshot", "Preview viewport screenshot (scene key %#) -> %#", _sceneKey, path.c_str() );
        gv_editorPreviewScreenshot.set( {} ); // 한 번만
    }

    uint32 EditorPreviewViewport::requestImageFile( string_view path )
    {
        const uint64                 renderTarget = getRenderTarget();
        const bool                   bNotDrawn    = renderTarget == 0 || _submittedFrameCount == 0;
        const optional<AbsolutePath> absolutePath = AbsolutePath::tryMake( path );
        if ( absolutePath.has_value() == false || bNotDrawn )
            return 0;
        return RenderThread::requestScreenshot( *absolutePath, renderTarget, RHIFormat::R8G8B8A8_UNORM );
    }

    uint32 EditorPreviewViewport::requestImagePNGBytes( string_view requestName )
    {
        const uint64 renderTarget = getRenderTarget();
        const bool   bNotDrawn    = renderTarget == 0 || _submittedFrameCount == 0;
        if ( bNotDrawn )
            return 0;
        return RenderThread::requestScreenshotPNGBytes( requestName, renderTarget, RHIFormat::R8G8B8A8_UNORM );
    }

    bool EditorPreviewViewport::takeImagePNGBytes( uint32 ticket, vector<uint8>& outPNGBytes )
    {
        return RenderThread::takeScreenshotPNGBytes( ticket, outPNGBytes );
    }

    ScreenshotTicketState EditorPreviewViewport::findImageFileResult( uint32 ticket )
    {
        return RenderThread::findScreenshotResult( ticket );
    }

    bool EditorPreviewViewport::hasFloor() const
    {
        const GameObjectManager* pManager = _pScene != nullptr ? _pScene->getObjectManager() : nullptr;
        return pManager != nullptr && pManager->findGameObjectByName( hashed_string( "PreviewFloor" ) ) != nullptr;
    }

    void EditorPreviewViewport::setFloorVisible( bool bVisible )
    {
        GameObjectManager* pManager = _pScene != nullptr ? _pScene->getObjectManager() : nullptr;
        GameObject*        pFloor   = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( "PreviewFloor" ) ) : nullptr;
        if ( pFloor != nullptr && pFloor->isActive() != bVisible )
            pFloor->setActive( bVisible );
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
