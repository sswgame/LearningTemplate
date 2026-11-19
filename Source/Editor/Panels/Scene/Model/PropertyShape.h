/**
 * @file PropertyShape.h
 * @brief 프로퍼티 메타로 선언한 모양(`Meta = "Shape=Sphere"`)의 값과 계산입니다 — 월드 틀, 모양 값, 그 모양의 선 · 손잡이, 손잡이를 끈 새 값(ImGui · 씬 없음).
 * @details ezEngine 조작기 · 시각화 속성(`ezSphereManipulatorAttribute` · `ezConeVisualizerAttribute`), 언리얼 `MakeEditWidget`, 유니티 `Handles.RadiusHandle` 자리다 —
 *          컴포넌트가 손 코드 없이 프로퍼티 메타 한 줄로 씬 뷰 모양과 손잡이를 얻는다. 선언을 읽고 값을 쓰는 쪽은 `PropertyShapeEdit.h`.
 *          손잡이 도구 View 가 셰이프 · 씬 헤더 없이 들 수 있게 값만 둔다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Ray.h"
#include "Core/Math/VectorMath.h"
#include "Core/Name/hashed_string.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    struct EditorVisualizerDrawList;
    struct EditorVisualizerLine;

    /** @brief 프로퍼티 메타 `Shape=` 가 고르는 모양입니다. */
    enum class PropertyShapeKind : uint8
    {
        Sphere,   ///< float32 반지름 — 세 원
        Box,      ///< float3 반 크기 — 상자 12 모서리
        Cone,     ///< float32 반각(rad) + `ShapeRange=` 가 가리키는 float32 길이 — 꼭짓점이 원점, 축은 로컬 +Z(빛 방향과 같다)
        Arrow,    ///< float3 로컬 방향 — 원점에서 그 방향으로 고정 길이 화살표
        Circle,   ///< float32 반지름 — 로컬 XY 평면의 원 하나(2D 빛 · 2D 범위)
        Capsule,  ///< float32 반지름 + `ShapeRange=` 가 가리키는 float32 원기둥 반 높이 — 축은 로컬 +Y(`ShapeOrigin=Base` 면 바닥이 원점, 캐릭터 컨트롤러)
        Cylinder, ///< float3 하나 — x 반지름 · y 반 높이(z 는 쓰지 않는다), 축은 로컬 +Y 이고 가운데가 원점(내비 장애물 원기둥)
    };

    /** @brief @p kind 의 값이 float3 이면(상자 · 화살표 · 원기둥) true, float32 · 각이면 false 입니다. */
    constexpr bool isVectorShapeKind( PropertyShapeKind kind )
    {
        return kind == PropertyShapeKind::Box || kind == PropertyShapeKind::Arrow || kind == PropertyShapeKind::Cylinder;
    }

    /**
     * @struct PropertyShapeFrame
     * @brief 모양을 놓는 월드 틀입니다 — 원점, 회전만 든 세 단위 축, 축마다의 월드 배율 크기(`_scale`).
     * @details 배율을 값에 먹일지는 그 값을 쓰는 쪽의 규칙이다(`PropertyShapeScaleRule` — 점광 반경 · 오디오 존은 보지 않고, 캐릭터 캡슐은 물리 규칙으로 본다).
     *          축은 배율을 뺀 방향이라 배율은 `_scale` 로만 들어간다.
     */
    struct SW_EDITOR_API PropertyShapeFrame
    {
        float3 _origin{};
        float3 _axisX{ 1.0f, 0.0f, 0.0f };
        float3 _axisY{ 0.0f, 1.0f, 0.0f };
        float3 _axisZ{ 0.0f, 0.0f, 1.0f };
        float3 _scale{ 1.0f, 1.0f, 1.0f }; ///< 로컬 축마다 월드 배율의 크기(부호 없음)

        /** @brief 로컬 축 @p axis(0 X, 1 Y, 2 Z)입니다. */
        const float3& getAxis( uint32 axis ) const;
        /** @brief 로컬 방향을 월드 방향으로 돌립니다. */
        float3 toWorldVector( const float3& local ) const;
        /** @brief 월드 방향을 로컬 방향으로 돌립니다(축이 직교 단위라 내적이 곧 역회전이다). */
        float3 toLocalVector( const float3& world ) const;
    };

    /** @brief 손잡이가 바꾸는 값입니다. */
    enum class PropertyShapeHandleKind : uint8
    {
        Radius,         ///< 구(±X · ±Y · ±Z) · 원(±X · ±Y) · 캡슐(±X · ±Z) 반지름
        BoxExtent,      ///< 상자 반 크기의 한 축
        ConeAngle,      ///< 원뿔 반각 — 테두리 원 위의 점(±X · ±Y)을 축에서 멀리 끌면 넓어진다
        ConeRange,      ///< 원뿔 길이 — 축(+Z) 끝
        ArrowTip,       ///< 화살표 끝 — 끈 자리 쪽으로 방향을 돌린다(길이는 지킨다)
        CapsuleHeight,  ///< 캡슐 원기둥 반 높이 — 축(±Y) 끝의 반구 꼭대기까지의 거리로 끈다
        CylinderRadius, ///< 원기둥 반지름(float3 의 x) — 옆(±X · ±Z)으로 끈다
    };

    /**
     * @struct PropertyShapeHandle
     * @brief 손잡이 하나입니다 — 바꿀 프로퍼티(컴포넌트 id · 깊이 0 이름), 잡은 때의 틀 · 값, 화면에 그릴 자리.
     * @details 끄는 동안은 잡은 때의 값으로 계산한다(`computeDraggedNumber` · `computeDraggedVector`) — 콜라이더 손잡이(`ColliderHandle`)와 같다.
     */
    struct SW_EDITOR_API PropertyShapeHandle
    {
        PropertyShapeFrame      _frame{};
        float3                  _center{};                        ///< 손잡이 축의 시작점(월드) — 끈 거리를 여기서 잰다
        float3                  _direction{};                     ///< 손잡이 축(월드 단위 벡터)
        float3                  _position{};                      ///< 손잡이 위치(월드)
        float3                  _vector{};                        ///< 잡은 때의 벡터 값(상자 반 크기 · 화살표 로컬 방향)
        hashed_string           _property{};                      ///< 쓰는 프로퍼티(깊이 0 이름)
        uint64                  _componentID{ 0 };                ///< 대상 컴포넌트
        float3                  _vectorScale{ 1.0f, 1.0f, 1.0f }; ///< 벡터 값의 저장 → 월드 배율(`BoxExtent` 가 끈 거리를 나눠 저장 값으로 되쓴다)
        float32                 _number{ 0.0f };                  ///< 잡은 때의 float32 값(월드 — `CapsuleHeight` 는 캡슐 반지름을 뺄 때 쓴다)
        float32                 _range{ 0.0f };                   ///< 잡은 때의 둘째 값(월드 — `ConeAngle` 은 꼭짓점에서 테두리 원 중심까지)
        float32                 _numberScale{ 1.0f };             ///< float32 값의 저장 → 월드 배율(끈 거리를 나눠 저장 값으로 되쓴다)
        float32                 _rangeScale{ 1.0f };              ///< 둘째 값의 저장 → 월드 배율
        float32                 _heightSpan{ 1.0f };              ///< `CapsuleHeight` — 축 시작점에서 꼭대기까지가 (반 높이 + 반지름)의 몇 배인지(가운데 원점 1, 바닥 원점 2)
        uint32                  _axis{ 0 };                       ///< 로컬 축(0 X, 1 Y, 2 Z)
        float32                 _sign{ 1.0f };                    ///< 로컬 축의 양(+1)과 음(-1)
        PropertyShapeHandleKind _kind{ PropertyShapeHandleKind::Radius };

        /** @brief 값이 float3 이면(상자 반 크기 · 화살표 · 원기둥) true, float32 이면 false 입니다. */
        bool isVectorValue() const
        {
            return _kind == PropertyShapeHandleKind::BoxExtent || _kind == PropertyShapeHandleKind::ArrowTip || _kind == PropertyShapeHandleKind::CylinderRadius;
        }
        /** @brief 같은 손잡이(같은 컴포넌트 · 프로퍼티 · 종류 · 축 · 부호)이면 true 입니다 — 도구가 끄는 손잡이를 강조할 때 본다. */
        bool isSameHandle( const PropertyShapeHandle& other ) const;
        /**
         * @brief 레이 @p ray 가 가리키는 자리로 끈 float32 저장 값입니다(반지름 · 반각 · 길이 — 월드 거리를 그 값의 배율로 나눈다).
         * @details 길이는 월드에서 `PropertyShapeValue::kMinExtent` 로 아래를 막는다. 반각은 0 ~ `PropertyShapeValue::kMaxConeAngle`, 캡슐 반 높이는 0 이상이다.
         *          끈 거리는 손잡이 축 위에서 레이와 가장 가까운 점이다(`Ray::computeClosestAxisParameter` — 콜라이더 손잡이와 같은 식).
         */
        float32 computeDraggedNumber( const Ray& ray ) const;
        /**
         * @brief 레이 @p ray 가 가리키는 자리로 끈 float3 저장 값입니다(상자 반 크기는 그 축만 — 배율로 나눈다, 화살표는 잡은 길이를 지킨 새 방향 — 잡은 길이가 0 이면 단위 길이).
         * @details 원기둥 반지름은 옆 축 어느 쪽에서 끌어도 x 에, 원기둥 반 높이(`BoxExtent` 축 Y)는 y 에 쓴다.
         */
        float3 computeDraggedVector( const Ray& ray ) const;
    };

    /**
     * @struct PropertyShapeValue
     * @brief 선언 하나의 지금 값입니다 — 모양, 틀, 저장 값(float32 는 `_number`, float3 는 `_vector`), 둘째 값(`_range` — 원뿔 길이 · 캡슐 반 높이),
     *        저장 → 월드 배율(`_numberScale` · `_rangeScale` · `_vectorScale` — 쓰는 쪽 규칙으로 정한다, `PropertyShapeDecl::makeValue`).
     */
    struct SW_EDITOR_API PropertyShapeValue
    {
        /** @brief 손잡이를 끌어 줄일 수 있는 가장 작은 길이(m)입니다. 0 이면 모양이 사라져 다시 잡을 수 없다. */
        static constexpr float32 kMinExtent = 0.01f;
        /** @brief 그릴 · 끌 수 있는 원뿔 반각의 끝(rad, 약 87.7°)입니다. 90° 에서 테두리 원이 무한히 커진다. */
        static constexpr float32 kMaxConeAngle = 1.53f;
        /** @brief 화살표 길이(m)입니다 — 방향만 고치는 값이라 길이는 정해 둔다(ezEngine 방향 시각화의 고정 배율 자리). */
        static constexpr float32 kArrowLength = 1.0f;
        /** @brief 화살촉 길이 ÷ 화살표 길이입니다. */
        static constexpr float32 kArrowHeadRatio = 0.2f;
        /** @brief 원 하나를 긋는 선분 수입니다. */
        static constexpr uint32 kCircleSegmentCount = 32;
        /** @brief 손잡이를 둘 수 있는 가장 작은 배율입니다. 0 배율은 끈 거리를 저장 값으로 되돌릴 수 없다(물리도 짓지 않는 모양). */
        static constexpr float32 kMinScale = 1.0e-4f;

        PropertyShapeFrame _frame{};
        float3             _vector{};
        float32            _number{ 0.0f };
        float32            _range{ 0.0f };
        float3             _vectorScale{ 1.0f, 1.0f, 1.0f };
        float32            _numberScale{ 1.0f };
        float32            _rangeScale{ 1.0f };
        PropertyShapeKind  _kind{ PropertyShapeKind::Sphere };
        uint8              _bBaseOrigin{ 0 }; ///< 캡슐 바닥이 틀 원점이다(`ShapeOrigin=Base`). 아니면 가운데가 원점이다

        /** @brief 모양의 선을 @p outDrawList 에 더합니다(@p prototype 의 색 · 굵기 · 역할을 쓴다). */
        void addLines( const EditorVisualizerLine& prototype, EditorVisualizerDrawList& outDrawList ) const;
        /**
         * @brief 모양의 손잡이를 @p outListHandle 뒤에 붙입니다(비우지 않는다). 원뿔 길이 · 캡슐 반 높이 손잡이는 @p rangeProperty 를 쓴다.
         * @details 값의 배율이 `kMinScale` 보다 작으면 손잡이가 없다. 구 6, 상자 6, 원뿔 5(반각 4 · 길이 1 — 길이가 `kMinExtent` 보다 짧으면 반각 손잡이는 없다), 화살표 1, 원 4, 캡슐 6(바닥 원점이면 5 — 바닥 쪽 높이 손잡이는 없다),
         *          원기둥 6(반지름 4 · 반 높이 2)이다.
         */
        void collectHandles( uint64 componentID, const hashed_string& property, const hashed_string& rangeProperty, vector<PropertyShapeHandle>& outListHandle ) const;
        /** @brief 화살표가 가리키는 월드 단위 방향입니다(로컬 방향이 0 이면 로컬 +Z). */
        float3 computeArrowDirection() const;
        /** @brief 월드 float32 값(저장 값 × 배율)입니다. */
        float32 getWorldNumber() const { return _number * _numberScale; }
        /** @brief 월드 둘째 값입니다. */
        float32 getWorldRange() const { return _range * _rangeScale; }
        /** @brief 월드 벡터 값(축마다 배율을 곱한다)입니다. */
        float3 getWorldVector() const { return float3::multiply( _vector, _vectorScale ); }
        /** @brief 캡슐 가운데(월드)입니다 — 바닥 원점이면 원점에서 +Y 로 (반 높이 + 반지름). */
        float3 computeCapsuleCenter() const;
    };
} // namespace sw::editor
