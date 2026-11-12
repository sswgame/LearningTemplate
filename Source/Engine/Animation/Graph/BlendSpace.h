/**
 * @file BlendSpace.h
 * @brief 파라미터 하나나 둘로 여러 클립의 포즈를 섞는 포즈 블렌드 스페이스입니다(언리얼 Blend Space, 유니티 Blend Tree 에 해당합니다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Name/hashed_string.h"

namespace sw
{
    /**
     * @struct BlendSpaceSample
     * @brief 표본 하나입니다. 파라미터 공간의 위치와 그 위치에서 재생할 클립 이름을 가집니다.
     */
    struct BlendSpaceSample
    {
        float2        _position{ 0.0f, 0.0f }; /**< 파라미터 공간의 위치입니다. 1D 면 x 만 씁니다. */
        hashed_string _clipName;               /**< 이 표본이 재생할 클립(상태) 이름입니다. */
    };

    /**
     * @struct BlendSpaceTriangle
     * @brief 2D 표본 삼각분할의 삼각형 하나입니다. 꼭짓점은 표본 번호입니다.
     */
    struct BlendSpaceTriangle
    {
        uint32 _sampleA{ 0 }; /**< 첫 꼭짓점 표본 번호입니다. */
        uint32 _sampleB{ 0 }; /**< 둘째 꼭짓점 표본 번호입니다. */
        uint32 _sampleC{ 0 }; /**< 셋째 꼭짓점 표본 번호입니다. */
    };
} // namespace sw

namespace sw
{
    /**
     * @class BlendSpace
     * @brief 표본 목록 · 축 수(1 또는 2) · 축 범위를 가지고, 파라미터에서 표본마다의 가중치를 구합니다. 포즈는 표본 클립을 샘플한 쪽이 가중치로 섞습니다(`Pose::blendWeighted`).
     * @details - **축 범위**: 에셋이 축마다 최솟값 · 최댓값을 적는다(언리얼 BlendParameter Min/Max · Godot min_space/max_space). 파라미터는 이 범위로 가두고,
     *            삼각분할 · 겹침 판정은 이 범위로 정규화한 공간에서 한다 — 단위(cm/s 와 도)가 달라도 한 축이 지배하지 않는다.
     *          - **1D**: 표본을 x 순으로 두고, 파라미터를 끼운 이웃 두 표본을 선형으로 섞는다. 양 끝 바깥은 끝 표본 하나다.
     *          - **2D**: 표본 · 축 범위가 바뀔 때 들로네 삼각분할을 구워 두고, 파라미터를 품은 삼각형의 무게중심 좌표로 섞는다 — 0 이 아닌 표본은 많아야 셋이다.
     *            삼각분할 바깥(볼록 껍질 밖)이면 가장 가까운 바깥 변 위의 점으로 옮겨 그 변의 두 표본을 섞는다. 표본이 넓이를 이루지 못하면(셋 미만 · 한 줄) 삼각형이 없다(`hasArea`).
     *            삼각분할은 정규화 좌표를 정수 격자로 옮겨 정확한 정수 판정으로 굽는다 — 부동소수 허용치로 얇은 테두리 삼각형이 빠지지 않는다.
     *          - **동기화**: 표본 클립들은 같은 위상(정규화 시간)으로 샘플한다. 한 바퀴 길이는 가중치로 평균한 표본 길이다(`computeBlendedLength`).
     *            그래서 걷기와 달리기를 섞어도 발이 엇갈리지 않는다.
     *          표본 수에는 상한이 없다. 가중치를 고정 길이 배열에 담지 않기 때문이다.
     */
    class SW_API BlendSpace
    {
    public:
        BlendSpace();

        /** @brief 축 수(1 또는 2)를 정합니다. 그 밖의 값은 1 로 봅니다. 1D 정렬은 `addSample` 이 하므로 표본을 더하기 전에 정합니다. */
        void setAxisCount( uint32 axisCount );
        /** @brief 축 수입니다. */
        uint32 getAxisCount() const { return _axisCount; }
        /** @brief 축마다의 최솟값 · 최댓값을 정합니다(1D 면 y 는 보지 않습니다). 2D 면 삼각분할을 다시 굽습니다. */
        void setAxisRange( const float2& minimum, const float2& maximum );
        /** @brief 축 최솟값입니다. */
        const float2& getAxisMinimum() const { return _axisMinimum; }
        /** @brief 축 최댓값입니다. */
        const float2& getAxisMaximum() const { return _axisMaximum; }
        /** @brief 쓰는 축마다 최댓값이 최솟값보다 큰지입니다. 아니면 정규화할 수 없다(그래프 로드가 막는다). */
        bool hasValidAxisRange() const;
        /** @brief 표본을 더합니다. 1D 면 목록을 x 오름차순으로 유지하고, 2D 면 삼각분할을 다시 굽습니다. */
        void addSample( const float2& position, const hashed_string& clipName );
        /** @brief 표본과 삼각분할을 모두 지웁니다. */
        void clear();
        /** @brief 표본 개수입니다. */
        uint32 getSampleCount() const { return static_cast<uint32>( _listSample.size() ); }
        /** @brief 표본 목록입니다. 1D 면 x 오름차순입니다. */
        const vector<BlendSpaceSample>& getSamples() const { return _listSample; }
        /** @brief 2D 삼각분할입니다. 1D 이거나 표본이 넓이를 이루지 못하면 비어 있습니다. */
        const vector<BlendSpaceTriangle>& getTriangles() const { return _listTriangle; }
        /** @brief 2D 표본이 넓이를 이뤄 삼각형이 하나라도 있는지입니다. 표본이 둘 이상인 2D 는 이것이 참이어야 한다(그래프 로드가 막는다). */
        bool hasArea() const { return _listTriangle.empty() == false; }
        /** @brief 두 표본이 정규화 격자에서 같은 자리인지입니다(1D 는 x 만). 겹친 표본은 늘 가중치 0 이라 그래프 로드가 막는다. */
        bool hasCoincidentSamples() const;
        /** @brief 축 범위 밖의 표본이 있는지입니다(1D 는 x 만). */
        bool hasSampleOutsideAxisRange() const;

        /**
         * @brief 파라미터에서 표본마다의 가중치를 구해 @p outListWeight 에 씁니다(표본 순서, 합은 1). 표본이 없으면 비웁니다.
         * @details 가중치가 0 인 표본은 샘플하지 않아도 된다. 1D 는 많아야 둘, 2D 는 많아야 셋(표본 위면 하나, 바깥이면 둘)만 0 이 아니다.
         */
        void computeSampleWeights( const float2& parameter, vector<float32>& outListWeight ) const;
        /** @brief 표본 길이(초)를 가중치로 평균한 한 바퀴 길이입니다. 두 목록의 길이가 다르면 짧은 쪽까지 봅니다. */
        static float32 computeBlendedLength( const vector<float32>& listSampleLength, const vector<float32>& listWeight );
        /**
         * @brief LOD 로 섞는 가지를 접습니다. 가중치 = @p blendWeight × 원래 가중치 + (1 − @p blendWeight) × (가장 무거운 표본만 1)이라 합은 1 그대로입니다.
         * @details @p blendWeight 가 1 이면 그대로, 0 이면 가장 무거운 표본 하나만 남아 샘플 수가 하나가 된다. 같은 무게면 앞 표본이다(결정적).
         */
        static void foldToHeaviest( vector<float32>& inoutListWeight, float32 blendWeight );

    private:
        /** @brief 2D 면 들로네 삼각분할을 표본에서 다시 굽습니다. 1D 이거나 축 범위가 없으면 비웁니다. */
        void rebuildTriangulation();
        /** @brief 2D 가중치입니다. 파라미터를 품은 삼각형의 무게중심, 바깥이면 가장 가까운 변입니다. */
        void computeTriangleWeights( const float2& parameter, vector<float32>& outListWeight ) const;
        /** @brief 파라미터 공간의 점을 축 범위로 정규화한 [0, 1]² 공간으로 옮깁니다(가두지 않습니다). */
        float2 toNormalized( const float2& position ) const;

    private:
        vector<BlendSpaceSample>   _listSample;
        vector<BlendSpaceTriangle> _listTriangle; /**< 2D 들로네 삼각분할입니다. */
        float2                     _axisMinimum;  /**< 축 최솟값(에셋 "min_x" · "min_y")입니다. */
        float2                     _axisMaximum;  /**< 축 최댓값(에셋 "max_x" · "max_y")입니다. */
        uint32                     _axisCount;
    };
} // namespace sw
