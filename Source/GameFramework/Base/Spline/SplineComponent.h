/**
 * @file SplineComponent.h
 * @brief 씬에 놓는 스플라인 — 조절점(소유 오브젝트 로컬)을 저장하고 `SplinePath` 를 들어 줍니다. 무버 · 카메라 레일 · 길이 같은 곡선을 읽습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Spline/SplinePath.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class SplineComponent
     * @brief 조절점은 소유 오브젝트의 주 씬 컴포넌트 기준 로컬 좌표입니다. 플레이가 시작되면 그 순간의 월드 변환으로 **월드 곡선을 굳힙니다**
     *        (`getWorldPath`) — 곡선을 단 오브젝트가 그 곡선을 따라 움직여도(발판 하나짜리 프리팹) 길이 같이 움직이지 않습니다.
     * @details 틱하지 않습니다. 값을 바꾸면(`setControlPoints` · 에디터 편집) 로컬 곡선을 다시 짓고, 플레이 중이면 월드 곡선도 지금 변환으로 다시 굳힙니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Spline", Tooltip = "Catmull-Rom / Bezier / linear path in owner space, arc-length parameterized" )
    class SW_GF_API SplineComponent : public Component
    {
    public:
        REFLECT_BODY();

        SplineComponent();
        virtual ~SplineComponent() override = default;

        void onBeginPlay() override;
        void onPostLoad() override;
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 조절점 · 방식 · 닫힘을 한 번에 바꾸고 곡선을 다시 짓습니다. */
        void setControlPoints( const vector<float3>& listControlPoint, SplineType type, bool bClosed );
        /** @brief 지금 소유자의 월드 변환으로 월드 곡선을 다시 굳힙니다. */
        void freezeWorldPath();

        const vector<float3>& getControlPoints() const { return _listControlPoint; }
        /** @brief 소유자 로컬 곡선입니다. */
        const SplinePath& getLocalPath() const { return _localPath; }
        /** @brief 굳힌 월드 곡선입니다(플레이 전이면 마지막으로 굳힌 것 — `freezeWorldPath`). */
        const SplinePath& getWorldPath() const { return _worldPath; }

    private:
        void rebuildLocalPath();

    private:
        PROPERTY( Category = "Spline", DisplayName = "Control Points", Tooltip = "Owner-local points; Bezier uses [anchor, handle, handle, anchor, ...]", Units = m )
        vector<float3> _listControlPoint;
        PROPERTY( Category = "Spline", DisplayName = "Samples Per Segment", Min = 1, Tooltip = "Arc-length table density" )
        int32 _samplesPerSegment;
        PROPERTY( Category = "Spline", DisplayName = "Type" )
        SplineType _type;
        PROPERTY( Category = "Spline", DisplayName = "Closed", Tooltip = "Loop back from the last point to the first" )
        bool _bClosed;

        SplinePath _localPath;
        SplinePath _worldPath;
    };
} // namespace sw
