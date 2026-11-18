/**
 * @file MeshModifierHandleEdit.h
 * @brief 뷰포트 Modifiers 화면 도구의 Model 쪽 값과 명령입니다 — 고른 모디파이어의 범위 · 기즈모 손잡이 · 격자 제어점을 값으로 모으고(`MeshModifierHandleViewState::gather`),
 *        도구(View `MeshModifiersVisualizer.cpp`)가 보낸 명령(잡기 · 끌기 · 놓기)을 적용합니다(`MeshModifierHandleCommand::apply`). 끌기 하나가 되돌리기 하나다.
 * @details 콜라이더 손잡이(`EditorColliderHandleEdit`)와 같은 모양이다 — 언리얼 컴포넌트 비주얼라이저가 손잡이 끌기를 트랜잭션으로 감싸는 자리. ImGui 를 모릅니다.
 *          - 기즈모: 중심의 기준 축 화살표 셋(이동)과 기준 축 둘레 고리 셋(회전 — 고리 k 는 오일러 성분 k 를 바꾼다).
 *          - 범위: 매개변수 정의의 힌트 · 이름으로 반경(구) · 하한/상한(주축 단면 사각형) · 중심(점) · 방향(화살표)을 월드 도형 값으로 낸다(`ModifierGizmoLogic::computeRangeKind`).
 *          - 제어점: 격자(lattice · ffd2 · ffd3 · ffd4)와 ffd2d 의 점. 점을 누르면 고르고(세션의 `setSelectedControlPoint`) 그대로 끌면 카메라를 마주 보는 평면에서,
 *            축 화살표를 끌면 그 축으로 옮긴다. 세션의 대칭 설정이 켜져 있으면 짝 점도 함께 옮긴다.
 *          마우스 양보(`EditorViewportMouseClaim`)는 적용이 한다: 잡을 때 가져가고 끄는 동안 끌기 명령마다 다시 잡으며 놓을 때 돌려준다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Ray.h"
#include "Core/Math/VectorMath.h"

namespace sw::editor
{
    struct EditorViewportVisualizerArgs;
    struct EditorVisualizerDrawList;

    /** @brief 손잡이의 종류입니다. */
    enum class MeshModifierHandleKind : uint8
    {
        None,
        GizmoMove,   ///< 기즈모 중심을 기준 축 하나로
        GizmoRotate, ///< 기즈모 오일러 성분 하나를 고리로
        PointAxis,   ///< 고른 제어점을 모디파이어 축 하나로
        PointFree,   ///< 고른 제어점을 카메라를 마주 보는 평면에서
    };

    /** @brief 이번 프레임에 집을 수 있는 손잡이 하나입니다(월드 값). */
    struct MeshModifierHandle
    {
        MeshModifierHandleKind _kind{ MeshModifierHandleKind::None };
        uint32                 _axis{ 0 };
        float3                 _origin{ 0.0f, 0.0f, 0.0f };
        float3                 _direction{ 0.0f, 0.0f, 0.0f }; ///< 이동은 끝점까지의 벡터, 회전은 고리 법선(단위)
        float32                _radius{ 0.0f };                ///< 회전 고리 반경

        /** @brief 손잡이와 레이의 거리입니다. 집을 수 없으면 아주 큰 값입니다. */
        float32 computeDistance( const Ray& ray ) const;
        /** @brief 같은 손잡이(종류 · 축)인지입니다 — 끄는 손잡이 강조에 쓴다. */
        bool isSameAs( const MeshModifierHandle& other ) const { return _kind == other._kind && _axis == other._axis; }
    };

    /** @brief 범위 도형의 종류입니다. 색은 모두 테마 `Viewport.ModifierRange` 다. */
    enum class MeshModifierRangeShapeKind : uint8
    {
        Segment, ///< 월드 선분
        Arrow,   ///< 월드 선분 + 화면 공간 머리
        Marker,  ///< 점(`_from`)의 화면 공간 십자
    };

    /** @brief 범위 매개변수(반경 · 한계 · 중심 · 방향)를 그리는 도형 하나입니다(월드 값). */
    struct MeshModifierRangeShape
    {
        MeshModifierRangeShapeKind _kind{ MeshModifierRangeShapeKind::Segment };
        float3                     _from{ 0.0f, 0.0f, 0.0f };
        float3                     _to{ 0.0f, 0.0f, 0.0f };
        float32                    _thickness{ 1.0f };
    };

    /** @brief 화면 도구가 이번 프레임에 그릴 값입니다(`gather`). */
    struct MeshModifierHandleViewState
    {
        static constexpr int32 kNoIndex = -1; ///< 번호 칸(모디파이어 · 제어점)이 비었다

        vector<MeshModifierRangeShape> _listRange;  ///< 범위 도형
        vector<MeshModifierHandle>     _listHandle; ///< 집을 수 있는 손잡이(고른 제어점 축 → 기즈모 순)
        vector<float3>                 _listPoint;  ///< 제어점(월드, x 가 가장 빨리 도는 격자 순)
        MeshModifierHandle             _active{};   ///< 끄는 손잡이(`_bDragging` 일 때만)
        float3                         _centerWorld{ 0.0f, 0.0f, 0.0f };
        float3                         _axisFrom{ 0.0f, 0.0f, 0.0f }; ///< 주축 선(기존 표시)
        float3                         _axisTo{ 0.0f, 0.0f, 0.0f };
        float32                        _pointPickRadius{ 0.0f }; ///< 제어점을 집는 구 반경(월드)
        int32                          _countX{ 0 };
        int32                          _countY{ 0 };
        int32                          _countZ{ 0 };
        int32                          _modifierIndex{ MeshModifierHandleViewState::kNoIndex };     ///< 고른 모디파이어
        int32                          _dragModifierIndex{ MeshModifierHandleViewState::kNoIndex }; ///< 끄는 손잡이의 모디파이어(`_bDragging` 일 때만)
        int32                          _selectedPoint{ MeshModifierHandleViewState::kNoIndex };     ///< 고른 제어점(없으면 음수)
        bool                           _bAvailable{ false };                                        ///< NOLINT(sw-bool-flag): 값 집합체 — 대상과 고른 모디파이어가 있고 공간 정보를 풀었다
        bool                           _bDragging{ false };                                         ///< NOLINT(sw-bool-flag): 값 집합체 — 손잡이를 끄는 중이다
        bool                           _bPointSet{ false };                                         ///< NOLINT(sw-bool-flag): 값 집합체 — 고른 모디파이어가 제어점을 가진다(격자 · ffd2d)

        /**
         * @brief 레이 아래의 손잡이(가장 가까운 것, 손잡이 길이 대비 두께 안)를 @p outHandle 에, 손잡이가 없으면 제어점을 @p outPoint 에 냅니다. 둘 다 없으면 비운다.
         * @details 마우스 · 화면을 모르는 판정이라 테스트가 값으로 부른다.
         */
        void pick( const Ray& ray, optional<int32>& outHandle, optional<int32>& outPoint ) const;
        /**
         * @brief 세션의 고른 모디파이어로 값을 모아 돌려줍니다(메인 씬 · 에디터 범위의 세션). 대상이 없으면 `_bAvailable` 이 거짓이다. 고른 제어점이 범위를 넘으면 마지막 점으로 고친다.
         * @details 값은 에디터 범위의 도구 상태(서브시스템)가 프레임마다 다시 채우는 버퍼다 — 다음 `gather` 까지 읽는다.
         */
        static const MeshModifierHandleViewState& gather();
    };

    /** @brief 화면 도구가 보내는 명령의 종류입니다. */
    enum class MeshModifierHandleCommandKind : uint8
    {
        Begin, ///< 손잡이를 잡았다 — 시작 값을 찍고 뷰포트 마우스를 가져간다(`_bSelectPoint` 면 제어점 `_pointIndex` 를 먼저 고른다)
        Drag,  ///< 끄는 중이다 — 마우스를 다시 잡고, 레이가 있으면(`_bHasRay`) 손잡이를 옮긴다. 첫 변경 직전에 되돌리기를 하나 남긴다
        End,   ///< 놓았거나 대상이 바뀌었다 — 끌기를 닫고 마우스를 돌려준다
    };

    /** @brief 화면 도구 명령 하나입니다(값만 든다). */
    struct MeshModifierHandleCommand
    {
        MeshModifierHandleCommandKind _kind{ MeshModifierHandleCommandKind::End };
        MeshModifierHandle            _handle{};
        Ray                           _ray{};
        int32                         _modifierIndex{ MeshModifierHandleViewState::kNoIndex };
        int32                         _pointIndex{ MeshModifierHandleViewState::kNoIndex };
        bool                          _bHasRay{ false };      ///< NOLINT(sw-bool-flag): 값 집합체 — 이번 프레임에 마우스 레이를 만들었다
        bool                          _bSelectPoint{ false }; ///< NOLINT(sw-bool-flag): 값 집합체 — 잡기 전에 제어점을 고른다(점을 눌렀다)

        /**
         * @brief 끄는 중의 한 프레임입니다. 대상이 없어졌거나 다른 모디파이어로 바뀌었거나 단추를 놓았으면 놓기를, 아니면 끌기를 냅니다.
         * @return 끌기가 이어지면 true 입니다(끄지 않는 중이면 아무것도 내지 않고 false).
         */
        static bool step( const MeshModifierHandleViewState& state, bool bMouseDown, const Ray* pRay, vector<MeshModifierHandleCommand>& outListCommand );
        /** @brief 누름입니다 — 손잡이 @p hoveredHandle 이 있으면 그것을, 없고 제어점 @p hoveredPoint 가 있으면 그 점을 골라 평면 끌기로 잡는다. 둘 다 없으면 아무것도 내지 않는다. */
        static void press( const MeshModifierHandleViewState& state, optional<int32> hoveredHandle, optional<int32> hoveredPoint, const Ray& ray,
                           vector<MeshModifierHandleCommand>& outListCommand );
        /** @brief 명령들을 차례로 적용합니다(에디터 범위의 세션 — 바로 앞 `gather` 의 공간 정보를 쓴다). */
        static void apply( const vector<MeshModifierHandleCommand>& listCommand );
    };

    /**
     * @struct MeshModifierPreviewGatherer
     * @brief 시각화 등록 줄의 모으기 진입점입니다 — 변형 결과 선(`meshmodifiers.preview`, CPU 미리보기를 먼저 갱신)과 고른 모디파이어까지의 결과 선(`meshmodifiers.stage`).
     */
    struct MeshModifierPreviewGatherer
    {
        static void gatherPreview( const EditorViewportVisualizerArgs& args, EditorVisualizerDrawList& outDrawList );
        static void gatherStage( const EditorViewportVisualizerArgs& args, EditorVisualizerDrawList& outDrawList );
    };
} // namespace sw::editor
