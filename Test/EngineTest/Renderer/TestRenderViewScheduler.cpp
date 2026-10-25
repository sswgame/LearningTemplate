#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Frame/View/OcclusionTileSnapshot.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Frame/View/RenderViewCollector.h"
#include "Engine/Renderer/Frame/View/RenderViewScheduler.h"
#include "Engine/Scene/Scene.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

#include "TestFramework/TestFramework.h"

// 추가 뷰 스케줄러 — 갱신 주기로 쉬기 · 보이지 않으면 쉬기 · 프레임 예산(뷰 수 · 픽셀)과 굶지 않기 · 사라진 뷰 잊기. 수집기 — 모니터의 화면 크기로 보임 판정.

using namespace sw;

namespace
{
    /** @brief 추가 뷰 수집기 테스트의 씬입니다. 주 카메라는 원점에서 +Z 를 보고, 캡처 카메라의 모니터를 그 앞에 둡니다. */
    struct RenderViewCollectorTestScene
    {
        Scene            _scene{ "RenderViewCollectorTest" };
        CameraComponent* _pMainCamera{ nullptr };
        SceneComponent*  _pMonitor{ nullptr };
        CameraComponent* _pCapture{ nullptr };
        /// 주 시점 가림 타일 사본. nullptr 이면 가려짐을 보지 않는다.
        const OcclusionTileSnapshot* _pOcclusion{ nullptr };

        bool populate( float32 minScreenSize )
        {
            GameObject* pMainObject    = _scene.getObjectManager()->createGameObject( hashed_string( "MainCamera" ) );
            GameObject* pMonitorObject = _scene.getObjectManager()->createGameObject( hashed_string( "Monitor" ) );
            GameObject* pCaptureObject = _scene.getObjectManager()->createGameObject( hashed_string( "Cctv" ) );
            const bool  bObjectMissing = pMainObject == nullptr || pMonitorObject == nullptr || pCaptureObject == nullptr;
            if ( bObjectMissing )
                return false;
            _pMainCamera                 = pMainObject->addComponent<CameraComponent>();
            _pMonitor                    = pMonitorObject->addComponent<SceneComponent>();
            _pCapture                    = pCaptureObject->addComponent<CameraComponent>();
            const bool bComponentMissing = _pMainCamera == nullptr || _pMonitor == nullptr || _pCapture == nullptr;
            if ( bComponentMissing )
                return false;
            _pMainCamera->setRole( CameraRole::Game );
            _pMainCamera->setLocalPosition( float3{ 0.0f, 0.0f, 0.0f } );
            _pMainCamera->lookAt( float3{ 0.0f, 0.0f, 1.0f } );
            _pCapture->setRole( CameraRole::Capture );
            _pCapture->setLocalPosition( float3{ 20.0f, 0.0f, 0.0f } );
            CameraRenderOutput output;
            output._target              = CameraOutputTarget::RenderTexture;
            output._renderTexture       = "rendertarget/collector_test";
            output._renderTextureWidth  = 64;
            output._renderTextureHeight = 32;
            output._visibilityObject    = pMonitorObject->getHandle();
            output._minScreenSize       = minScreenSize;
            _pCapture->setRenderOutput( output );
            return true;
        }

        /** @brief 모니터를 @p position 에 두고 한 프레임 모읍니다. 캡처 뷰 요청을 돌려줍니다(없으면 기본값). */
        RenderViewRequest collectAt( const float3& position, RenderViewScheduler& scheduler, float64 now )
        {
            _pMonitor->setLocalPosition( position );
            vector<RenderViewRequest> listView;
            const float4x4            mainViewProj = _pMainCamera->getViewProjectionMatrix( 16.0f / 9.0f );
            RenderViewCollector::collectExtraViews( *_scene.getObjectManager(), _pMainCamera, mainViewProj, _pMainCamera->getCameraPosition(), 1280, 720, 1.0f, now,
                                                    RenderViewBudget{}, scheduler, _pOcclusion, listView );
            return listView.size() == 1 ? listView[0] : RenderViewRequest{};
        }
    };
} // namespace

/**
 * @brief [RenderViewSchedulerTest] 10 Hz 뷰는 60 Hz 프레임에서 여섯 프레임마다 한 번 그리고, 0 Hz 뷰는 매 프레임 그린다
 */
SW_TEST_CASE( RenderViewSchedulerTest, UpdateRateSkipsFrames )
{
    RenderViewScheduler            scheduler;
    RenderViewScheduler::Candidate arrCandidate[2]{};
    arrCandidate[0]._viewID     = 1;
    arrCandidate[0]._updateRate = 10.0f;
    arrCandidate[1]._viewID     = 2;
    arrCandidate[1]._updateRate = 0.0f;

    uint32 arrRenderCount[2]{};
    uint32 firstRenders[8]{};
    uint32 firstRenderCount = 0;
    for ( uint32 frameIndex = 0; frameIndex < 60; ++frameIndex )
    {
        uint8 arrRender[2]{};
        (void)scheduler.schedule( static_cast<float64>( frameIndex ) / 60.0, arrCandidate, 2, RenderViewBudget{}, arrRender );
        for ( uint32 index = 0; index < 2; ++index )
        {
            arrRenderCount[index] += arrRender[index] == SW_TRUE ? 1u : 0u;
        }
        if ( arrRender[0] == SW_TRUE && firstRenderCount < 8 )
            firstRenders[firstRenderCount++] = frameIndex;
    }
    SW_EXPECT_EQUAL( 10u, arrRenderCount[0] );
    SW_EXPECT_EQUAL( 60u, arrRenderCount[1] );
    SW_EXPECT_EQUAL( 0u, firstRenders[0] );
    SW_EXPECT_EQUAL( 6u, firstRenders[1] );
    SW_EXPECT_EQUAL( 12u, firstRenders[2] );
}

/**
 * @brief [RenderViewSchedulerTest] 보이지 않는 뷰는 그리지 않고, 보이게 되면 그 프레임에 바로 그린다(쉬던 시간이 이미 지났다)
 */
SW_TEST_CASE( RenderViewSchedulerTest, InvisibleViewsWait )
{
    RenderViewScheduler            scheduler;
    RenderViewScheduler::Candidate candidate;
    candidate._viewID     = 7;
    candidate._updateRate = 2.0f;
    candidate._bVisible   = SW_FALSE;
    uint8 bRender         = SW_FALSE;
    for ( uint32 frameIndex = 0; frameIndex < 30; ++frameIndex )
    {
        (void)scheduler.schedule( static_cast<float64>( frameIndex ) / 30.0, &candidate, 1, RenderViewBudget{}, &bRender );
        SW_EXPECT_TRUE( bRender == SW_FALSE );
    }
    candidate._bVisible = SW_TRUE;
    (void)scheduler.schedule( 1.0, &candidate, 1, RenderViewBudget{}, &bRender );
    SW_EXPECT_TRUE( bRender == SW_TRUE );
    (void)scheduler.schedule( 1.1, &candidate, 1, RenderViewBudget{}, &bRender );
    SW_EXPECT_TRUE( bRender == SW_FALSE ); // 2 Hz — 0.5 초가 안 지났다
}

/**
 * @brief [RenderViewSchedulerTest] 예산보다 많은 뷰가 때가 되면 가장 오래 기다린 것부터 그리고, 밀린 뷰는 다음 프레임에 먼저 그린다(굶지 않는다)
 */
SW_TEST_CASE( RenderViewSchedulerTest, BudgetRotatesWithoutStarving )
{
    RenderViewScheduler            scheduler;
    RenderViewScheduler::Candidate arrCandidate[3]{};
    for ( uint32 index = 0; index < 3; ++index )
    {
        arrCandidate[index]._viewID = 100 + index;
    }

    uint32 arrRenderCount[3]{};
    for ( uint32 frameIndex = 0; frameIndex < 30; ++frameIndex )
    {
        uint8        arrRender[3]{};
        const uint32 chosen = scheduler.schedule( static_cast<float64>( frameIndex ) / 60.0, arrCandidate, 3, RenderViewBudget{ 2, 0 }, arrRender );
        SW_EXPECT_EQUAL( 2u, chosen );
        for ( uint32 index = 0; index < 3; ++index )
        {
            arrRenderCount[index] += arrRender[index] == SW_TRUE ? 1u : 0u;
        }
    }
    // 예산 둘을 셋이 나눠 쓴다 — 셋 다 20 번(±1) 근처다. 늦은 쪽을 먼저 고르지 않으면 한 뷰가 0 번이다.
    for ( uint32 index = 0; index < 3; ++index )
    {
        SW_EXPECT_TRUE_MSG( 19u <= arrRenderCount[index] && arrRenderCount[index] <= 21u, ( "뷰가 굶었다: " + to_string( arrRenderCount[index] ) ).c_str() );
    }
}

/**
 * @brief [RenderViewSchedulerTest] 후보에서 빠진 뷰(카메라가 사라졌다)는 잊고, 다시 나타나면 처음 보는 뷰처럼 바로 그린다
 */
SW_TEST_CASE( RenderViewSchedulerTest, ForgetsViewsThatDisappear )
{
    RenderViewScheduler            scheduler;
    RenderViewScheduler::Candidate candidate;
    candidate._viewID     = 9;
    candidate._updateRate = 0.5f;
    uint8 bRender         = SW_FALSE;
    (void)scheduler.schedule( 0.0, &candidate, 1, RenderViewBudget{}, &bRender );
    SW_EXPECT_TRUE( bRender == SW_TRUE );
    SW_EXPECT_EQUAL( 1u, scheduler.getTrackedCount() );
    (void)scheduler.schedule( 0.1, &candidate, 0, RenderViewBudget{}, &bRender );
    SW_EXPECT_EQUAL( 0u, scheduler.getTrackedCount() );
    (void)scheduler.schedule( 0.2, &candidate, 1, RenderViewBudget{}, &bRender );
    SW_EXPECT_TRUE( bRender == SW_TRUE );
}

/**
 * @brief [RenderViewSchedulerTest] 픽셀 예산 — 큰 뷰 하나가 예산을 넘어도 차례가 오면 그리고(굶지 않는다), 작은 뷰가 큰 뷰를 건너뛰어 예산을 채우지 않는다
 * @details 큰 뷰(예산의 1.5 배)와 작은 뷰 둘(각 예산의 0.4 배). 작은 뷰 둘은 한 프레임에 같이 들어가고, 큰 뷰는 혼자 그린다 — 세 뷰가 모두 그려진다.
 */
SW_TEST_CASE( RenderViewSchedulerTest, PixelBudgetDoesNotStarveLargeViews )
{
    RenderViewScheduler            scheduler;
    RenderViewScheduler::Candidate arrCandidate[3]{};
    arrCandidate[0]._viewID     = 1;
    arrCandidate[0]._pixelCount = 1500;
    arrCandidate[1]._viewID     = 2;
    arrCandidate[1]._pixelCount = 400;
    arrCandidate[2]._viewID     = 3;
    arrCandidate[2]._pixelCount = 400;
    const RenderViewBudget budget{ 0, 1000 };

    uint32 arrRenderCount[3]{};
    for ( uint32 frameIndex = 0; frameIndex < 40; ++frameIndex )
    {
        uint8 arrRender[3]{};
        (void)scheduler.schedule( static_cast<float64>( frameIndex ) / 60.0, arrCandidate, 3, budget, arrRender );
        // 큰 뷰는 언제나 혼자다 — 같이 그리면 예산을 넘는다.
        SW_EXPECT_TRUE( arrRender[0] == SW_FALSE || ( arrRender[1] == SW_FALSE && arrRender[2] == SW_FALSE ) );
        for ( uint32 index = 0; index < 3; ++index )
        {
            arrRenderCount[index] += arrRender[index] == SW_TRUE ? 1u : 0u;
        }
    }
    // 두 분기(큰 뷰 / 작은 뷰 둘)가 번갈아 — 셋 다 20 번(±1) 근처다. 건너뛰어 채우면 큰 뷰가 0 번이다.
    for ( uint32 index = 0; index < 3; ++index )
    {
        SW_EXPECT_TRUE_MSG( 19u <= arrRenderCount[index] && arrRenderCount[index] <= 21u, ( "뷰가 굶었다: " + to_string( arrRenderCount[index] ) ).c_str() );
    }
}

/**
 * @brief [RenderViewSchedulerTest] 예산을 받지 않는 뷰(화면 사각형)는 예산이 차도 때가 되면 그리고, 예산을 쓰지도 않는다
 */
SW_TEST_CASE( RenderViewSchedulerTest, UnbudgetedViewsIgnoreTheBudget )
{
    RenderViewScheduler            scheduler;
    RenderViewScheduler::Candidate arrCandidate[3]{};
    arrCandidate[0]._viewID     = 1;
    arrCandidate[0]._bBudgeted  = SW_FALSE;
    arrCandidate[0]._pixelCount = 5000;
    arrCandidate[1]._viewID     = 2;
    arrCandidate[2]._viewID     = 3;
    const RenderViewBudget budget{ 1, 1000 };
    for ( uint32 frameIndex = 0; frameIndex < 10; ++frameIndex )
    {
        uint8        arrRender[3]{};
        const uint32 chosen = scheduler.schedule( static_cast<float64>( frameIndex ) / 60.0, arrCandidate, 3, budget, arrRender );
        SW_EXPECT_TRUE( arrRender[0] == SW_TRUE );
        SW_EXPECT_TRUE( ( arrRender[1] == SW_TRUE ) != ( arrRender[2] == SW_TRUE ) ); // 예산 하나를 둘이 번갈아 쓴다
        SW_EXPECT_EQUAL( 2u, chosen );
    }
}

/**
 * @brief [RenderViewCollectorTest] 모니터가 주 시점에 `_minScreenSize` 이상으로 보일 때만 캡처 뷰를 그린다 — 멀어 작거나, 뒤에 있으면 쉰다
 * @details 반지름 1 의 모니터를 시야각 0.7 rad 카메라 앞 10 m 에 두면 화면 크기는 약 0.27, 50 m 면 약 0.055 다. 임계값은 0.1 이다.
 */
SW_TEST_CASE( RenderViewCollectorTest, VisibilityNeedsOnScreenSize )
{
    RenderViewCollectorTestScene stage;
    SW_ASSERT_TRUE( stage.populate( 0.1f ) );
    RenderViewScheduler scheduler;

    const RenderViewRequest nearRequest = stage.collectAt( float3{ 0.0f, 0.0f, 10.0f }, scheduler, 0.0 );
    SW_EXPECT_EQUAL( stage._pCapture->getComponentID(), nearRequest._viewID );
    SW_EXPECT_TRUE( nearRequest._bRender == SW_TRUE );
    SW_EXPECT_TRUE( nearRequest._outputKind == RenderViewOutputKind::RenderTexture );

    const RenderViewRequest farRequest = stage.collectAt( float3{ 0.0f, 0.0f, 50.0f }, scheduler, 1.0 );
    SW_EXPECT_EQUAL( stage._pCapture->getComponentID(), farRequest._viewID ); // 쉬는 뷰도 실린다
    SW_EXPECT_TRUE( farRequest._bRender == SW_FALSE );

    const RenderViewRequest behindRequest = stage.collectAt( float3{ 0.0f, 0.0f, -10.0f }, scheduler, 2.0 );
    SW_EXPECT_TRUE( behindRequest._bRender == SW_FALSE );

    const RenderViewRequest backRequest = stage.collectAt( float3{ 0.0f, 0.0f, 10.0f }, scheduler, 3.0 );
    SW_EXPECT_TRUE( backRequest._bRender == SW_TRUE );
}

/**
 * @brief [RenderViewCollectorTest] 주 시점의 가림 타일 사본이 모니터를 가리면 캡처 뷰를 쉬고, 가리지 않거나 사본이 없으면 그린다(지난 프레임에 그려졌나)
 * @details 모니터는 주 카메라 앞 40 m(화면 크기 약 0.069 — 임계값 0.05 이상)이고 타일 몇 개에 든다. 타일 깊이 0 은 카메라 바로 앞의 벽이라 가리고, 1 은 가리지 않는다.
 */
SW_TEST_CASE( RenderViewCollectorTest, OccludedMonitorStopsTheViewOnlyWithASnapshot )
{
    RenderViewCollectorTestScene stage;
    SW_ASSERT_TRUE( stage.populate( 0.05f ) );
    RenderViewScheduler scheduler;
    const float3        monitor{ 0.0f, 0.0f, 40.0f };

    OcclusionTileSnapshot snapshot;
    snapshot._viewProj    = stage._pMainCamera->getViewProjectionMatrix( 16.0f / 9.0f );
    snapshot._columns     = 80;
    snapshot._rows        = 45;
    snapshot._tileSize    = 16;
    snapshot._depthWidth  = 1280.0f;
    snapshot._depthHeight = 720.0f;
    snapshot._listTileDepth.assign( 80u * 45u, 0.0f );

    stage._pOcclusion                  = nullptr;
    const RenderViewRequest noSnapshot = stage.collectAt( monitor, scheduler, 0.0 );
    SW_EXPECT_TRUE( noSnapshot._bRender == SW_TRUE ); // 사본이 없으면 가려짐을 보지 않는다

    stage._pOcclusion                = &snapshot;
    const RenderViewRequest occluded = stage.collectAt( monitor, scheduler, 1.0 );
    SW_EXPECT_EQUAL( stage._pCapture->getComponentID(), occluded._viewID ); // 쉬는 뷰도 실린다
    SW_EXPECT_TRUE( occluded._bRender == SW_FALSE );

    snapshot._listTileDepth.assign( 80u * 45u, 1.0f ); // 벽이 사라졌다
    const RenderViewRequest open = stage.collectAt( monitor, scheduler, 2.0 );
    SW_EXPECT_TRUE( open._bRender == SW_TRUE );
}

/**
 * @brief [RenderViewCollectorTest] 보임 오브젝트가 없으면(핸들이 가리키는 오브젝트가 없다) 볼 것이 없으므로 그리지 않는다
 */
SW_TEST_CASE( RenderViewCollectorTest, MissingVisibilityObjectStopsTheView )
{
    RenderViewCollectorTestScene stage;
    SW_ASSERT_TRUE( stage.populate( 0.0f ) );
    CameraRenderOutput output = stage._pCapture->getRenderOutput();
    output._visibilityObject  = GameObjectHandle::make( 0x7FFFFFF0u );
    stage._pCapture->setRenderOutput( output );
    RenderViewScheduler     scheduler;
    const RenderViewRequest request = stage.collectAt( float3{ 0.0f, 0.0f, 10.0f }, scheduler, 0.0 );
    SW_EXPECT_EQUAL( stage._pCapture->getComponentID(), request._viewID );
    SW_EXPECT_TRUE( request._bRender == SW_FALSE );
}

/**
 * @brief [RenderViewCollectorTest] 카메라의 후처리 값과 컷 표시가 주 시점 · 추가 뷰 설정에 실리고, 컷은 한 번만 읽힌다
 * @details 렌더러는 뷰 설정만 본다(`FrameRenderer::applyViewPassConstants` 가 g_CameraGrade · g_CameraFrame 을 채운다). 카메라에서 뷰로 가는 값이 하나라도 빠지면
 *          그 카메라의 노출 · 레터박스가 화면에 나오지 않는다.
 */
SW_TEST_CASE( RenderViewCollectorTest, CameraPostProcessAndCutReachTheViewSettings )
{
    RenderViewCollectorTestScene stage;
    SW_ASSERT_TRUE( stage.populate( 0.0f ) );
    CameraPostProcessValues mainPost;
    mainPost._exposureCompensation = 1.5f;
    mainPost._letterboxAspectRatio = 2.39f;
    stage._pMainCamera->setPostProcess( mainPost );
    stage._pMainCamera->markCut();
    CameraPostProcessValues capturePost;
    capturePost._saturation = 0.0f;
    stage._pCapture->setPostProcess( capturePost );

    const RenderViewSettings mainSettings = RenderViewCollector::makeMainSettings( stage._pMainCamera, 1.0f );
    SW_EXPECT_NEAR_EQUAL( 1.5f, mainSettings._postProcess._exposureCompensation, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 2.39f, mainSettings._postProcess._letterboxAspectRatio, 1e-6f );
    SW_EXPECT_TRUE( mainSettings._bCut == SW_TRUE );
    SW_EXPECT_TRUE( RenderViewCollector::makeMainSettings( stage._pMainCamera, 1.0f )._bCut == SW_FALSE );

    RenderViewScheduler     scheduler;
    const RenderViewRequest request = stage.collectAt( float3{ 0.0f, 0.0f, 10.0f }, scheduler, 0.0 );
    SW_EXPECT_EQUAL( stage._pCapture->getComponentID(), request._viewID );
    SW_EXPECT_NEAR_EQUAL( 0.0f, request._settings._postProcess._saturation, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, request._settings._postProcess._exposureCompensation, 1e-6f );
}

/**
 * @brief [RenderViewCollectorTest] 카메라의 피사계 심도 렌즈 값(초점 거리 · 조리개)이 주 시점과 추가 뷰의 설정에 실린다 — 카메라마다 따로
 */
SW_TEST_CASE( RenderViewCollectorTest, CameraDepthOfFieldReachesTheViewSettings )
{
    RenderViewCollectorTestScene stage;
    SW_ASSERT_TRUE( stage.populate( 0.0f ) );
    CameraDepthOfFieldValues mainLens;
    mainLens._focusDistance = 3.5f;
    mainLens._aperture      = 1.4f;
    stage._pMainCamera->setDepthOfField( mainLens );

    const RenderViewSettings mainSettings = RenderViewCollector::makeMainSettings( stage._pMainCamera, 1.0f );
    SW_EXPECT_NEAR_EQUAL( 3.5f, mainSettings._depthOfField._focusDistance, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.4f, mainSettings._depthOfField._aperture, 1e-6f );
    // 착란원의 화면 크기는 시야각에서 나온 투영 배율을 함께 쓴다(원근이면 1 / tan( fovY / 2 ), 직교면 0 — 피사계 심도 없음).
    SW_EXPECT_NEAR_EQUAL( 1.0f / MathUtil::tan( stage._pMainCamera->getFieldOfViewY() * 0.5f ), mainSettings._projectionScaleY, 1e-5f );
    stage._pMainCamera->setOrthographic( true );
    SW_EXPECT_NEAR_EQUAL( 0.0f, RenderViewCollector::makeMainSettings( stage._pMainCamera, 1.0f )._projectionScaleY, 1e-6f );
    stage._pMainCamera->setOrthographic( false );

    RenderViewScheduler     scheduler;
    const RenderViewRequest request = stage.collectAt( float3{ 0.0f, 0.0f, 10.0f }, scheduler, 0.0 );
    SW_EXPECT_EQUAL( stage._pCapture->getComponentID(), request._viewID );
    const CameraDepthOfFieldValues defaults;
    SW_EXPECT_NEAR_EQUAL( defaults._focusDistance, request._settings._depthOfField._focusDistance, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( defaults._aperture, request._settings._depthOfField._aperture, 1e-6f );
}

/**
 * @brief [RenderViewCollectorTest] 자동 렌더 배율은 화면에 나가는 뷰(주 시점 · 화면 사각형)의 내부 해상도에 곱해지고 렌더 텍스처 뷰에는 곱해지지 않는다
 * @details 사용자 렌더 스케일(`gv_renderScale`)과 같은 규칙이다. 렌더 텍스처 뷰는 자기 크기가 데이터다.
 */
SW_TEST_CASE( RenderViewCollectorTest, AutoRenderScaleFollowsScreenViewsOnly )
{
    RenderViewCollectorTestScene stage;
    SW_ASSERT_TRUE( stage.populate( 0.0f ) );
    const float32 userScale = static_cast<float32>( gv_renderScale );
    SW_EXPECT_NEAR_EQUAL( userScale * 0.6f, RenderViewCollector::makeMainSettings( stage._pMainCamera, 0.6f )._resolutionScale, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( userScale, RenderViewCollector::makeMainSettings( stage._pMainCamera, 1.0f )._resolutionScale, 1e-5f );

    const float4x4 mainViewProj = stage._pMainCamera->getViewProjectionMatrix( 16.0f / 9.0f );
    const float3   mainPos      = stage._pMainCamera->getCameraPosition();
    {
        // 렌더 텍스처 뷰: 배율 1 과 같다.
        RenderViewScheduler       scheduler;
        vector<RenderViewRequest> listView;
        RenderViewCollector::collectExtraViews( *stage._scene.getObjectManager(), stage._pMainCamera, mainViewProj, mainPos, 1280, 720, 0.6f, 0.0, RenderViewBudget{}, scheduler,
                                                nullptr, listView );
        SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listView.size() ) );
        SW_EXPECT_NEAR_EQUAL( 1.0f, listView[0]._settings._resolutionScale, 1e-5f );
    }
    {
        // 화면 사각형 뷰: 주 시점과 같이 곱해진다.
        CameraRenderOutput output;
        output._target     = CameraOutputTarget::ScreenRect;
        output._screenRect = float4{ 0.5f, 0.0f, 0.5f, 0.5f };
        stage._pCapture->setRenderOutput( output );
        RenderViewScheduler       scheduler;
        vector<RenderViewRequest> listView;
        RenderViewCollector::collectExtraViews( *stage._scene.getObjectManager(), stage._pMainCamera, mainViewProj, mainPos, 1280, 720, 0.6f, 0.0, RenderViewBudget{}, scheduler,
                                                nullptr, listView );
        SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listView.size() ) );
        SW_EXPECT_NEAR_EQUAL( userScale * 0.6f, listView[0]._settings._resolutionScale, 1e-5f );
    }
}

/**
 * @brief [RenderViewCollectorTest] 씬 직접 경로(`kDirectPathAutoScale`)와 호스트 뷰는 사용자 렌더 스케일 · 자동 렌더 배율을 곱하지 않는다 — 직접 경로는 결정적이어야 한다
 * @details 직접 경로는 자동 배율을 곱하지 않고(사용자 스케일만), 호스트 타깃(에디터 씬 뷰)은 타깃 크기 그대로 그려 둘 다 곱하지 않는다.
 */
SW_TEST_CASE( RenderViewCollectorTest, DirectPathAndHostViewSkipAutoScale )
{
    GlobalVariableManager& variables = engine::getSubsystem<GlobalVariableManager>();
    GlobalVariableInfo*    pScale    = variables.findVariable( "gv_renderScale" );
    SW_ASSERT_TRUE( pScale != nullptr );
    const string previousScale = pScale->getValueAsString();
    SW_ASSERT_TRUE( pScale->setValueAsFloat( 0.5f ) );

    RenderViewCollectorTestScene stage;
    SW_ASSERT_TRUE( stage.populate( 0.0f ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, RenderViewCollector::kDirectPathAutoScale, 1e-6f );
    // 직접 경로: 사용자 스케일만(0.5) — 자동 배율이 켜져 0.6 이 되는 일이 없다.
    SW_EXPECT_NEAR_EQUAL( 0.5f, RenderViewCollector::makeMainSettings( stage._pMainCamera, RenderViewCollector::kDirectPathAutoScale )._resolutionScale, 1e-5f );

    // 호스트 뷰: 사용자 스케일이 0.5 여도 카메라 출력의 배율(1) 그대로다.
    HostViewTarget target;
    target._renderTarget = 1;
    target._width        = 640;
    target._height       = 360;
    vector<RenderViewRequest> listView;
    RenderViewCollector::appendHostView( *stage._pMainCamera, target, RenderViewMode::Lit, listView );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listView.size() ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listView[0]._settings._resolutionScale, 1e-5f );

    (void)pScale->setValueAsString( previousScale );
}

/**
 * @brief [RenderViewSchedulerTest] 출력 사각형이 전체인지의 판정 — 주 시점 Present 가 뷰포트를 걸지(사각형) 말지(전체)를 가른다
 */
SW_TEST_CASE( RenderViewSchedulerTest, FullRectIsDetected )
{
    RenderViewSettings settings;
    SW_EXPECT_TRUE( settings.isFullRect() );
    settings._screenRect = float4{ 0.5f, 0.0f, 0.5f, 1.0f };
    SW_EXPECT_FALSE( settings.isFullRect() );
}

/**
 * @brief [RenderViewModeTest] 보기 모드 표의 이름이 겹치지 않고, LightingOnly 는 셰이더 define 을 갖되 톤맵 · 후처리를 그대로 쓴다
 * @details 툴바와 셀프 테스트 표식(`viewport.viewMode.<이름>`)이 이름으로 모드를 찾으므로 이름이 겹치면 다른 모드를 고른다.
 *          쿠커는 define 이 있는 모드만 변형을 베이크한다 — define 이 비면 Shipping 에서 PSO 가 없다.
 */
SW_TEST_CASE( RenderViewModeTest, NamesAreUniqueAndLightingOnlyIsShaded )
{
    const uint32 modeCount = static_cast<uint32>( RenderViewMode::Count );
    for ( uint32 first = 0; first < modeCount; ++first )
    {
        for ( uint32 second = first + 1; second < modeCount; ++second )
        {
            const string firstName( getRenderViewModeInfo( static_cast<RenderViewMode>( first ) )._pName );
            const string secondName( getRenderViewModeInfo( static_cast<RenderViewMode>( second ) )._pName );
            SW_EXPECT_TRUE( firstName != secondName );
        }
    }

    const RenderViewModeInfo& lightingOnly = getRenderViewModeInfo( RenderViewMode::LightingOnly );
    SW_EXPECT_TRUE( string( lightingOnly._pName ) == string( "LightingOnly" ) );
    SW_EXPECT_TRUE( lightingOnly._pShaderDefine != nullptr );
    SW_EXPECT_FALSE( lightingOnly._bShowsBuffer );
    SW_EXPECT_FALSE( lightingOnly._bWireframe );
    SW_EXPECT_FALSE( lightingOnly._bAdditiveNoDepth );

    // ShadedWireframe 은 겹침 모드다 — Lit 위에 선(와이어프레임 채우기 + 단색 define)을 한 번 더 그리고 후처리는 그대로다.
    const RenderViewModeInfo& shadedWireframe = getRenderViewModeInfo( RenderViewMode::ShadedWireframe );
    SW_EXPECT_TRUE( shadedWireframe._bOverlay );
    SW_EXPECT_TRUE( shadedWireframe._bWireframe );
    SW_EXPECT_TRUE( shadedWireframe._pShaderDefine != nullptr );
    SW_EXPECT_FALSE( shadedWireframe._bShowsBuffer );
    SW_EXPECT_FALSE( lightingOnly._bOverlay );

    // 밖에서 들어온 번호는 거른다 — 범위 밖을 Lit 으로 바꾸지 않는다.
    SW_EXPECT_TRUE( isRenderViewModeIndex( 0 ) );
    SW_EXPECT_TRUE( isRenderViewModeIndex( static_cast<int32>( RenderViewMode::Count ) - 1 ) );
    SW_EXPECT_FALSE( isRenderViewModeIndex( static_cast<int32>( RenderViewMode::Count ) ) );
    SW_EXPECT_FALSE( isRenderViewModeIndex( -1 ) );
}

/**
 * @brief [RenderViewModeTest] 버퍼 보기 Roughness · Metallic · Mipmaps 는 셰이더 define 을 갖고 표면 값을 그대로 보인다(톤맵 · 후처리 없음)
 * @details 셰이더(binding.hlsli)가 그 define 으로 조명을 건너뛰고 값을 색으로 낸다. 톤맵이 걸리면 회색 값이 바뀌어 읽을 수 없다.
 */
SW_TEST_CASE( RenderViewModeTest, BufferVisualizationModesShowRawValues )
{
    const RenderViewMode arrMode[]   = { RenderViewMode::Roughness, RenderViewMode::Metallic, RenderViewMode::Mipmaps };
    const utf8*          arrDefine[] = { "SW_VIEWMODE_ROUGHNESS=1", "SW_VIEWMODE_METALLIC=1", "SW_VIEWMODE_MIPMAPS=1" };
    for ( uint32 modeIndex = 0; modeIndex < 3; ++modeIndex )
    {
        const RenderViewModeInfo& info = getRenderViewModeInfo( arrMode[modeIndex] );
        SW_EXPECT_TRUE( info._pShaderDefine != nullptr && string( info._pShaderDefine ) == string( arrDefine[modeIndex] ) );
        SW_EXPECT_TRUE( info._bShowsBuffer );
        SW_EXPECT_FALSE( info._bWireframe );
        SW_EXPECT_FALSE( info._bAdditiveNoDepth );
    }
}
