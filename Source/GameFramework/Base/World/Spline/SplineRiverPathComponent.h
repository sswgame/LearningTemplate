/**
 * @file SplineRiverPathComponent.h
 * @brief 같은 오브젝트의 `SplineComponent` 곡선으로 `WaterBodyComponent` 의 강 경로를 정하는 어댑터입니다.
 * @details `SplineComponent` 는 GameFramework(`Base/World/Spline`)에 있어 Engine 의 `WaterBodyComponent` 가 읽을 수 없다(층 방향). 그래서 이 컴포넌트가
 *          스플라인의 월드 곡선(`getWorldPath`)을 일정 간격으로 샘플해 물의 오너 위치 기준 점으로 바꾸고 `WaterBodyComponent::setRiverPathFromSource` 로 넘긴다.
 *          그때부터 경로의 정본은 스플라인 하나다 — 물의 점 목록 속성은 읽기 전용이 되고(`EditCondition`) 이 컴포넌트가 읽을 때마다 덮는다.
 *          스플라인을 고치거나(곡선 번호 `SplineComponent::getPathRevision` 이 오른다) 물을 옮기면(스플라인의 월드 곡선은 안 움직인다) 틱에서 다시 샘플해 수면을 다시 만든다.
 *          물이나 스플라인이 없으면 오류를 한 번 남기고 아무것도 하지 않는다 — 다른 경로로 대신하지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    class SplineComponent;
    class SplinePath;
    class WaterBodyComponent;
} // namespace sw

namespace sw
{
    /**
     * @class SplineRiverPathComponent
     * @brief 스플라인 곡선을 강 경로로 넘기는 어댑터. 물(`WaterBodyComponent`)과 스플라인(`SplineComponent`)이 같은 오브젝트에 있어야 한다.
     */
    REFLECT( Category = "Environment", DisplayName = "Spline River Path", Tooltip = "Drives the river path of the water body on this object from the spline on this object" )
    class SW_GF_API SplineRiverPathComponent : public Component
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
        void onTick( float32 deltaTime ) override;

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

        uint32 _lastRevision;      ///< 마지막으로 넘긴 스플라인 곡선 번호
        float3 _lastWaterPosition; ///< 마지막으로 넘길 때의 물 월드 위치
        bool   _bHasPushed;        ///< 한 번이라도 넘겼다
        bool   _bReportedMissing;  ///< 물 · 스플라인이 없다는 오류를 남겼다
    };
} // namespace sw
