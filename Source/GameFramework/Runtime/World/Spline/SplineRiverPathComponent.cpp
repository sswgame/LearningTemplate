#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Math/Math.h"

#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/World/Spline/SplineComponent.h"

namespace sw
{
    class GameObjectManager;
    class SplineComponent;
    class SplinePath;
    class WaterBodyComponent;

    /**
     * @class SplineRiverPathComponent
     * @brief 스플라인 곡선을 강 경로로 넘기는 어댑터. 물(`WaterBodyComponent`)과 스플라인(`SplineComponent`)이 같은 오브젝트에 있어야 한다.
     */
    REFLECT( Category = "Environment", DisplayName = "Spline River Path", Tooltip = "Drives the river path of the water body on this object from the spline on this object" )
    class SplineRiverPathComponent : public Component
    {
    public:
        REFLECT_BODY();

        SplineRiverPathComponent();
        virtual ~SplineRiverPathComponent() override = default;

        void onBeginPlay() override;
        void onPostLoad() override;
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 이 컴포넌트가 사라지면 물의 경로를 다시 손으로 고칠 수 있게 합니다(`WaterBodyComponent::clearRiverPathSource`). */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 스플라인 곡선 번호나 물 위치가 바뀌었으면 다시 샘플해 넘깁니다. */
        void onTick( TickContext& context ) override;

        /**
         * @brief 스플라인에서 점을 뽑아 물에 넘깁니다.
         * @param bForce 거짓이면 곡선 번호와 물 위치가 지난번과 같을 때 아무것도 하지 않는다.
         * @return 물에 넘겼으면 true. 물이나 스플라인이 없거나 곡선이 비었으면 false(오류를 한 번 남긴다).
         */
        [[nodiscard]] bool refreshRiverPath( bool bForce );

        /**
         * @brief 월드 곡선을 @p spacing 간격으로 샘플해 @p waterWorldPosition 기준 점(월드 − 물 위치, 회전 없음)으로 바꿉니다. 곡선이 비었거나 간격이 0 이하면 비웁니다.
         * @details `WaterBodyComponent::_listRiverPoint` 와 같은 좌표다(수면은 오너 월드 위치만 원점으로 쓴다).
         */
        static void sampleRiverPoints( const SplinePath& worldPath, const float3& waterWorldPosition, float32 spacing, vector<float3>& outListPoint );

        float32 getSampleSpacing() const { return _sampleSpacing; }
        void    setSampleSpacing( float32 spacing ) { _sampleSpacing = spacing; }

    private:
        /** @brief 같은 오브젝트의 물입니다. 없으면 nullptr 입니다. */
        WaterBodyComponent* findWater() const;
        /** @brief 같은 오브젝트의 스플라인입니다. 없으면 nullptr 입니다. */
        SplineComponent* findSpline() const;

        PROPERTY( Category = "River", DisplayName = "Sample Spacing", Min = 0.1, Units = m, Tooltip = "Distance between river points taken from the spline" )
        float32 _sampleSpacing;

        SW_TRANSIENT uint32 _lastRevision;      ///< 마지막으로 넘긴 스플라인 곡선 번호
        SW_TRANSIENT float3 _lastWaterPosition; ///< 마지막으로 넘길 때의 물 월드 위치
        SW_TRANSIENT bool   _bHasPushed;        ///< 한 번이라도 넘겼다
        SW_TRANSIENT bool   _bReportedMissing;  ///< 물 · 스플라인이 없다는 오류를 남겼다
    };

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

    void SplineRiverPathComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
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
                SW_LOG_ERROR_ID( "SplineRiverPathComponent.WaterBodyComponentSplineComponentBothRequired", "A WaterBodyComponent and a SplineComponent are both required on the same object - the river path is not driven" );
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
                SW_LOG_ERROR_ID( "SplineRiverPathComponent.SplineNoUsableCurve", "The spline has no usable curve (needs two control points) or the sample spacing is not above 0 - the river path is not driven" );
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

#include "SplineRiverPathComponent.cpp.gen.inl"
