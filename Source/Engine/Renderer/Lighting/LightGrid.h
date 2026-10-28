/**
 * @file LightGrid.h
 * @brief 뷰 하나의 클러스터 라이트 목록(라이트 그리드)을 CPU 에서 만듭니다. 언리얼 클러스터드 디퍼드의 라이트 그리드, 유니티 Forward+ 의 빛 목록에 해당합니다.
 * @details 클러스터는 뷰의 NDC 를 X `kDimX` 칸, Y `kDimY` 칸, 깊이 `kDimZ` 조각으로 나눈 것입니다. 원근은 클립 w 의 로그 조각, 직교는 NDC z 의 선형 조각입니다.
 *          결과는 32 비트 단어 배열이고 배치와 칸 수의 원본은 bindingslots.hlsli 10절(`SW_LIGHT_GRID_*`)입니다. 셰이더는 `lighting.hlsli` 의 커서(`LightCursor::begin`)로 읽습니다.
 *          방향광은 모든 클러스터에 닿으므로 목록 앞의 전역 구간에 한 번만 둡니다. 2D 라이트와 2D 그림자 세그먼트는 넣지 않습니다. 라이트 프로브 볼륨 · 반사 프로브는 점광처럼 경계 구(상자의 외접구)로 넣습니다 — 어느 타입을 경계 구로 넣는지는 `shaderslot::kLightGridLocalTypeMask` 하나가 정합니다.
 *          클러스터 범위는 보수적입니다. 라이트 경계 구에 닿는 클러스터는 빠지지 않고, 닿지 않는 클러스터가 조금 들어갈 수 있습니다.
 *          GPU 생산자(lightgrid.hlsl)가 같은 형식을 같은 범위 식으로 만듭니다 — 식을 고치면 두 곳(이 파일과 그 셰이더, 칸 식은 lightgridcommon.hlsli)을 함께 고칩니다.
 *          클러스터 안의 라이트 순서는 GPU 생산자에서 원자 연산이 정하므로 다를 수 있습니다(집합은 같습니다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    /**
     * @struct LightGridSlicing
     * @brief 뷰 하나의 깊이 조각 상수입니다. 그리드 헤더에 실리고, GPU 생산자에게는 뷰 정보로 넘어갑니다.
     */
    struct LightGridSlicing
    {
        float32 _nearDepth{ 1.0f };  ///< 원근의 가까운 면 클립 w(직교는 1)
        float32 _sliceScale{ 0.0f }; ///< 원근은 조각 수 / log2(먼 면 w / 가까운 면 w), 직교는 조각 수
        uint8   _bPerspective{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class LightGrid
     * @brief 뷰 하나의 라이트 그리드입니다. 렌더 스레드가 프레임마다 `populate` 로 다시 채우고, `GPULightBuffer` 가 그리드 버퍼(t12)에 이어 붙여 올립니다.
     */
    class SW_API LightGrid
    {
    public:
        /// @brief 클러스터 칸 수입니다. 원본은 bindingslots.hlsli 10절입니다.
        static constexpr uint32 kDimX         = shaderslot::lightgrid::kDimX;
        static constexpr uint32 kDimY         = shaderslot::lightgrid::kDimY;
        static constexpr uint32 kDimZ         = shaderslot::lightgrid::kDimZ;
        static constexpr uint32 kClusterCount = shaderslot::lightgrid::kClusterCount;

        /**
         * @brief 이 뷰-투영의 깊이 조각 상수를 구합니다. CPU 생산자(`populate`)와 GPU 생산자의 뷰 정보가 같은 값을 씁니다.
         * @return 원근 투영의 가까운 면 · 먼 면을 구하지 못하면(무한 · 뒤집힌 깊이) 거짓입니다. 그 뷰는 그리드를 만들지 않습니다.
         */
        [[nodiscard]] static bool computeSlicing( const float4x4& viewProj, LightGridSlicing& outSlicing );

        /**
         * @brief 이 뷰와 라이트 목록으로 그리드를 다시 채웁니다.
         * @param viewProj   셰이더의 `g_ViewProj` 와 같은 행렬이어야 합니다(클러스터를 같은 식으로 구합니다).
         * @param lightCount 셰이더의 `g_SwLightCount` 와 같은 수입니다. 그리드의 라이트 번호는 이 수보다 작습니다.
         * @return `computeSlicing` 이 실패하면 거짓이고 그리드는 비어 있습니다. 부르는 쪽이 오류를 남기고 그 뷰는 그리드 없이 그립니다.
         */
        [[nodiscard]] bool populate( const float4x4& viewProj, const GPULight* pLight, uint32 lightCount );

        /** @brief 그리드 단어입니다. 비었으면 그리드가 없습니다. */
        const vector<uint32>& getWords() const { return _listWord; }

        /** @brief 이 월드 위치가 든 클러스터 번호입니다. 셰이더 `LightGrid::computeCluster` 와 같은 식입니다. 그리드가 비었으면 0 입니다. */
        uint32 computeClusterIndex( const float3& worldPosition ) const;
        /** @brief 셰이더 커서와 같은 순서(전역 구간, 클러스터 목록)로 이 위치를 비추는 라이트 번호를 모읍니다. 테스트와 디버그 보기용입니다. */
        void collectLights( const float3& worldPosition, vector<uint32>& outListLightIndex ) const;
        /** @brief 전역 구간(방향광)의 라이트 수입니다. */
        uint32 getGlobalLightCount() const;
        /** @brief 클러스터 하나의 라이트 수입니다(전역 구간 제외). */
        uint32 getClusterLightCount( uint32 clusterIndex ) const;

    private:
        /** @brief 라이트 하나가 덮는 클러스터 범위입니다(양 끝 포함). */
        struct ClusterRange
        {
            uint32 _lightIndex{ 0 };
            uint32 _minX{ 0 };
            uint32 _maxX{ 0 };
            uint32 _minY{ 0 };
            uint32 _maxY{ 0 };
            uint32 _minZ{ 0 };
            uint32 _maxZ{ 0 };
        };

        /** @brief 깊이 값(원근은 클립 w, 직교는 NDC z)을 조각 위치(실수)로 바꿉니다. 셰이더 `LightGrid::computeSlicePosition` 과 같은 식입니다. */
        float32 computeSlicePosition( float32 depth ) const;
        /** @brief 점광 · 스폿의 경계 구가 덮는 클러스터 범위를 구합니다. 화면 밖이거나 깊이 범위 밖이면 거짓입니다. 컴퓨트 lightgrid.hlsl `computeLightRange` 와 같은 식입니다. */
        bool computeLightRange( const float4x4& viewProj, const GPULight& light, ClusterRange& outRange ) const;

        vector<uint32>       _listWord;
        vector<ClusterRange> _listRangeScratch;   ///< 라이트마다의 범위(프레임마다 다시 쓴다)
        vector<uint32>       _listClusterScratch; ///< 클러스터마다의 개수, 이어서 채우기 커서
        vector<uint32>       _listGlobalScratch;  ///< 전역 구간의 라이트 번호
        float4x4             _viewProj{};
        LightGridSlicing     _slicing{};
    };
} // namespace sw
