#include "pch.h"

#include "Engine/Renderer/Frame/View/RenderViewCollector.h"

#include "Core/Container/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/SceneAnimation/AnimationLOD.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/View/OcclusionTileSnapshot.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

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
            /// 백만 픽셀 단위 예산을 픽셀 수로 바꾸는 비율입니다.
            static constexpr float64 kPixelsPerMegapixel = 1.0e6;
            /// 보임을 측정하는 화면의 최대 수입니다(주 시점 + 화면 사각형 뷰).
            static constexpr uint32 kMaxVisibilityView = kMaxExtraRenderView + 1;

            /**
             * @brief 카메라 하나의 뷰 설정입니다 — 출력 설정, 후처리 값, 피사계 심도 렌즈 값, 컷 표시(읽고 지운다).
             * @details 세 뷰(주 시점 · 추가 뷰 · 호스트 뷰)가 모두 이 함수로 만든다. 카메라에서 뷰로 가는 값이 늘면 여기 한 곳에만 더한다.
             */
            static RenderViewSettings makeSettings( CameraComponent& camera )
            {
                const CameraRenderOutput& output = camera.getRenderOutput();
                RenderViewSettings        settings;
                settings._screenRect       = output._screenRect;
                settings._resolutionScale  = MathUtil::clamp( output._resolutionScale, 0.1f, 2.0f );
                settings._postProcess      = camera.getPostProcess();
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
             * @brief 화면에 나가는 뷰(주 시점 · 화면 사각형)의 내부 해상도에 사용자 렌더 스케일(`gv_renderScale`, 옵션 메뉴 graphics.renderScale)을 곱합니다.
             * @details Present 가 출력 크기로 선형 업샘플한다. 렌더 텍스처 뷰는 자기 크기가 데이터라 곱하지 않고, 정수 업스케일 뷰는 배율을 보지 않는다.
             */
            static float32 applyUserRenderScale( float32 cameraScale )
            {
                return MathUtil::clamp( cameraScale * static_cast<float32>( gv_renderScale ), 0.1f, 2.0f );
            }

            /**
             * @brief 보임 오브젝트(모니터)가 화면들 중 가장 크게 보이는 크기(경계 구 지름 / 화면 높이, 애니메이션 LOD 와 같은 식)입니다. 어느 화면에도 없으면 0 입니다.
             * @details 메시가 없으면 오브젝트 위치(반지름 1)로 봅니다. 오브젝트가 없으면(파괴됐다) 볼 것이 없으니 0 입니다.
             *          @p pMainOcclusion 이 있고 첫 화면(주 시점)에서 경계 구가 가림 타일 뒤에 가려졌으면 그 화면은 세지 않는다.
             */
            static float32 computeVisibleScreenSize( const GameObjectManager& manager, const GameObjectHandle& handle, const AnimationLODView* pView, uint32 viewCount,
                                                     const OcclusionTileSnapshot* pMainOcclusion )
            {
                const GameObject* pObject = manager.resolveGameObject( handle );
                if ( pObject == nullptr )
                    return 0.0f;
                const MeshComponent*  pMesh  = pObject->getComponent<MeshComponent>();
                const SceneComponent* pScene = pMesh != nullptr ? static_cast<const SceneComponent*>( pMesh ) : pObject->getComponent<SceneComponent>();
                if ( pScene == nullptr )
                    return 0.0f;
                const float4x4 world      = pScene->getWorldMatrix();
                const float32  scaleX     = float3{ world._11, world._12, world._13 }.getLength();
                const float32  scaleY     = float3{ world._21, world._22, world._23 }.getLength();
                const float32  scaleZ     = float3{ world._31, world._32, world._33 }.getLength();
                const float32  radius     = ( pMesh != nullptr ? pMesh->getBoundsRadius() : 1.0f ) * MathUtil::max( scaleX, MathUtil::max( scaleY, scaleZ ) );
                float32        screenSize = 0.0f;
                for ( uint32 viewIndex = 0; viewIndex < viewCount; ++viewIndex )
                {
                    // 첫 화면이 주 시점이다. 가림 타일은 주 시점의 깊이에서 지었으므로 다른 화면에는 쓰지 않는다.
                    const bool bOccludedInMain = viewIndex == 0 && pMainOcclusion != nullptr && OcclusionTileQuery::isSphereOccluded( *pMainOcclusion, world.getTranslation(), radius );
                    if ( bOccludedInMain )
                        continue;
                    screenSize = MathUtil::max( screenSize, AnimationLODUtil::computeScreenSize( pView[viewIndex], world.getTranslation(), radius ) );
                }
                return screenSize;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RenderViewSettings RenderViewCollector::makeMainSettings( CameraComponent* pMainCamera )
    {
        if ( pMainCamera == nullptr )
            return RenderViewSettings{};
        RenderViewSettings settings = RenderViewCollectorInternal::makeSettings( *pMainCamera );
        settings._resolutionScale   = RenderViewCollectorInternal::applyUserRenderScale( settings._resolutionScale );
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

    RenderViewBudget RenderViewCollector::getDefaultBudget()
    {
        RenderViewBudget budget;
        budget._maxViewCount  = static_cast<uint32>( MathUtil::max( 0, static_cast<int32>( gv_renderViewBudget ) ) );
        budget._maxPixelCount = static_cast<uint32>( static_cast<float64>( MathUtil::max( 0.0f, static_cast<float32>( gv_renderViewMegapixelBudget ) ) ) *
                                                     RenderViewCollectorInternal::kPixelsPerMegapixel );
        return budget;
    }

    void RenderViewCollector::collectExtraViews( const GameObjectManager& manager, const CameraComponent* pMainCamera, const float4x4& mainViewProj,
                                                 const float3& mainPosition, uint32 outputWidth, uint32 outputHeight, float64 now, const RenderViewBudget& budget,
                                                 RenderViewScheduler& scheduler, const OcclusionTileSnapshot* pMainOcclusion, vector<RenderViewRequest>& outListView )
    {
        outListView.clear();
        RenderViewScheduler::Candidate arrCandidate[kMaxExtraRenderView]{};
        const CameraRenderOutput*      arrCameraOutput[kMaxExtraRenderView]{};
        uint32                         candidateCount = 0;
        for ( CameraComponent* pCamera : manager.getCameraRegistry().getAll() )
        {
            if ( candidateCount >= kMaxExtraRenderView )
                break;
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
                request._outputKind            = RenderViewOutputKind::RenderTexture;
                request._renderTexture         = hashed_string( string_view{ output._renderTexture } );
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
                request._settings._resolutionScale = RenderViewCollectorInternal::applyUserRenderScale( request._settings._resolutionScale );
                request._outputWidth               = MathUtil::max( 1u, static_cast<uint32>( static_cast<float32>( outputWidth ) * output._screenRect._z + 0.5f ) );
                request._outputHeight              = MathUtil::max( 1u, static_cast<uint32>( static_cast<float32>( outputHeight ) * output._screenRect._w + 0.5f ) );
            }
            // 정수 업스케일이면 실제로 그리는 크기(사각형 / 배율의 내림)의 비율이다(주 시점의 computeAspect 와 같은 규칙).
            const uint32  zoom           = request._settings._upscaleZoom;
            const float32 drawnWidth     = static_cast<float32>( FrameRendererUtil::computeViewPoolExtent( static_cast<float32>( request._outputWidth ), 1.0f, zoom ) );
            const float32 drawnHeight    = static_cast<float32>( FrameRendererUtil::computeViewPoolExtent( static_cast<float32>( request._outputHeight ), 1.0f, zoom ) );
            const float32 aspect         = drawnWidth / drawnHeight;
            request._viewProj            = pCamera->getViewProjectionMatrix( aspect );
            request._position            = pCamera->getCameraPosition();
            request._transparentSortAxis = engine::getSubsystem<Render2DSettings>().computeTransparentSortAxis( pCamera->isOrthographic(), pCamera->getCameraForward() );

            RenderViewScheduler::Candidate& candidate = arrCandidate[candidateCount];
            candidate._viewID                         = request._viewID;
            candidate._updateRate                     = MathUtil::max( 0.0f, output._updateRate );
            candidate._pixelCount                     = FrameRendererUtil::computeViewPoolExtent( static_cast<float32>( request._outputWidth ), request._settings._resolutionScale, zoom ) *
                                    FrameRendererUtil::computeViewPoolExtent( static_cast<float32>( request._outputHeight ), request._settings._resolutionScale, zoom );
            candidate._bBudgeted            = request._outputKind == RenderViewOutputKind::RenderTexture ? SW_TRUE : SW_FALSE;
            arrCameraOutput[candidateCount] = &output;
            outListView.push_back( request );
            ++candidateCount;
        }

        // 보임은 사람이 보는 화면 전부에서 측정한다 — 주 시점과 화면 사각형 뷰(분할 화면의 다른 칸에 모니터가 보일 수 있다). 렌더 텍스처 뷰 너머로 보이는 모니터는 세지 않는다.
        AnimationLODView arrVisibilityView[RenderViewCollectorInternal::kMaxVisibilityView]{};
        uint32           visibilityViewCount     = 0;
        arrVisibilityView[visibilityViewCount++] = AnimationLODView::make( mainViewProj, mainPosition );
        for ( const RenderViewRequest& request : outListView )
        {
            if ( request._outputKind == RenderViewOutputKind::ScreenRect )
                arrVisibilityView[visibilityViewCount++] = AnimationLODView::make( request._viewProj, request._position );
        }
        for ( uint32 index = 0; index < candidateCount; ++index )
        {
            const CameraRenderOutput& output = *arrCameraOutput[index];
            if ( output._visibilityObject.isValid() == false )
                continue;
            const float32 screenSize      = RenderViewCollectorInternal::computeVisibleScreenSize( manager, output._visibilityObject, arrVisibilityView, visibilityViewCount, pMainOcclusion );
            const bool    bVisible        = screenSize > 0.0f && screenSize >= output._minScreenSize;
            arrCandidate[index]._bVisible = bVisible ? SW_TRUE : SW_FALSE;
        }

        uint8 arrRender[kMaxExtraRenderView]{};
        (void)scheduler.schedule( now, arrCandidate, candidateCount, budget, arrRender );
        for ( uint32 index = 0; index < candidateCount; ++index )
        {
            outListView[index]._bRender = arrRender[index];
        }
    }

    void RenderViewCollector::appendHostView( CameraComponent& camera, const HostViewTarget& target, RenderViewMode viewMode, vector<RenderViewRequest>& inoutListView )
    {
        if ( target.isValid() == false )
            return;
        RenderViewRequest request;
        request._settings             = RenderViewCollectorInternal::makeSettings( camera );
        request._settings._screenRect = float4{ 0.0f, 0.0f, 1.0f, 1.0f };
        request._settings._viewMode   = viewMode;
        request._viewID               = camera.getComponentID();
        request._outputKind           = RenderViewOutputKind::HostTarget;
        request._hostTarget           = target._renderTarget;
        request._outputWidth          = target._width;
        request._outputHeight         = target._height;
        request._viewProj             = camera.getViewProjectionMatrix( static_cast<float32>( target._width ) / static_cast<float32>( target._height ) );
        request._position             = camera.getCameraPosition();
        request._transparentSortAxis  = engine::getSubsystem<Render2DSettings>().computeTransparentSortAxis( camera.isOrthographic(), camera.getCameraForward() );
        request._bRender              = SW_TRUE;
        if ( inoutListView.size() >= kMaxExtraRenderView )
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
