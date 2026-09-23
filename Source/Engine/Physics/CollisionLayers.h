/**
 * @file CollisionLayers.h
 * @brief 32 레이어 충돌 필터 행렬입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @class CollisionLayers
     * @brief 레이어 쌍마다 충돌을 허용할지 담는 대칭 행렬입니다(32 레이어).
     */
    class SW_API CollisionLayers
    {
    public:
        static constexpr uint32 kLayerCount = 32;

        /** @brief 모든 쌍을 허용하도록 초기화합니다. */
        CollisionLayers();

        /** @brief 레이어 a 와 b 의 충돌을 켜거나 끕니다(대칭). */
        void setLayerCollision( uint8 layerA, uint8 layerB, bool shouldCollide );

        /** @brief 두 레이어가 충돌해야 하는지 반환합니다. */
        bool shouldCollide( uint8 layerA, uint8 layerB ) const;

        /** @brief @p layer 와 충돌하는 레이어 비트마스크를 반환합니다. */
        uint32 getLayerMask( uint8 layer ) const;

        /** @brief 모든 쌍을 다시 허용으로 되돌립니다. */
        void resetDefaults();

    private:
        uint32 _arrMatrix[kLayerCount];
    };
} // namespace sw
