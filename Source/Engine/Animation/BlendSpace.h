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
        float32  _parameter{ 0.0f };
        string   _clipName;
        float4x4 _pose{ float4x4::Identity };
    };

    /**
     * @brief 2D 파라미터 모션 샘플 노드
     */
    struct BlendSample2D
    {
        float2   _parameter{ 0.0f, 0.0f };
        string   _clipName;
        float4x4 _pose{ float4x4::Identity };
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

        void     addSample( float32 parameter, string_view clipName, const float4x4& pose );
        float4x4 evaluate( float32 parameter ) const;
        void     evaluateSkeleton( float32 parameter, Skeleton& inoutSkeleton ) const;

        size_t getSampleCount() const { return _listSample.size(); }
        void   clear() { _listSample.clear(); }

    private:
        vector<BlendSample1D> _listSample;
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

        void     addSample( float32 paramX, float32 paramY, string_view clipName, const float4x4& pose );
        float4x4 evaluate( float32 paramX, float32 paramY ) const;

        size_t getSampleCount() const { return _listSample.size(); }
        void   clear() { _listSample.clear(); }

    private:
        vector<BlendSample2D> _listSample;
    };
} // namespace sw
