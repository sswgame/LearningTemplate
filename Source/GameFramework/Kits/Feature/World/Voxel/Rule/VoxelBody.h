/**
 * @file VoxelBody.h
 * @brief 블록 월드를 걷는 몸입니다. 축 정렬 상자(AABB) 충돌, 중력, 점프, 물속 헤엄을 처리합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"

namespace sw
{
    class VoxelWorld;

    /** @brief 몸의 크기와 움직임 설정입니다. 마인크래프트의 값에 가깝게 잡았습니다. */
    struct VoxelBodySettings
    {
        float32 _halfWidth{ 0.3f };
        float32 _height{ 1.8f };
        float32 _eyeHeight{ 1.62f };
        float32 _gravity{ 28.0f };
        float32 _jumpSpeed{ 8.4f }; ///< 블록 하나(1.25 m)를 넘을 수 있는 속도이다
        float32 _walkSpeed{ 4.3f };
        float32 _sprintSpeed{ 5.6f };
        float32 _maxFallSpeed{ 50.0f };
        float32 _groundAcceleration{ 40.0f }; ///< 원하는 속도로 다가가는 가속도(m/s²)
        float32 _airAcceleration{ 10.0f };
        float32 _swimSpeed{ 3.0f };
        float32 _maxStep{ 1.0f / 120.0f }; ///< 한 걸음의 최대 시간이다. 빠르게 떨어져도 블록을 뚫지 않게 한다
    };
} // namespace sw

namespace sw
{
    /**
     * @class VoxelBody
     * @brief 발 가운데 위치(`_position`)를 가진 상자 몸입니다. 축마다 따로(Y, X, Z 순서로) 움직이고, 단단한 블록에 닿으면 그 축의 속도를 0으로 둡니다.
     * @details 축을 나눠 움직이면 벽에 비스듬히 부딪혀도 벽을 따라 미끄러집니다.
     *          한 걸음이 `_maxStep` 보다 길면 잘게 나눕니다. 그러면 몸이 한 걸음에 반 블록 넘게 움직이지 않으므로 얇은 블록을 뚫지 않습니다.
     *          물(단단하지 않은 블록 중 `water`)에서는 중력이 약해지고 점프가 헤엄이 됩니다.
     */
    class SW_GF_API VoxelBody
    {
    public:
        VoxelBody();

        void setSettings( const VoxelBodySettings& settings ) { _settings = settings; }
        void setPosition( const float3& position );
        /** @brief 물로 여길 블록입니다. 공기이면 헤엄을 치지 않습니다. */
        void setWaterBlock( VoxelBlockIndex waterBlock ) { _waterBlock = waterBlock; }

        /**
         * @brief @p deltaTime 만큼 움직입니다.
         * @param wishDirection 수평으로 가려는 방향입니다. 길이는 1 이하이고 Y 는 무시하며, 보통 시점의 앞쪽과 오른쪽을 조합합니다.
         * @param bJump true 이면 땅에서 점프하고, 물에서는 위로 헤엄칩니다.
         */
        void step( const VoxelWorld& world, const float3& wishDirection, bool bJump, bool bSprint, float32 deltaTime );

        /** @brief 이 몸이 블록 위치 @p coord 와 겹치면 true 입니다. 블록을 몸 안에 놓지 못하게 하려는 검사입니다. */
        bool overlapsBlock( const VoxelCoord& coord ) const;
        /** @brief 몸 위치 @p position 이 단단한 블록과 겹치면 true 입니다. */
        bool isBlockedAt( const VoxelWorld& world, const float3& position ) const;

        const float3&            getPosition() const { return _position; }
        const float3&            getVelocity() const { return _velocity; }
        float3                   getEyePosition() const { return float3{ _position._x, _position._y + _settings._eyeHeight, _position._z }; }
        bool                     isOnGround() const { return _bOnGround != SW_FALSE; }
        bool                     isInWater() const { return _bInWater != SW_FALSE; }
        const VoxelBodySettings& getSettings() const { return _settings; }

    private:
        void integrate( const VoxelWorld& world, const float3& wishDirection, bool bJump, bool bSprint, float32 deltaTime );
        /** @brief 한 축으로 @p delta 만큼 움직입니다. 막히면 그 위치에서 멈추고 true 를 반환합니다. */
        [[nodiscard]] bool moveAxis( const VoxelWorld& world, int32 axis, float32 delta );

        VoxelBodySettings _settings;
        float3            _position;
        float3            _velocity;
        VoxelBlockIndex   _waterBlock;
        uint8             _bOnGround;
        uint8             _bInWater;
    };
} // namespace sw
