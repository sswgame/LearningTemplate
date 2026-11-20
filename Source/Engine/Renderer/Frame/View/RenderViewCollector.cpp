#include "pch.h"

#include "Engine/Renderer/Frame/View/RenderViewCollector.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Scene/AnimationLOD.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Registry/ScenePrimitive.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/View/OcclusionTileSnapshot.h"
#include "Engine/UserSettings/UserSettingsVariables.h"
#include "Engine/Visual/Component/MeshComponent.h"
#include "Engine/Visual/Component/PostProcessVolumeComponent.h"

namespace sw
{
    /**
     * @brief `-gv_renderViewBudget=<n>`: 한 프레임에 그리는 추가 뷰(CCTV, 백미러, PiP)의 최대 수입니다(0 = 제한 없음).
     * @details 넘치면 가장 오래 기다린 뷰부터 그리고 나머지는 다음 프레임으로 미룬다(굶지 않는다. `RenderViewScheduler`).
     */
    SW_GLOBAL_VARIABLE( int32, gv_renderViewBudget, 4, "한 프레임에 그리는 렌더 텍스처 뷰(CCTV · 백미러)의 최대 수 (0 = 제한 없음)" );
    /**
     * @brief `-gv_renderViewMegapixelBudget=<n>`: 한 프레임에 그리는 렌더 텍스처 뷰들의 내부 해상도 픽셀 합의 상한(백만 픽셀, 0 = 제한 없음)입니다. 기본 2 는 1080p 한 장 남짓이다.
     * @details 뷰 수 예산과 함께 건다. 가장 늦은 뷰 하나는 이 값보다 커도 그린다(`RenderViewScheduler`).
     */
    SW_GLOBAL_VARIABLE( float32, gv_renderViewMegapixelBudget, 2.0f, "한 프레임에 그리는 렌더 텍스처 뷰의 픽셀 합 상한, 백만 픽셀 (0 = 제한 없음)" );

    namespace
    {
        struct RenderViewCollectorInternal
        {
            /// 정수 업스케일 배율의 상한입니다(`CameraRenderOutput::_upscaleZoom` 의 Max 와 같다 — 넘는 값은 데이터 오류라 자른다).
            static constexpr uint32 kMaxUpscaleZoom = 16;
            /// 배율이 1 인지 보는 허용 오차입니다(부동소수 저장 오차만 받는다).
            static constexpr float32 kUnitScaleTolerance = 1.0e-4f;
            /// 백만 픽셀 단위 예산을 픽셀 수로 바꾸는 비율입니다.
            static constexpr float64 kPixelsPerMegapixel = 1.0e6;
            /**
             * @brief 카메라 하나의 뷰 설정입니다 — 출력 설정, 후처리 값, 피사계 심도 렌즈 값, 컷 표시(읽고 지운다).
             * @details 세 뷰(주 시점 · 추가 뷰 · 호스트 뷰)가 모두 이 함수로 만든다. 카메라에서 뷰로 가는 값이 늘면 여기 한 곳에만 더한다.
             *          해상도 배율(`_resolutionScale`)은 일부러 옮기지 않는다 — Present 가 출력 크기로 업샘플하는 화면 출력 뷰만 `applyScreenRenderScale` 로 넣고,
             *          업샘플이 없는 뷰(렌더 텍스처 · 호스트 타깃)는 1 이어야 하며 아니면 `hasUnitResolutionScale` 이 거절한다.
             */
            /**
             * @brief 카메라 자신의 후처리 값 위에 카메라가 선 자리의 후처리 볼륨들을 우선순위 순으로 섞은 값입니다(`PostProcessVolumeComponent::resolveAt`).
             * @details 언리얼은 볼륨을 섞은 뒤 카메라 값을 덮지만 여기서는 카메라 값이 바탕이다 — 카메라 값은 프리셋 · 포즈 블렌드가 쓰는 샷의 룩이고, 볼륨은 장소의 룩이다.
             */
            static PostProcessValues resolvePostProcess( const CameraComponent& camera )
            {
                const GameObject*        pOwner   = camera.getOwner();
                const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
                if ( pManager == nullptr )
                    return camera.getPostProcess();
                return PostProcessVolumeComponent::resolveAt( pManager->getComponentRegistry(), camera.getWorldPosition(), camera.getPostProcess() );
            }

            static RenderViewSettings makeSettings( CameraComponent& camera )
            {
                const CameraRenderOutput& output = camera.getRenderOutput();
                RenderViewSettings        settings;
                settings._screenRect       = output._screenRect;
                settings._postProcess      = resolvePostProcess( camera );
                settings._depthOfField     = camera.getDepthOfField();
                const bool bPerspective    = camera.isOrthographic() == false && camera.getFieldOfViewY() > 0.0f;
                settings._projectionScaleY = bPerspective ? 1.0f / MathUtil::tan( camera.getFieldOfViewY() * 0.5f ) : 0.0f;
                settings._upscaleZoom      = static_cast<uint8>( MathUtil::min( output._upscaleZoom, kMaxUpscaleZoom ) );
                settings._bShadows         = output._bShadows ? SW_TRUE : SW_FALSE;
                settings._bPostProcess     = output._bPostProcess ? SW_TRUE : SW_FALSE;
                settings._bCut             = camera.consumeCut( CameraCutReader::Render ) ? SW_TRUE : SW_FALSE;
                return settings;
            }

            /**
             * @brief 화면에 나가는 뷰(주 시점 · 화면 사각형)의 풀 배율 — 카메라 내부 해상도에 사용자 렌더 스케일(`gv_renderScale`, 옵션 메뉴 graphics.renderScale)을 곱합니다.
             * @details Present 가 출력 크기로 선형 업샘플한다. 렌더 텍스처 뷰는 자기 크기가 데이터라 곱하지 않고, 정수 업스케일 뷰는 배율을 보지 않는다.
             *          자동 렌더 배율(`RenderScaleAdjuster`)은 풀이 아니라 그리는 사각형만 줄인다(`RenderViewSettings::_rectScale` — 풀을 다시 만들지 않는다).
             */
            static float32 applyScreenRenderScale( float32 cameraScale )
            {
                return MathUtil::clamp( cameraScale * static_cast<float32>( gv_renderScale ), 0.1f, 2.0f );
            }

            /**
             * @brief Present 업샘플이 없는 뷰(렌더 텍스처 · 호스트 타깃)의 카메라 출력 배율이 1 인지 봅니다. 아니면 오류를 남기고 false 를 돌려준다.
             * @details 풀만 작아지고 이미지는 타깃 크기로 늘어나므로 조용히 1 로 바꾸지 않고 그 뷰를 거절한다(데이터 오류를 드러낸다).
             */
            static bool hasUnitResolutionScale( const CameraComponent& camera, string_view viewKind )
            {
                const float32 scale = camera.getRenderOutput()._resolutionScale;
                if ( MathUtil::abs( scale - 1.0f ) <= kUnitScaleTolerance )
                    return true;
                SW_LOG_ERROR_ID( "RenderViewCollector.CameraResolutionScaleView", "Camera %# has resolution scale %# on a %# view, which has no present upsample - the view is not drawn (use 1)", camera.getComponentID(), scale, viewKind );
                return false;
            }

            /**
             * @brief 보임 오브젝트(모니터)가 화면들 중 가장 크게 보이는 크기(경계 구 지름 / 화면 높이, 애니메이션 LOD 와 같은 식)입니다. 어느 화면에도 없으면 0 입니다.
             * @details 경계는 오브젝트의 렌더 경계(`ScenePrimitive::computeObjectRenderBounds` — 카메라 질의와 같은 구)다. 그릴 것이 없으면 오브젝트 위치(반지름 1)로 봅니다.
             *          오브젝트가 없으면(파괴됐다) 볼 것이 없으니 0 입니다.
             *          @p pMainOcclusion 이 있고 첫 화면(주 시점)에서 경계 구가 가림 타일 뒤에 가려졌으면 그 화면은 세지 않는다.
             */
            static float32 computeVisibleScreenSize( const GameObjectManager& manager, const GameObjectHandle& handle, const LODView* pView, uint32 viewCount,
                                                     const OcclusionTileSnapshot* pMainOcclusion )
            {
                const GameObject* pObject = manager.resolveGameObject( handle );
                if ( pObject == nullptr )
                    return 0.0f;
                float3  center{};
                float32 radius = 0.0f;
                if ( ScenePrimitive::computeObjectRenderBounds( *pObject, center, radius ) == false )
                {
                    const SceneComponent* pScene = pObject->getPrimarySceneComponent();
                    if ( pScene == nullptr )
                        return 0.0f;
                    center = pScene->getWorldPosition();
                    radius = 1.0f;
                }
                float32 screenSize = 0.0f;
                for ( uint32 viewIndex = 0; viewIndex < viewCount; ++viewIndex )
                {
                    // 첫 화면이 주 시점이다. 가림 타일은 주 시점의 깊이에서 지었으므로 다른 화면에는 쓰지 않는다.
                    const bool bOccludedInMain = viewIndex == 0 && pMainOcclusion != nullptr && OcclusionTileSnapshot::isSphereOccluded( *pMainOcclusion, center, radius );
                    if ( bOccludedInMain )
                        continue;
                    screenSize = MathUtil::max( screenSize, LODView::computeScreenSize( pView[viewIndex], center, radius ) );
                }
                return screenSize;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RenderViewSettings RenderViewCollector::makeMainSettings( CameraComponent* pMainCamera, float32 autoScale )
    {
        if ( pMainCamera == nullptr )
            return RenderViewSettings{};
        RenderViewSettings settings = RenderViewCollectorInternal::makeSettings( *pMainCamera );
        settings._resolutionScale   = RenderViewCollectorInternal::applyScreenRenderScale( pMainCamera->getRenderOutput()._resolutionScale );
        settings._rectScale         = autoScale;
        return settings;
    }

    float32 RenderViewCollector::computeAspect( const RenderViewSettings& settings, uint32 outputWidth, uint32 outputHeight )
    {
        float32 width  = static_cast<float32>( outputWidth ) * settings._screenRect._z;
        float32 height = static_cast<float32>( outputHeight ) * settings._screenRect._w;
        // 정수 업스케일이면 실제로 그리는 크기(사각형 / 배율의 내림)의 비율이다. 키운 뒤에도 픽셀이 정사각이다.
        const bool bIntegerUpscale = settings._upscaleZoom > 0 && width > 0.0f && height > 0.0f;
        if ( bIntegerUpscale )
        {
            width  = static_cast<float32>( FrameRendererUtil::computeViewPoolExtent( width, 1.0f, settings._upscaleZoom ) );
            height = static_cast<float32>( FrameRendererUtil::computeViewPoolExtent( height, 1.0f, settings._upscaleZoom ) );
        }
        return ( width > 0.0f && height > 0.0f ) ? width / height : ( 16.0f / 9.0f );
    }

    uint32 RenderViewCollector::getMaxExtraViewCount()
    {
        const uint32 count = engine::getSubsystem<EngineConfig>()._render._maxExtraViewCount;
        if ( isValidMaxExtraViewCount( count ) == false )
            SW_FATAL( "EngineConfig _render._maxExtraViewCount %# is missing or out of range (1..%#)", count, kExtraRenderViewCapacity );
        return count;
    }

    RenderViewBudget RenderViewCollector::getDefaultBudget()
    {
        RenderViewBudget budget;
        budget._maxViewCount  = static_cast<uint32>( MathUtil::max( 0, static_cast<int32>( gv_renderViewBudget ) ) );
        budget._maxPixelCount = static_cast<uint32>( static_cast<float64>( MathUtil::max( 0.0f, static_cast<float32>( gv_renderViewMegapixelBudget ) ) ) *
                                                     RenderViewCollectorInternal::kPixelsPerMegapixel );
        return budget;
    }

    RenderViewCollectResult RenderViewCollector::collectExtraViews( const GameObjectManager& manager, const CameraComponent* pMainCamera, const float4x4& mainViewProj,
                                                                    const float3& mainPosition, uint32 outputWidth, uint32 outputHeight, float32 autoScale, float64 now, uint32 maxExtraViewCount, const RenderViewBudget& budget,
                                                                    RenderViewScheduler& scheduler, const OcclusionTileSnapshot* pMainOcclusion, vector<RenderViewRequest>& outListView )
    {
        outListView.clear();
        // 후보는 자리 수(@p maxExtraViewCount)와 무관하게 모두 모은다 — 자리는 스케줄러가 보이는 것 · 늦은 것 먼저 준다(등록 순서로 자르지 않는다).
        vector<RenderViewScheduler::Candidate> listCandidate;
        vector<const CameraRenderOutput*>      listCameraOutput;
        for ( CameraComponent* pCamera : manager.getCameraRegistry().getAll() )
        {
            const bool bNotExtraViewCamera = pCamera == pMainCamera || CameraRegistry::isUsableCamera( pCamera ) == false || pCamera->getRole() == CameraRole::Viewport;
            if ( bNotExtraViewCamera )
                continue;
            const CameraRenderOutput& output = pCamera->getRenderOutput();
            if ( output._target == CameraOutputTarget::MainView )
                continue;
            RenderViewRequest request;
            request._settings = RenderViewCollectorInternal::makeSettings( *pCamera );
            request._viewID   = pCamera->getComponentID();
            if ( output._target == CameraOutputTarget::RenderTexture )
            {
                if ( output._renderTexture.empty() )
                    continue;
                if ( RenderViewCollectorInternal::hasUnitResolutionScale( *pCamera, "render texture" ) == false )
                    continue;
                request._outputKind            = RenderViewOutputKind::RenderTexture;
                request._renderTexture         = output._renderTexture;
                request._outputWidth           = MathUtil::max( 1u, output._renderTextureWidth );
                request._outputHeight          = MathUtil::max( 1u, output._renderTextureHeight );
                request._settings._screenRect  = float4{ 0.0f, 0.0f, 1.0f, 1.0f };
                request._settings._upscaleZoom = 0; // 렌더 텍스처는 자기 크기에 그린다
            }
            else
            {
                if ( outputWidth == 0 || outputHeight == 0 )
                    continue;
                request._outputKind                = RenderViewOutputKind::ScreenRect;
                request._settings._resolutionScale = RenderViewCollectorInternal::applyScreenRenderScale( output._resolutionScale );
                request._settings._rectScale       = autoScale;
                request._outputWidth               = MathUtil::max( 1u, static_cast<uint32>( static_cast<float32>( outputWidth ) * output._screenRect._z + 0.5f ) );
                request._outputHeight              = MathUtil::max( 1u, static_cast<uint32>( static_cast<float32>( outputHeight ) * output._screenRect._w + 0.5f ) );
            }
            // 정수 업스케일이면 실제로 그리는 크기(사각형 / 배율의 내림)의 비율이다(주 시점의 computeAspect 와 같은 규칙).
            const uint32  zoom        = request._settings._upscaleZoom;
            const float32 drawnWidth  = static_cast<float32>( FrameRendererUtil::computeViewPoolExtent( static_cast<float32>( request._outputWidth ), 1.0f, zoom ) );
            const float32 drawnHeight = static_cast<float32>( FrameRendererUtil::computeViewPoolExtent( static_cast<float32>( request._outputHeight ), 1.0f, zoom ) );
            const float32 aspect      = drawnWidth / drawnHeight;
            request._viewProj         = pCamera->getViewProjectionMatrix( aspect );
            pCamera->setDrawnAspect( aspect ); // 게임 쪽이 같은 비로 투영하게 적는다(대기 뷰 LOD)
            request._position            = pCamera->getCameraPosition();
            request._transparentSortAxis = engine::getSubsystem<Render2DSettings>().computeTransparentSortAxis( pCamera->isOrthographic(), pCamera->getCameraForward() );

            RenderViewScheduler::Candidate& candidate = listCandidate.emplace_back();
            candidate._viewID                         = request._viewID;
            candidate._updateRate                     = MathUtil::max( 0.0f, output._updateRate );
            candidate._pixelCount                     = FrameRendererUtil::computeViewPoolExtent( static_cast<float32>( request._outputWidth ), request._settings._resolutionScale, zoom ) *
                                    FrameRendererUtil::computeViewPoolExtent( static_cast<float32>( request._outputHeight ), request._settings._resolutionScale, zoom );
            candidate._bBudgeted = request._outputKind == RenderViewOutputKind::RenderTexture ? SW_TRUE : SW_FALSE;
            listCameraOutput.push_back( &output );
            outListView.push_back( request );
        }
        const uint32 candidateCount = static_cast<uint32>( listCandidate.size() );

        // 보임은 사람이 보는 화면 전부에서 측정한다 — 주 시점과 화면 사각형 뷰 모두(분할 화면의 다른 칸에 모니터가 보일 수 있다). 자리 수로 자르지 않는다 —
        // 자리는 스케줄러가 이 보임을 보고 나눠 주므로, 자르면 넘친 칸에만 보이는 모니터가 자리를 못 얻는다(언리얼 뷰 패밀리도 모든 뷰를 본다).
        // 렌더 텍스처 뷰 너머로 보이는 모니터는 세지 않는다.
        vector<LODView> listVisibilityView;
        listVisibilityView.reserve( outListView.size() + 1 );
        listVisibilityView.push_back( LODView::make( mainViewProj, mainPosition ) );
        for ( const RenderViewRequest& request : outListView )
        {
            if ( request._outputKind == RenderViewOutputKind::ScreenRect )
                listVisibilityView.push_back( LODView::make( request._viewProj, request._position ) );
        }
        const uint32 visibilityViewCount = static_cast<uint32>( listVisibilityView.size() );
        uint32       hiddenViewCount     = 0;
        for ( uint32 index = 0; index < candidateCount; ++index )
        {
            const CameraRenderOutput& output = *listCameraOutput[index];
            if ( output._visibilityObject.isValid() == false )
                continue;
            const float32 screenSize       = RenderViewCollectorInternal::computeVisibleScreenSize( manager, output._visibilityObject, listVisibilityView.data(), visibilityViewCount, pMainOcclusion );
            const bool    bVisible         = screenSize > 0.0f && screenSize >= output._minScreenSize;
            listCandidate[index]._bVisible = bVisible ? SW_TRUE : SW_FALSE;
            hiddenViewCount += bVisible ? 0u : 1u;
        }

        vector<uint8> listListed( candidateCount, SW_FALSE );
        vector<uint8> listRender( candidateCount, SW_FALSE );
        (void)scheduler.schedule( now, listCandidate.data(), candidateCount, maxExtraViewCount, budget, listListed.data(), listRender.data() );
        // 자리를 얻지 못한 뷰는 목록에서 뺀다(렌더러는 자리 수만큼만 뷰 자원을 둔다). 남은 순서는 등록 순서 그대로다.
        RenderViewCollectResult result;
        result._hiddenCount = hiddenViewCount;
        uint32 keptCount    = 0;
        for ( uint32 index = 0; index < candidateCount; ++index )
        {
            if ( listListed[index] == SW_FALSE )
            {
                ++result._overflowCount;
                continue;
            }
            if ( keptCount != index )
                outListView[keptCount] = std::move( outListView[index] );
            outListView[keptCount]._bRender = listRender[index];
            ++keptCount;
        }
        outListView.resize( keptCount );
        return result;
    }

    void RenderViewCollector::appendViewportView( CameraComponent& camera, const ViewportTarget& target, RenderViewMode viewMode, uint32 maxExtraViewCount, vector<RenderViewRequest>& inoutListView )
    {
        if ( target.isValid() == false )
            return;
        if ( RenderViewCollectorInternal::hasUnitResolutionScale( camera, "host target" ) == false )
            return;
        RenderViewRequest request;
        request._settings             = RenderViewCollectorInternal::makeSettings( camera );
        request._settings._screenRect = float4{ 0.0f, 0.0f, 1.0f, 1.0f };
        request._settings._viewMode   = viewMode;
        request._viewID               = camera.getComponentID();
        request._outputKind           = RenderViewOutputKind::ViewportTarget;
        request._viewportTarget       = target._renderTarget;
        request._outputWidth          = target._width;
        request._outputHeight         = target._height;
        request._viewProj             = camera.getViewProjectionMatrix( static_cast<float32>( target._width ) / static_cast<float32>( target._height ) );
        request._position             = camera.getCameraPosition();
        request._transparentSortAxis  = engine::getSubsystem<Render2DSettings>().computeTransparentSortAxis( camera.isOrthographic(), camera.getCameraForward() );
        request._bRender              = SW_TRUE;
        if ( inoutListView.size() >= maxExtraViewCount )
            inoutListView.back() = request;
        else
            inoutListView.push_back( request );
    }

    void RenderViewCollector::removeScreenRectViews( vector<RenderViewRequest>& inoutListView )
    {
        VectorUtil::removeIf( inoutListView, []( const RenderViewRequest& request )
        { return request._outputKind == RenderViewOutputKind::ScreenRect; } );
    }
} // namespace sw
