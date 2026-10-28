/**
 * @file LatticeModifier.h
 * @brief Sederberg-Parry 자유 형태 변형(FFD) 모디파이어입니다. 패널 · 시각화가 제어점을 다루려고 헤더를 둡니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"

namespace sw::editor
{
    /**
     * @class LatticeModifier
     * @brief 격자 상자 안의 정규 좌표 [0,1]^3 를 제어점의 3중 베른슈타인 다항식(차수 = 해상도 - 1)으로 옮깁니다.
     * @details 제어점은 정규 좌표로 둔다(항등 격자에서 점 (i,j,k) 는 (i/(Nx-1), j/(Ny-1), k/(Nz-1))). 베른슈타인 기저는 선형 정밀도가 있어
     *          항등 격자는 상자 밖 점까지 모두 그대로 둔다. 상자를 다시 맞추면 변형 모양이 상자 크기를 따라 늘어난다.
     *          격자 상자는 모디파이어 공간이다. 상자가 무효면 입력 정점의 상자를 쓴다(`Use Mesh Bounds`).
     *          해상도는 매개변수(Int)이고, 바뀌면 `onParamsChanged` 가 제어점을 옮겨 담는다.
     */
    class LatticeModifier : public MeshModifier
    {
    public:
        static constexpr int32 kMinResolution = 2;
        static constexpr int32 kMaxResolution = 8;

        explicit LatticeModifier( int32 resolution );
        LatticeModifier();

        /** @brief 격자 모디파이어(레지스트리 id `lattice` · `ffd2` · `ffd3` · `ffd4`)면 그 포인터, 아니면 nullptr 입니다. RTTI 없이 id 로 가린다. */
        static LatticeModifier* findFrom( MeshModifier& modifier );

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override;
        bool   supportsGPU() const override { return true; }
        /** @brief 3D 격자 표본 버퍼를 싣는다: float4(격자 수 x · y · z, 1) · 쓰는 상자 최소 · 최대(저장된 상자, 무효면 입력 정점 상자), 뒤에 제어점(정규 좌표, x 가 가장 빨리 도는 순서)마다 float4 하나. */
        bool               packGPUInputs( const MeshModifierContext& context, vector<float4>& outListValue ) const override;
        void               writeExtraJSON( const JSONValue& object ) const override;
        [[nodiscard]] bool readExtraJSON( const JSONValue& object ) override;
        void               onParamsChanged() override;

        /**
         * @brief 해상도를 바꿉니다. 모든 축이 같거나 커지면 차수 올리기로 변형을 정확히 보존하고, 하나라도 줄면 옛 변형을 새 격자점에서 다시 표본합니다.
         * @details 각 축은 [`kMinResolution`, `kMaxResolution`] 로 자른다. 매개변수 값도 맞춘다.
         */
        void setResolution( int32 countX, int32 countY, int32 countZ );
        /** @brief 모든 제어점을 항등 격자로 되돌립니다. */
        void resetToIdentity();
        /** @brief 정규 좌표 하나를 변형합니다(상자 밖도 다항식 그대로 이어집니다). */
        float3 deformNormalized( const float3& normalizedPoint ) const;
        /** @brief 제어점 하나를 정규 좌표 @p delta 만큼 옮깁니다. 범위 밖 번호는 무시합니다. */
        void moveControlPoint( int32 index, const float3& delta );
        /**
         * @brief 고른 제어점들을 옮깁니다. @p bMirror 면 @p mirrorAxisIndex 축으로 마주 보는 점을 그 성분만 뒤집은 delta 로 함께 옮깁니다.
         * @details 마주 보는 점이 고른 목록에 이미 있거나 자기 자신(가운데 줄)이면 두 번 옮기지 않는다.
         */
        void moveControlPoints( const vector<int32>& listIndex, const float3& delta, bool bMirror, uint32 mirrorAxisIndex );

        int32         getControlPointIndex( int32 indexX, int32 indexY, int32 indexZ ) const;
        int32         getResolutionX() const { return _countX; }
        int32         getResolutionY() const { return _countY; }
        int32         getResolutionZ() const { return _countZ; }
        int32         getControlPointCount() const { return static_cast<int32>( _listControlPoint.size() ); }
        const float3& getControlPoint( int32 index ) const { return _listControlPoint[static_cast<size_t>( index )]; }
        void          setControlPoint( int32 index, const float3& normalizedPosition );
        /** @brief 항등 격자에서 이 제어점이 있을 정규 좌표입니다. */
        float3 getIdentityControlPoint( int32 index ) const;
        /** @brief 쓰는 격자 상자입니다. 저장된 상자가 무효면 @p fallback 입니다. */
        MeshModifierBounds        getEffectiveBox( const MeshModifierBounds& fallback ) const;
        const MeshModifierBounds& getBox() const { return _box; }
        void                      setBox( const MeshModifierBounds& box ) { _box = box; }

    protected:
        void onFitToBounds( const MeshModifierBounds& modifierBounds ) override;

    private:
        void elevateAxis( uint32 axisIndex );
        void syncResolutionParams();

        MeshModifierBounds _box;              ///< 모디파이어 공간의 격자 상자. 무효면 입력 정점의 상자
        vector<float3>     _listControlPoint; ///< X 가 가장 빨리 도는 순서(index = x + Nx · (y + Ny · z))
        uint32             _resolutionXParam;
        uint32             _resolutionYParam;
        uint32             _resolutionZParam;
        int32              _countX;
        int32              _countY;
        int32              _countZ;
    };
} // namespace sw::editor
