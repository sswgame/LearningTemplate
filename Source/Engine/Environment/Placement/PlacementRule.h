/**
 * @file PlacementRule.h
 * @brief 규칙 기반 배치(흩뿌리기)입니다 — 밀도 · 최소 거리(푸아송) · 경사 · 높이 · 레이어 필터 · 제외 영역 · 무작위 크기 · 회전을
 *        씨앗 고정으로 계산합니다. 2D 와 3D 가 같은 코어를 씁니다.
 * @details 코어는 **평면 좌표 (u, v)** 위에서 돕니다. 3D 는 u = x, v = z 이고 높이는 표면(`IPlacementSurface`)이 줍니다(지형).
 *          2D 는 u = x, v = y 이고 표면은 타일 맵(`PlacementTileSurface`)이나 없음입니다 — 높이 0 · 위쪽 노멀 · 레이어 가중치만 다릅니다.
 *          계산은 후보마다 (씨앗, 후보 번호) 해시로 난수를 뽑으므로 같은 입력은 같은 결과이고, 후보 하나의 판정이 다른 후보의 난수를
 *          밀지 않습니다(필터 하나를 바꿔도 남는 자리는 그대로입니다). 언리얼 PCG · Horizon 의 배치 규칙과 같은 자리입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 제외 영역의 모양입니다. 평면 위의 도형이고 높이 방향으로는 끝이 없습니다(3D 에서는 기둥 · 직육면체 기둥). */
    ENUM()
    enum class PlacementExclusionShape : uint8
    {
        Circle = 0, ///< 중심 · 반지름
        Rect,       ///< 중심 · 반폭(축 정렬)
    };
} // namespace sw

namespace sw
{
    /** @brief 아무것도 놓지 않는 평면 영역 하나입니다(길 · 건물 자리 · 물가). */
    REFLECT()
    struct SW_API PlacementExclusion
    {
        REFLECT_BODY();

        PROPERTY( Meta = "Units=m" )
        float2 _center{};
        PROPERTY( Min = 0.0, Meta = "Units=m" )
        float2 _halfExtent{ 1.0f, 1.0f }; ///< 사각형의 반폭
        PROPERTY( Min = 0.0, Meta = "Units=m" )
        float32 _radius{ 1.0f }; ///< 원의 반지름
        PROPERTY()
        PlacementExclusionShape _shape{ PlacementExclusionShape::Circle };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 배치 규칙 하나 — 무엇을 놓을지가 아니라 **어디에 어떻게** 놓을지입니다. 놓을 것(메시 · 스프라이트)의 목록과 비중은 쓰는 쪽이 따로 줍니다.
     * @details 필터는 모두 "통과해야 놓는다" 입니다. 레이어 번호가 −1 이면 그 필터를 끕니다. 각도는 라디안입니다.
     */
    REFLECT()
    struct SW_API PlacementRule
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Tooltip = "Candidates per square unit before filters" )
        float32 _density{ 0.1f };
        PROPERTY( Min = 0.0, Meta = "Units=m", Tooltip = "No two placements closer than this (Poisson disk)" )
        float32 _minDistance{ 0.0f };
        PROPERTY( Min = 0.01 )
        float32 _scaleMin{ 1.0f };
        PROPERTY( Min = 0.01 )
        float32 _scaleMax{ 1.0f };
        PROPERTY( Meta = "Units=rad" )
        float32 _yawMin{ 0.0f };
        PROPERTY( Meta = "Units=rad" )
        float32 _yawMax{ 6.2831853f };
        PROPERTY( Min = 0.0, Max = 1.0, Meta = "Units=ratio", Tooltip = "0 stands upright, 1 follows the surface normal" )
        float32 _alignToNormal{ 0.0f };
        PROPERTY( Min = 0.0, Meta = "Units=rad" )
        float32 _slopeMin{ 0.0f };
        PROPERTY( Min = 0.0, Meta = "Units=rad" )
        float32 _slopeMax{ 1.5707964f };
        PROPERTY( Meta = "Units=m" )
        float32 _heightMin{ -100000.0f };
        PROPERTY( Meta = "Units=m" )
        float32 _heightMax{ 100000.0f };
        PROPERTY( Meta = "Units=m", Tooltip = "Added to the surface height (negative sinks into the ground)" )
        float32 _heightOffset{ 0.0f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Minimum weight of the filter layer" )
        float32 _layerMinWeight{ 0.5f };
        PROPERTY( Min = -1, Max = 3, Tooltip = "Surface layer that must be present (-1 = any)" )
        int32 _layerIndex{ -1 };
        PROPERTY( Min = -1, Max = 3, Tooltip = "Surface layer whose weight scales the density (density map, -1 = none)" )
        int32 _densityLayerIndex{ -1 };
        PROPERTY( Tooltip = "Same seed, same layout" )
        uint32 _seed{ 1u };
    };
} // namespace sw

namespace sw
{
    /** @brief 표면 한 점의 값입니다. 레이어 가중치는 넷까지이고 합이 1 이 아니어도 됩니다(필터는 각 값을 그대로 봅니다). */
    struct PlacementSurfaceSample
    {
        float4  _layerWeight{ 1.0f, 0.0f, 0.0f, 0.0f };
        float3  _normal{ 0.0f, 1.0f, 0.0f }; ///< 3D 는 위쪽이 +Y 입니다. 2D 표면도 같은 값을 줍니다(경사 0)
        float32 _height{ 0.0f };
        uint8   _bValid{ SW_TRUE }; ///< 놓을 수 없는 자리(지형 구멍 · 맵 밖)면 SW_FALSE
    };
} // namespace sw

namespace sw
{
    /**
     * @class IPlacementSurface
     * @brief 배치가 묻는 표면입니다 — 지형(3D)과 타일 맵(2D)이 구현합니다. 표면이 없으면 높이 0 · 평평함 · 레이어 0 이 가득입니다.
     * @details 스레드: 배치 계산 동안 읽기만 합니다.
     */
    class SW_API IPlacementSurface
    {
    public:
        IPlacementSurface()          = default;
        virtual ~IPlacementSurface() = default;

        IPlacementSurface( const IPlacementSurface& )            = delete;
        IPlacementSurface& operator=( const IPlacementSurface& ) = delete;

        /** @brief 평면 좌표 @p planePosition 의 표면 값을 채웁니다. 표면 밖이면 false 입니다. */
        [[nodiscard]] virtual bool sampleSurface( const float2& planePosition, PlacementSurfaceSample& outSample ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 배치 하나입니다. 평면 좌표와 높이, 표면 노멀, 크기 · 요(라디안), 고른 항목 번호입니다. */
    struct PlacementInstance
    {
        float3  _normal{ 0.0f, 1.0f, 0.0f };
        float2  _planePosition{};
        float32 _height{ 0.0f };
        float32 _scale{ 1.0f };
        float32 _yaw{ 0.0f };
        float32 _alignToNormal{ 0.0f }; ///< 규칙의 값을 그대로 싣습니다(월드 행렬을 만들 때 씁니다)
        uint32  _hash{ 0 };             ///< 후보 해시 — 인스턴스마다 다른 값(색 흔들기 · 바람 위상)이 필요하면 씁니다
        int32   _entryIndex{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 배치를 계산할 평면 사각형입니다. */
    struct PlacementRegion
    {
        float2 _min{};
        float2 _max{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PlacementScatter
     * @brief 규칙대로 배치를 계산합니다. 씬 없이 돌고 같은 입력이면 같은 결과입니다.
     * @details 순서: 후보 수 = 밀도 × 면적 → 후보마다 (씨앗, 번호) 해시로 위치 → 제외 영역 → 표면(구멍) → 경사 · 높이 · 레이어 필터 →
     *          밀도 레이어(가중치 확률로 버림) → 최소 거리(이미 놓인 것과 격자 해시로 대조) → 항목 · 크기 · 요. 최소 거리는 먼저 놓인 것이
     *          이기는 다트 던지기라 결과가 후보 순서에만 달려 있습니다(결정적).
     */
    struct SW_API PlacementScatter
    {
        /** @brief 후보 수 상한입니다. 밀도 × 면적이 이보다 크면 잘라 쓰고 경고합니다. */
        static constexpr uint32 kMaxCandidateCount = 4u * 1024u * 1024u;

        /**
         * @brief 규칙 @p rule 로 @p region 에 배치를 계산해 @p outListInstance 에 채웁니다.
         * @param pSurface nullptr 이면 평평한 높이 0 표면입니다.
         * @param listEntryWeight 항목별 비중입니다. 비었으면 항목 번호는 늘 0 입니다. 비중 합이 0 이면 아무것도 놓지 않습니다.
         */
        static void scatter( const PlacementRule& rule, const PlacementRegion& region, const IPlacementSurface* pSurface,
                             const vector<PlacementExclusion>& listExclusion, const vector<float32>& listEntryWeight,
                             vector<PlacementInstance>& outListInstance );

        /** @brief 노멀의 경사(위쪽과 이루는 각, 라디안)입니다. */
        static float32 computeSlope( const float3& normal );
        /** @brief 평면 좌표가 제외 영역 안이면 true 입니다. */
        static bool isExcluded( const vector<PlacementExclusion>& listExclusion, const float2& planePosition );
        /** @brief 비중대로 고른 항목 번호입니다. @p unit 은 [0, 1) 입니다. 비중 합이 0 이면 −1 입니다. */
        static int32 pickEntry( const vector<float32>& listEntryWeight, float32 unit );
        /** @brief (씨앗, 번호, 칸) 을 섞은 32 비트 해시입니다. */
        static uint32 hashCandidate( uint32 seed, uint32 candidateIndex, uint32 lane );
        /** @brief 해시를 [0, 1) 실수로 바꿉니다(상위 24 비트). */
        static float32 toUnit( uint32 hash );

        /**
         * @brief 3D 배치 하나의 월드 행렬입니다 — 평면 (u, v) 를 (x, z) 로, 높이를 y 로 놓고 요를 돌린 뒤 노멀 쪽으로 `_alignToNormal` 만큼 기울입니다.
         * @param origin 평면 좌표의 원점(월드)입니다. 높이는 표면이 준 월드 높이 그대로라 원점의 y 를 더하지 않습니다.
         */
        static float4x4 makeWorldMatrix( const PlacementInstance& instance, const float3& origin );
    };
} // namespace sw
