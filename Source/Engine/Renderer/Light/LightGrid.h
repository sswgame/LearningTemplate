/**
 * @file LightGrid.h
 * @brief 뷰 하나의 클러스터 라이트 목록(라이트 그리드)을 CPU 에서 만듭니다. 언리얼 클러스터드 디퍼드의 라이트 그리드, 유니티 Forward+ 의 빛 목록에 해당합니다.
 * @details 클러스터는 뷰의 NDC 를 X `kDimX` 칸, Y `kDimY` 칸, 깊이 `kDimZ` 조각으로 나눈 것입니다. 원근은 클립 w 의 로그 조각, 직교는 NDC z 의 선형 조각입니다.
 *          결과는 32 비트 단어 배열이고 배치의 원본은 bindingslots.hlsli 10절(`SW_LIGHT_GRID_*`)입니다. 셰이더는 `lighting.hlsli` 의 커서(`swBeginLights`)로 읽습니다.
 *          방향광은 모든 클러스터에 닿으므로 목록 앞의 전역 구간에 한 번만 둡니다. 2D 라이트와 2D 그림자 세그먼트는 넣지 않습니다.
 *          클러스터 범위는 보수적입니다. 라이트 경계 구에 닿는 클러스터는 빠지지 않고, 닿지 않는 클러스터가 조금 들어갈 수 있습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/Shader/Binding/GPULight.h"

namespace sw
{
    /**
     * @class LightGrid
     * @brief 뷰 하나의 라이트 그리드입니다. 렌더 스레드가 프레임마다 `populate` 로 다시 채우고, `GPULightBuffer` 가 라이트 버퍼 꼬리에 붙여 올립니다.
     */
    class SW_API LightGrid
    {
    public:
        /// @brief 클러스터 칸 수입니다(결정 기록 5-12 D2-13 의 시작값). 측정 뒤에 조정합니다(ClusteredLighting.md S3).
        static constexpr uint32 kDimX         = 16;
        static constexpr uint32 kDimY         = 9;
        static constexpr uint32 kDimZ         = 24;
        static constexpr uint32 kClusterCount = kDimX * kDimY * kDimZ;

        /**
         * @brief 이 뷰와 라이트 목록으로 그리드를 다시 채웁니다.
         * @param viewProj   셰이더의 `g_ViewProj` 와 같은 행렬이어야 합니다(클러스터를 같은 식으로 구합니다).
         * @param lightCount 셰이더의 `g_SwLightCount` 와 같은 수입니다. 그리드의 라이트 번호는 이 수보다 작습니다.
         * @return 원근 투영의 가까운 면 · 먼 면을 구하지 못하면(무한 · 뒤집힌 깊이) 거짓이고 그리드는 비어 있습니다. 부르는 쪽이 오류를 남기고 그 뷰는 그리드 없이 그립니다.
         */
        [[nodiscard]] bool populate( const float4x4& viewProj, const GPULight* pLight, uint32 lightCount );

        /** @brief 그리드 단어입니다. 길이는 라이트 버퍼 원소(단어 16 개)의 배수로 채워져 있습니다. 비었으면 그리드가 없습니다. */
        const vector<uint32>& getWords() const { return _listWord; }
        /** @brief 단어가 차지하는 라이트 버퍼 원소 수입니다. */
        uint32 getElementCount() const;

        /** @brief 이 월드 위치가 든 클러스터 번호입니다. 셰이더 `swComputeLightGridCluster` 와 같은 식입니다. 그리드가 비었으면 0 입니다. */
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

        /** @brief 깊이 값(원근은 클립 w, 직교는 NDC z)을 조각 위치(실수)로 바꿉니다. 셰이더와 같은 식입니다. */
        float32 computeSlicePosition( float32 depth ) const;
        /** @brief 점광 · 스폿의 경계 구가 덮는 클러스터 범위를 구합니다. 화면 밖이거나 깊이 범위 밖이면 거짓입니다. */
        bool computeLightRange( const float4x4& viewProj, const GPULight& light, ClusterRange& outRange ) const;

        vector<uint32>       _listWord;
        vector<ClusterRange> _listRangeScratch;   ///< 라이트마다의 범위(프레임마다 다시 쓴다)
        vector<uint32>       _listClusterScratch; ///< 클러스터마다의 개수, 이어서 채우기 커서
        vector<uint32>       _listGlobalScratch;  ///< 전역 구간의 라이트 번호
        float4x4             _viewProj{};
        float32              _nearDepth{ 1.0f };
        float32              _sliceScale{ 0.0f };
        uint8                _bPerspective{ SW_FALSE };
    };
} // namespace sw
