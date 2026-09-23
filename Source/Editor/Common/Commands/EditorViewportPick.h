/**
 * @file EditorViewportPick.h
 * @brief 뷰포트 클릭 피킹 — 레이와 처음 만나는 오브젝트·컴포넌트 (ImGui 없음)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    struct float4x4;

    class Component;
    class GameObject;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /** @brief 화면 좌표에서 만든 월드 레이 */
    struct EditorPickRay
    {
        float3 _origin{};
        float3 _direction{}; ///< 정규화되어 있어야 합니다
    };

    /** @brief 피킹 결과. 맞은 것이 없으면 포인터가 nullptr입니다. */
    struct EditorPickResult
    {
        GameObject* _pObject{ nullptr };
        Component*  _pComponent{ nullptr };
        float32     _distance{ 0.0f }; ///< 레이 원점에서의 거리
    };

    /**
     * @class EditorViewportPick
     * @brief 뷰포트 피킹 로직. 컴포넌트 종류별 경계는 이 안의 표가 정합니다.
     * @details 예전에는 `EditorViewportClient.cpp` 안에 `considerMeshPick`·`considerSpritePick`·
     *          `considerBoxPick`·`considerScenePick` 네 함수가 있었고, 마지막 하나는
     *          `getPrimarySceneComponent()` **하나만** 봤습니다 — 그래서 게임이 만든 컴포넌트는
     *          그것이 주 컴포넌트가 아니면 뷰포트에서 클릭으로 집을 수 없었습니다.
     *          지금은 (1) 종류를 아는 제공자는 표의 한 줄이고 (2) 표가 못 잡은 오브젝트는 그
     *          오브젝트의 **모든** SceneComponent 를 기본 반지름으로 훑습니다.
     *          ImGui 를 쓰지 않으므로 `Test/EditorTest` 가 실제 씬으로 검증합니다.
     */
    class EditorViewportPick
    {
    public:
        /** @brief 기본 반지름 — 고유한 경계가 없는 컴포넌트를 집을 수 있게 하는 크기입니다. */
        static constexpr float32 kFallbackRadius = 0.35f;

        /**
         * @brief 레이와 가장 먼저 만나는 오브젝트·컴포넌트를 찾습니다.
         * @param b2DMode true면 같은 거리에서 스프라이트·콜라이더가 메시보다 우선합니다.
         * @return 맞은 것이 있으면 true이고 outResult를 채웁니다.
         */
        static bool pick( const GameObjectManager* pManager, const EditorPickRay& ray, bool b2DMode,
                          EditorPickResult& outResult );

        /** @brief 레이-구 교차. 맞으면 outHitT에 원점으로부터의 거리(0 이상)를 씁니다. */
        static bool rayHitsSphere( const float3& origin, const float3& dir, const float3& center, float32 radius,
                                   float32& outHitT );

        /**
         * @brief 캔버스 정규 좌표(u·v ∈ [0,1], 왼쪽 위가 0·0)에서 월드 레이를 만듭니다.
         * @details 근평면(NDC z 0)과 원평면(z 1)을 역투영해 잇는다. 원점은 근평면 위의 점, 방향은 단위 벡터다.
         *          피킹 · 자 · 애셋 드롭이 같은 레이를 쓴다 — 예전에는 셋이 이 계산을 각자 들었다.
         * @return 역행렬이 퇴화했으면 false.
         */
        static bool makeRay( const float4x4& invViewProj, float32 u, float32 v, EditorPickRay& outRay );

        /**
         * @brief 레이와 축 평면(axisIndex 0·1·2 = X·Y·Z = 0)의 교점. 나란하면 false.
         * @details 자는 바닥(Y = 0), 애셋 드롭은 2D 에서 Z = 0 · 3D 에서 Y = 0 을 쓴다. 평면이 레이 뒤에 있어도
         *          교점을 낸다(카메라가 바닥 아래일 때 드롭이 원점으로 튀지 않게). 교점의 축 성분은 정확히 0 이다.
         */
        static bool rayHitsAxisPlane( const EditorPickRay& ray, uint32 axisIndex, float3& outPoint );

        /** @brief 종류를 아는 피킹 제공자 개수입니다 (표가 비어 있지 않은지 보는 데 씁니다). */
        static uint32 getTypedProviderCount();
    };
} // namespace sw::editor
