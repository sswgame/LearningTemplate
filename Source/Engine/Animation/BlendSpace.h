/**
 * @file BlendSpace.h
 * @brief 1D · 2D 파라미터로 포즈를 합성하는 Blend Space.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    class Skeleton;

    /**
     * @brief 1D 파라미터 모션 샘플 노드
     */
    struct BlendSample1D
    {
        float32  _parameter{ 0.0f };          /**< 이 표본이 놓인 파라미터 값입니다. */
        string   _clipName;                   /**< 표본이 가리키는 클립 이름입니다. */
        float4x4 _pose{ float4x4::Identity }; /**< 표본의 포즈 행렬입니다. */
    };

    /**
     * @brief 2D 파라미터 모션 샘플 노드
     */
    struct BlendSample2D
    {
        float2   _parameter{ 0.0f, 0.0f };    /**< 이 표본이 놓인 (x, y) 파라미터입니다. */
        string   _clipName;                   /**< 표본이 가리키는 클립 이름입니다. */
        float4x4 _pose{ float4x4::Identity }; /**< 표본의 포즈 행렬입니다. */
    };

    /**
     * @brief 1차원 파라미터(예: Speed 0~10)에 따라 Idle -> Walk -> Run 포즈를 보간하는 1D Blend Space
     */
    class SW_API BlendSpace1D
    {
    public:
        BlendSpace1D()                                     = default;
        ~BlendSpace1D()                                    = default;
        BlendSpace1D( const BlendSpace1D& )                = default;
        BlendSpace1D& operator=( const BlendSpace1D& )     = default;
        BlendSpace1D( BlendSpace1D&& ) noexcept            = default;
        BlendSpace1D& operator=( BlendSpace1D&& ) noexcept = default;

        /** @brief 표본을 더합니다. 목록은 파라미터 오름차순으로 유지됩니다. */
        void addSample( float32 parameter, string_view clipName, const float4x4& pose );
        /**
         * @brief 파라미터에 해당하는 포즈를 합성합니다.
         * @details 양 끝 바깥에서는 끝 표본을 그대로 돌려주고, 사이에서는 이웃한 두 표본을
         *          DLB(회전·이동) + 선형 보간(스케일)으로 섞습니다.
         */
        float4x4 evaluate( float32 parameter ) const;
        /** @brief 합성한 포즈를 스켈레톤 루트 본에 얹고 캐릭터 공간 변환을 갱신합니다. */
        void evaluateSkeleton( float32 parameter, Skeleton& inoutSkeleton ) const;

        /** @brief 표본 개수입니다. */
        size_t getSampleCount() const { return _listSample.size(); }
        /** @brief 표본을 모두 지웁니다. */
        void clear() { _listSample.clear(); }

    private:
        vector<BlendSample1D> _listSample; /**< 파라미터 오름차순으로 정렬된 표본입니다. */
    };

    /**
     * @brief 2차원 파라미터(예: Direction, Speed)에 따라 다방향 보행 모션을 보간하는 2D Blend Space
     */
    class SW_API BlendSpace2D
    {
    public:
        BlendSpace2D()                                     = default;
        ~BlendSpace2D()                                    = default;
        BlendSpace2D( const BlendSpace2D& )                = default;
        BlendSpace2D& operator=( const BlendSpace2D& )     = default;
        BlendSpace2D( BlendSpace2D&& ) noexcept            = default;
        BlendSpace2D& operator=( BlendSpace2D&& ) noexcept = default;

        /** @brief 표본을 더합니다. 개수 제한은 없습니다. */
        void addSample( float32 paramX, float32 paramY, string_view clipName, const float4x4& pose );
        /**
         * @brief (x, y) 에 해당하는 포즈를 역거리 가중치(IDW)로 합성합니다.
         * @details 회전·이동은 DLB 로, 스케일은 같은 가중치의 선형 결합으로 섞습니다.
         */
        float4x4 evaluate( float32 paramX, float32 paramY ) const;

        /** @brief 표본 개수입니다. */
        size_t getSampleCount() const { return _listSample.size(); }
        /** @brief 표본을 모두 지웁니다. */
        void clear() { _listSample.clear(); }

    private:
        vector<BlendSample2D> _listSample; /**< 정렬하지 않은 표본 목록입니다. */
    };
} // namespace sw
