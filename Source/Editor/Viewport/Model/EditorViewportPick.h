/**
 * @file EditorViewportPick.h
 * @brief 씬 뷰 클릭 선택 · 호버 · 상자 선택의 Model 쪽입니다 — 마우스 아래 오브젝트를 id 로 찾고, 고른 id 를 메인 선택에 적용합니다. ImGui 를 모릅니다.
 * @details View(`EditorViewportPicker`)는 마우스 · 수정 키 · 호버 빈도만 다루고, 레이 · 화면 배지 목록 · 뷰-투영 값을 넘긴다. 언리얼 뷰포트 클라이언트가 HitProxy 로 찾은 것을
 *          선택 트랜잭션으로 넘기는 가름이다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/Ray.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    struct EditorVisualizerScreenBillboard;

    /** @brief 클릭 · 상자 선택이 고르는 방식입니다(누를 때의 수정 키 — Ctrl 넣고 빼기 · Shift 더하기, 언리얼 뷰포트 · 아웃라이너와 같다). */
    enum class EditorPickSelectMode : uint8
    {
        Replace,
        Add,
        Toggle,
    };

    /** @brief 마우스 아래에서 찾은 것입니다. 없으면 id 가 0 입니다. */
    struct EditorViewportPickHit
    {
        uint64 _objectID{ 0 };
        uint64 _componentID{ 0 };
    };

    /** @brief 피킹 한 번의 입력 값입니다(뷰포트가 프레임마다 채운다). */
    struct EditorViewportPickInput
    {
        float4x4                                       _viewProj{};
        float2                                         _canvasPos{};
        float2                                         _canvasSize{};
        uint64                                         _skipObjectID{ 0 };               ///< 화면을 그리는 에디터 카메라의 오브젝트(상자 선택이 고르지 않는다)
        const vector<EditorVisualizerScreenBillboard>* _pListScreenBillboard{ nullptr }; ///< 이번 프레임에 그은 아이콘 배지(빌보드 시각화를 끄면 비었다)
        bool                                           _b2DMode{ false };                ///< NOLINT(sw-bool-flag): 값 집합체 — 같은 거리에서 스프라이트 · 콜라이더가 메시보다 우선한다
    };

    /**
     * @struct EditorViewportPick
     * @brief 피킹 판정과 선택 적용입니다(상태 없음, 메인 스레드).
     */
    struct SW_EDITOR_API EditorViewportPick
    {
        /** @brief 화면 점 @p point 에서 배지 반지름 안의 가장 가까운 배지 위치입니다. 없으면 `constant::invalid_index::kUint32` 입니다. */
        [[nodiscard]] static uint32 findBillboardAt( const vector<EditorVisualizerScreenBillboard>& listBillboard, const float2& point );
        /**
         * @brief 마우스 아래 오브젝트를 찾습니다. 배지를 먼저 보고(메시가 없어 레이로 집히지 않는다 — 언리얼 빌보드 · 유니티 Gizmo 아이콘 클릭) 레이 피킹(`EditorViewportRayPick`)을 한다.
         * @details 잠근 오브젝트의 배지가 맞으면 그 자리는 빈 곳이다(뒤의 레이 피킹으로 넘기지 않는다). 잠그거나 숨긴 오브젝트는 빈 곳으로 처리한다.
         * @param pRay 마우스 레이입니다. 캔버스 밖이면 nullptr — 그때는 아무것도 찾지 않는다.
         */
        [[nodiscard]] static bool findUnderMouse( const EditorViewportPickInput& input, const float2& mouse, const Ray* pRay, EditorViewportPickHit& outHit );
        /** @brief 클릭 결과를 메인 선택에 적용합니다. 맞은 것이 있으면 `Replace` 는 그 컴포넌트를 고르고 나머지는 오브젝트를 더하거나 넣고 뺀다. 빈 곳이면 `Replace` 만 선택을 비운다. */
        static void applyClick( const EditorViewportPickHit& hit, EditorPickSelectMode mode );
        /**
         * @brief 화면 사각형 [@p minPos, @p maxPos] 에 걸친 오브젝트를 고릅니다. 크기를 선언한 오브젝트는 상자의 화면 사각형이 겹치면, 크기가 없으면 피벗이 안이면 걸친 것이다.
         * @details 잠근 · 숨긴 오브젝트와 화면을 그리는 에디터 카메라는 고르지 않는다(클릭 선택과 같은 규칙). 언리얼 박스 선택의 기본(걸치면 고름) · 유니티 `HandleUtility.PickRectObjects` 와 같다.
         */
        static void selectInRect( const EditorViewportPickInput& input, const float2& minPos, const float2& maxPos, EditorPickSelectMode mode );
        /** @brief @p objectID 가 메인 선택에 들었으면 true 입니다(호버가 이미 고른 것을 강조하지 않는다). */
        [[nodiscard]] static bool isSelected( uint64 objectID );
        /** @brief 메인 선택의 오브젝트 id 들입니다(우클릭 메뉴가 받는다). */
        static void getSelectedIDs( vector<uint64>& outListID );
        /** @brief 메인 선택을 @p objectID 하나로 바꿉니다(선택 밖 오브젝트 위에서 연 우클릭 메뉴 — R2). */
        static void selectOnly( uint64 objectID );
    };
} // namespace sw::editor
