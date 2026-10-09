#include "pch.h"

#include "Engine/Graphics/Renderer/Frame/RenderViewCollector.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/Frustum.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Graphics/Renderer/Frame/RenderViewScheduler.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/CameraRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    /**
     * @brief `-gv_renderViewBudget=<n>` — 한 프레임에 그리는 추가 뷰(CCTV · 백미러 · PiP)의 최대 수입니다(0 = 제한 없음).
     * @details 넘치면 가장 오래 기다린 뷰부터 그리고 나머지는 다음 프레임으로 미룬다(굶지 않는다 — `RenderViewScheduler`).
     */
    SW_GLOBAL_VARIABLE( int32, gv_renderViewBudget, 4, "한 프레임에 그리는 추가 뷰(CCTV · 백미러 · PiP)의 최대 수 (0 = 제한 없음)" );

    namespace
    {
        struct RenderViewCollectorInternal
        {
            static RenderViewSettings makeSettings( const CameraRenderOutput& output )
            {
                RenderViewSettings settings;
                settings._screenRect      = output._screenRect;
                settings._resolutionScale = MathUtil::clamp( output._resolutionScale, 0.1f, 2.0f );
                settings._bShadows        = output._bShadows ? SW_TRUE : SW_FALSE;
                settings._bPostProcess    = output._bPostProcess ? SW_TRUE : SW_FALSE;
                return settings;
            }

            /** @brief 오브젝트의 메시 경계 구가 절두체와 겹치는지입니다. 메시가 없으면 오브젝트 자리(반지름 1)로 봅니다. 오브젝트가 없으면 보인다고 칩니다. */
            static bool isObjectVisible( const GameObjectManager& manager, const GameObjectHandle& handle, const Frustum& frustum )
            {
                const GameObject* pObject = manager.resolveGameObject( handle );
                if ( pObject == nullptr )
                    return true;
                const MeshComponent*  pMesh  = pObject->getComponent<MeshComponent>();
                const SceneComponent* pScene = pMesh != nullptr ? static_cast<const SceneComponent*>( pMesh ) : pObject->getComponent<SceneComponent>();
                if ( pScene == nullptr )
                    return true;
                const float4x4 world  = pScene->getWorldMatrix();
                const float32  scaleX = float3{ world._11, world._12, world._13 }.getLength();
                const float32  scaleY = float3{ world._21, world._22, world._23 }.getLength();
                const float32  scaleZ = float3{ world._31, world._32, world._33 }.getLength();
                const float32  radius = ( pMesh != nullptr ? pMesh->getBoundsRadius() : 1.0f ) * MathUtil::max( scaleX, MathUtil::max( scaleY, scaleZ ) );
                return frustum.overlapsSphere( world.getTranslation(), radius );
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
        RenderViewSettings settings = RenderViewCollectorInternal::makeSettings( pMainCamera->getRenderOutput() );
        settings._bCut              = pMainCamera->consumeCut() ? SW_TRUE : SW_FALSE;
        return settings;
    }

    float32 RenderViewCollector::computeAspect( const RenderViewSettings& settings, uint32 outputWidth, uint32 outputHeight )
    {
        const float32 width  = static_cast<float32>( outputWidth ) * settings._screenRect._z;
        const float32 height = static_cast<float32>( outputHeight ) * settings._screenRect._w;
        return ( width > 0.0f && height > 0.0f ) ? width / height : ( 16.0f / 9.0f );
    }

    uint32 RenderViewCollector::getDefaultBudget()
    {
        return static_cast<uint32>( MathUtil::max( 0, static_cast<int32>( gv_renderViewBudget ) ) );
    }

    void RenderViewCollector::collectExtraViews( const GameObjectManager& manager, const CameraComponent* pMainCamera, const float4x4& mainViewProj,
                                                 uint32 outputWidth, uint32 outputHeight, float64 now, uint32 budget, RenderViewScheduler& scheduler,
                                                 vector<RenderViewRequest>& outListView )
    {
        outListView.clear();
        const Frustum                  mainFrustum = Frustum::fromViewProjection( mainViewProj );
        RenderViewScheduler::Candidate arrCandidate[kMaxExtraRenderView]{};
        uint32                         candidateCount = 0;
        for ( CameraComponent* pCamera : manager.getCameraRegistry().getAll() )
        {
            if ( candidateCount >= kMaxExtraRenderView )
                break;
            if ( pCamera == pMainCamera || CameraRegistry::isUsableCamera( pCamera ) == false || pCamera->getRole() == CameraRole::Editor )
                continue;
            const CameraRenderOutput& output = pCamera->getRenderOutput();
            if ( output._target == CameraOutputTarget::MainView )
                continue;
            RenderViewRequest request;
            request._settings       = RenderViewCollectorInternal::makeSettings( output );
            request._settings._bCut = pCamera->consumeCut() ? SW_TRUE : SW_FALSE;
            request._viewId         = pCamera->getComponentId();
            if ( output._target == CameraOutputTarget::RenderTexture )
            {
                if ( output._renderTexture.empty() )
                    continue;
                request._outputKind           = RenderViewOutputKind::RenderTexture;
                request._renderTexture        = hashed_string( string_view{ output._renderTexture } );
                request._outputWidth          = MathUtil::max( 1u, output._renderTextureWidth );
                request._outputHeight         = MathUtil::max( 1u, output._renderTextureHeight );
                request._settings._screenRect = float4{ 0.0f, 0.0f, 1.0f, 1.0f };
            }
            else
            {
                if ( outputWidth == 0 || outputHeight == 0 )
                    continue;
                request._outputKind   = RenderViewOutputKind::ScreenRect;
                request._outputWidth  = MathUtil::max( 1u, static_cast<uint32>( static_cast<float32>( outputWidth ) * output._screenRect._z + 0.5f ) );
                request._outputHeight = MathUtil::max( 1u, static_cast<uint32>( static_cast<float32>( outputHeight ) * output._screenRect._w + 0.5f ) );
            }
            const float32 aspect         = static_cast<float32>( request._outputWidth ) / static_cast<float32>( request._outputHeight );
            request._viewProj            = pCamera->getViewProjectionMatrix( aspect );
            request._position            = pCamera->getCameraPosition();
            request._transparentSortAxis = Render2DSettings::getActive().computeTransparentSortAxis( pCamera->isOrthographic(), pCamera->getCameraForward() );

            RenderViewScheduler::Candidate& candidate = arrCandidate[candidateCount];
            candidate._viewId                         = request._viewId;
            candidate._updateRate                     = MathUtil::max( 0.0f, output._updateRate );
            candidate._bVisible                       = output._visibilityObject.isValid() == false || RenderViewCollectorInternal::isObjectVisible( manager, output._visibilityObject, mainFrustum )
                                                          ? SW_TRUE
                                                          : SW_FALSE;
            outListView.push_back( request );
            ++candidateCount;
        }

        uint8 arrRender[kMaxExtraRenderView]{};
        (void)scheduler.schedule( now, arrCandidate, candidateCount, budget, arrRender );
        for ( uint32 index = 0; index < candidateCount; ++index )
        {
            outListView[index]._bRender = arrRender[index];
        }
    }

    void RenderViewCollector::appendHostView( CameraComponent& camera, const HostViewTarget& target, vector<RenderViewRequest>& inoutListView )
    {
        if ( target.isValid() == false )
            return;
        RenderViewRequest request;
        request._settings             = RenderViewCollectorInternal::makeSettings( camera.getRenderOutput() );
        request._settings._screenRect = float4{ 0.0f, 0.0f, 1.0f, 1.0f };
        request._settings._bCut       = camera.consumeCut() ? SW_TRUE : SW_FALSE;
        request._viewId               = camera.getComponentId();
        request._outputKind           = RenderViewOutputKind::HostTarget;
        request._hostTarget           = target._renderTarget;
        request._outputWidth          = target._width;
        request._outputHeight         = target._height;
        request._viewProj             = camera.getViewProjectionMatrix( static_cast<float32>( target._width ) / static_cast<float32>( target._height ) );
        request._position             = camera.getCameraPosition();
        request._transparentSortAxis  = Render2DSettings::getActive().computeTransparentSortAxis( camera.isOrthographic(), camera.getCameraForward() );
        request._bRender              = SW_TRUE;
        if ( inoutListView.size() >= kMaxExtraRenderView )
            inoutListView.back() = request;
        else
            inoutListView.push_back( request );
    }

    void RenderViewCollector::removeScreenRectViews( vector<RenderViewRequest>& inoutListView )
    {
        inoutListView.erase( std::remove_if( inoutListView.begin(), inoutListView.end(),
                                             []( const RenderViewRequest& request )
        { return request._outputKind == RenderViewOutputKind::ScreenRect; } ),
                             inoutListView.end() );
    }
} // namespace sw
