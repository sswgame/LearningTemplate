/**
 * @file BlendSpace.h
 * @brief 파라미터 하나나 둘로 여러 클립의 포즈를 섞는 포즈 블렌드 스페이스입니다(언리얼 Blend Space, 유니티 Blend Tree 에 해당합니다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

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
} // namespace sw

namespace sw
{
    /**
     * @class BlendSpace
     * @brief 표본 목록과 축 수(1 또는 2)를 가지고, 파라미터에서 표본마다의 가중치를 구합니다. 포즈는 표본 클립을 샘플한 쪽이 가중치로 섞습니다(`Pose::blendWeighted`).
     * @details - **1D**: 표본을 x 순으로 두고, 파라미터를 끼운 이웃 두 표본을 선형으로 섞는다. 양 끝 바깥은 끝 표본 하나다.
     *          - **2D**: 파라미터를 표본 범위로 먼저 가두고(언리얼 블렌드 스페이스와 같다), 역거리 가중치(IDW, `1 / d²`)로 섞는다.
     *            가중치는 가장 가까운 표본의 거리로 나눠 계산하므로 단위(cm/s 와 m/s)가 달라도 합이 작아 버려지는 일이 없다.
     *          - **동기화**: 표본 클립들은 같은 위상(정규화 시간)으로 샘플한다. 한 바퀴 길이는 가중치로 평균한 표본 길이다(`computeBlendedLength`).
     *            그래서 걷기와 달리기를 섞어도 발이 엇갈리지 않는다.
     *          표본 수에는 상한이 없다. 가중치를 고정 길이 배열에 담지 않기 때문이다.
     */
    class SW_API BlendSpace
    {
    public:
        BlendSpace();

        /** @brief 축 수(1 또는 2)를 정합니다. 그 밖의 값은 1 로 봅니다. 1D 정렬은 `addSample` 이 하므로 표본을 더하기 전에 정합니다. */
        void setAxisCount( uint32 axisCount ) { _axisCount = ( axisCount == 2 ) ? 2u : 1u; }
        /** @brief 축 수입니다. */
        uint32 getAxisCount() const { return _axisCount; }
        /** @brief 표본을 더합니다. 1D 면 목록을 x 오름차순으로 유지합니다. */
        void addSample( const float2& position, const hashed_string& clipName );
        /** @brief 표본을 모두 지웁니다. */
        void clear() { _listSample.clear(); }
        /** @brief 표본 개수입니다. */
        uint32 getSampleCount() const { return static_cast<uint32>( _listSample.size() ); }
        /** @brief 표본 목록입니다. 1D 면 x 오름차순입니다. */
        const vector<BlendSpaceSample>& getSamples() const { return _listSample; }

        /**
         * @brief 파라미터에서 표본마다의 가중치를 구해 @p outListWeight 에 씁니다(표본 순서, 합은 1). 표본이 없으면 비웁니다.
         * @details 가중치가 0 인 표본은 샘플하지 않아도 된다. 1D 는 많아야 둘, 2D 는 표본 위에 있으면 하나만 0 이 아니다.
         */
        void computeSampleWeights( const float2& parameter, vector<float32>& outListWeight ) const;
        /** @brief 표본 길이(초)를 가중치로 평균한 한 바퀴 길이입니다. 두 목록의 길이가 다르면 짧은 쪽까지 봅니다. */
        static float32 computeBlendedLength( const vector<float32>& listSampleLength, const vector<float32>& listWeight );

    private:
        vector<BlendSpaceSample> _listSample;
        uint32                   _axisCount;
    };
} // namespace sw
