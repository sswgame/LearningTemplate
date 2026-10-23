#include "pch.h"

#include "GameFramework/Base/World/Spline/SplineRiverPathComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/World/Spline/SplineComponent.h"

namespace sw
{
    SW_LOG_CALLER( "SplineRiverPathComponent" );

    SplineRiverPathComponent::SplineRiverPathComponent()
        : _sampleSpacing{ 2.0f }
        , _lastRevision{ 0 }
        , _lastWaterPosition{}
        , _bHasPushed{ false }
        , _bReportedMissing{ false }
    {
        setCanEverTick( true );
    }

    void SplineRiverPathComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        (void)refreshRiverPath( true ); // 실패는 refreshRiverPath 가 오류로 남겼다 — 틱이 다시 시도한다
    }

    void SplineRiverPathComponent::onPostLoad()
    {
        Component::onPostLoad();
        (void)refreshRiverPath( true ); // 실패는 refreshRiverPath 가 오류로 남겼다 — 틱이 다시 시도한다
    }

    void SplineRiverPathComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        (void)refreshRiverPath( true ); // 실패는 refreshRiverPath 가 오류로 남겼다 — 틱이 다시 시도한다
    }

    void SplineRiverPathComponent::onUnregister( GameObjectManager& manager )
    {
        const GameObject*   pOwner = getOwner();
        WaterBodyComponent* pWater = pOwner != nullptr ? pOwner->getComponent<WaterBodyComponent>() : nullptr;
        if ( pWater != nullptr )
            pWater->clearRiverPathSource();
        Component::onUnregister( manager );
    }

    void SplineRiverPathComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        (void)refreshRiverPath( false ); // 실패는 refreshRiverPath 가 오류로 남겼다
    }

    WaterBodyComponent* SplineRiverPathComponent::findWater() const
    {
        const GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getComponent<WaterBodyComponent>() : nullptr;
    }

    SplineComponent* SplineRiverPathComponent::findSpline() const
    {
        const GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getComponent<SplineComponent>() : nullptr;
    }

    bool SplineRiverPathComponent::refreshRiverPath( bool bForce )
    {
        WaterBodyComponent* pWater     = findWater();
        SplineComponent*    pSpline    = findSpline();
        const bool          bHasSource = pWater != nullptr && pSpline != nullptr;
        if ( bHasSource == false )
        {
            if ( _bReportedMissing == false )
            {
                _bReportedMissing = true;
                SW_LOG_ERROR( "A WaterBodyComponent and a SplineComponent are both required on the same object - the river path is not driven" );
            }
            return false;
        }
        const float3 waterPosition = pWater->getWorldPosition();
        const bool   bUnchanged    = _bHasPushed && _lastRevision == pSpline->getPathRevision() && _lastWaterPosition == waterPosition;
        if ( bForce == false && bUnchanged )
            return false;

        vector<float3> listPoint;
        sampleRiverPoints( pSpline->getWorldPath(), waterPosition, _sampleSpacing, listPoint );
        if ( listPoint.size() < 2 )
        {
            if ( _bReportedMissing == false )
            {
                _bReportedMissing = true;
                SW_LOG_ERROR( "The spline has no usable curve (needs two control points) or the sample spacing is not above 0 - the river path is not driven" );
            }
            return false;
        }
        _bReportedMissing  = false;
        _lastRevision      = pSpline->getPathRevision();
        _lastWaterPosition = waterPosition;
        _bHasPushed        = true;
        pWater->setRiverPathFromSource( listPoint );
        return true;
    }

    void SplineRiverPathComponent::sampleRiverPoints( const SplinePath& worldPath, const float3& waterWorldPosition, float32 spacing, vector<float3>& outListPoint )
    {
        outListPoint.clear();
        if ( worldPath.isValid() == false )
            return;
        vector<SplineSample> listSample;
        worldPath.sampleUniform( spacing, listSample );
        outListPoint.reserve( listSample.size() );
        for ( const SplineSample& sample : listSample )
        {
            outListPoint.push_back( sample._position - waterWorldPosition );
        }
    }
} // namespace sw
