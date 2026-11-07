/**
 * @file SurfaceQuery.h
 * @brief 공통 표면 질의입니다. 한 점 아래가 어떤 표면인지(종류 · 법선 · 표면 속도 · 이동 계수 · 발소리 분류)를 형식(콜라이더 · 지형 레이어 · 물)과 상관없이 한 모양으로 냅니다.
 * @details 발소리(`Footstep` 알림), 발자국(`SurfaceFootprintSystem`), 디딘 표면의 이동 계수(`GroundSurfaceComponent`), 진단 명령(`surface.probe`)이 모두 이 질의 하나를 쓴다.
 *          형식별 데이터를 한 포맷으로 합치지 않고, 각 형식이 물리 재질 이름 하나를 내는 어댑터다(`SurfaceKind.h` 머리말).
 *          - **물**: 맞은 점이 물(`WaterBodyComponent`)의 수면 아래면 표의 물 종류다. 깊이도 함께 낸다.
 *          - **지형 레이어**: 맞은 바디가 지형의 오브젝트면 그 점의 레이어 가중치(`TerrainComponent::computeLayerWeightsAt`)에서 가장 무거운 레이어가 재질을 적었으면 그 재질이다.
 *          - **콜라이더**: 그 밖에는 맞은 셰이프의 물리 재질이다. 이름이 빈 셰이프는 물리 설정의 첫 재질이다(물리 씬이 만드는 규칙과 같다).
 *          읽기만 하고 아무것도 바꾸지 않는다 — 발걸음마다 높이장이나 충돌 형상을 다시 만들지 않는다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct CharacterRayHit;
    struct SurfaceKindDef;
    struct SurfaceKindTable;

    class GameObjectManager;

    /** @brief 표면 종류를 정한 형식입니다(진단 표시용). */
    enum class SurfaceSource : uint8
    {
        Collider = 0, ///< 맞은 셰이프의 물리 재질
        TerrainLayer, ///< 지형의 가장 무거운 레이어가 적은 물리 재질
        Water         ///< 물속(표의 물 종류)
    };

    /** @brief 표면 질의 한 번의 결과입니다. */
    struct SW_API SurfaceSample
    {
        /** @brief 지형 레이어가 아닐 때의 `_terrainLayer` 입니다. */
        static constexpr uint8 kNoTerrainLayer = 0xFF;

        const SurfaceKindDef* _pKind{ nullptr };   ///< 표면 종류(질의가 성공하면 늘 있다)
        hashed_string         _material{};         ///< 종류를 고른 이름(물리 재질 · 물 종류)
        float3                _point{};            ///< 맞은 점(물이면 바닥의 점)
        float3                _normal{};           ///< 맞은 면의 법선
        float3                _surfaceVelocity{};  ///< 맞은 바디의 선속도(정적이면 0) — 지지체가 실어 나르는 속도
        uint64                _objectID{ 0 };      ///< 맞은 오브젝트(없으면 0)
        float32               _waterDepth{ 0.0f }; ///< 바닥 점에서 수면까지의 깊이(물이 아니면 0)
        uint8                 _terrainLayer{ kNoTerrainLayer };
        SurfaceSource         _source{ SurfaceSource::Collider };
    };

    /** @brief 표면 질의 도우미입니다(전부 static, 게임 스레드에서 부른다 — 물리 씬 질의). */
    struct SW_API SurfaceQuery
    {
        /**
         * @brief @p from → @p to 로 광선을 쏘아(오브젝트 @p ignoreObjectID 의 바디는 건너뜀) 맞은 표면을 고릅니다.
         * @return 맞은 것이 없거나 맞은 재질의 종류가 표에 없으면 false 입니다(후자는 이름마다 한 번 오류를 남긴다 — 다른 종류로 대신하지 않는다).
         */
        [[nodiscard]] static bool sampleBelow( GameObjectManager& manager, const SurfaceKindTable& table, const float3& from, const float3& to, uint64 ignoreObjectID, bool bIs2D,
                                               SurfaceSample& outSample );
        /** @brief 이미 맞은 것(@p hit)의 표면을 고릅니다. 실패 규칙은 `sampleBelow` 와 같습니다. */
        [[nodiscard]] static bool resolveHit( GameObjectManager& manager, const SurfaceKindTable& table, const CharacterRayHit& hit, SurfaceSample& outSample );
        /** @brief 형식의 이름입니다(진단 줄 · 감시 값). */
        static const utf8* getSourceName( SurfaceSource source );
    };
} // namespace sw
