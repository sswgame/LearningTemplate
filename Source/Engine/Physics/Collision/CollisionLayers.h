/**
 * @file CollisionLayers.h
 * @brief 바디 칸(레이어 = 충돌 프로필 번호) 32 개 쌍마다의 충돌 응답 표입니다 — 막음(Block) · 겹침(Overlap) · 무시(Ignore).
 * @details 언리얼 `ECollisionResponse` 와 같은 세 값이다. 표는 충돌 프로필(`PhysicsSettings` 의 `CollisionProfileDef`)이 서로의 오브젝트 채널에 준 응답 중
 *          약한 쪽(무시 < 겹침 < 막음)으로 채운다(`PhysicsSettings::makeCollisionLayers`). 백엔드는 무시가 아닌 쌍만 짝짓고(`shouldCollide` · `getLayerMask`),
 *          겹침 쌍은 밀어내지 않고 겹침 이벤트만 낸다(Jolt 센서 접촉 · Box2D 사용자 거름). 레이어 번호는 Jolt `ObjectLayer` 처럼 바디가 걸러지는 칸이다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 두 바디(또는 질의와 바디) 사이의 충돌 응답입니다(언리얼 `ECollisionResponse`). 값의 차례가 세기다 — 약한 쪽이 이긴다(`minResponse`). */
    ENUM()
    enum class CollisionResponse : uint8
    {
        Ignore = 0, ///< 서로 보지 않는다 — 막지도 겹침 이벤트도 없다
        Overlap,    ///< 막지 않고 겹침 이벤트만 낸다
        Block       ///< 막는다(접촉 이벤트)
    };

    /** @brief 두 응답 중 약한 쪽입니다(무시 < 겹침 < 막음). */
    inline CollisionResponse minResponse( CollisionResponse lhs, CollisionResponse rhs )
    {
        return static_cast<uint8>( lhs ) < static_cast<uint8>( rhs ) ? lhs : rhs;
    }

    /**
     * @class CollisionLayers
     * @brief 레이어 쌍마다 충돌 응답을 담는 대칭 표입니다(32 레이어, 파일 머리말).
     */
    class SW_API CollisionLayers
    {
    public:
        static constexpr uint32 kLayerCount = 32;

        /** @brief 모든 쌍을 막음으로 초기화합니다. */
        CollisionLayers();

        /** @brief 레이어 a 와 b 의 충돌을 막음(true) · 무시(false)로 둡니다(대칭). */
        void setLayerCollision( uint8 layerA, uint8 layerB, bool shouldCollide );
        /** @brief 레이어 a 와 b 의 응답을 둡니다(대칭). 범위 밖 레이어는 무시합니다. */
        void setResponse( uint8 layerA, uint8 layerB, CollisionResponse response );
        /** @brief 두 레이어의 응답입니다. 범위 밖 레이어는 무시입니다. */
        CollisionResponse getResponse( uint8 layerA, uint8 layerB ) const;

        /** @brief 두 레이어가 서로 보는지(무시가 아닌지) 반환합니다 — 백엔드가 짝을 지을지입니다. */
        bool shouldCollide( uint8 layerA, uint8 layerB ) const;

        /** @brief @p layer 와 서로 보는(무시가 아닌) 레이어 비트마스크를 반환합니다. */
        uint32 getLayerMask( uint8 layer ) const;
        /** @brief @p layer 와 겹침 응답인 레이어 비트마스크를 반환합니다(`getLayerMask` 의 부분집합). */
        uint32 getOverlapMask( uint8 layer ) const;

        /** @brief 모든 쌍을 다시 막음으로 되돌립니다. */
        void resetDefaults();

    private:
        uint32 _arrMatrix[kLayerCount];  ///< 무시가 아닌 쌍(막음 + 겹침)
        uint32 _arrOverlap[kLayerCount]; ///< 겹침 쌍 — 늘 `_arrMatrix` 의 부분집합
    };
} // namespace sw

namespace sw
{
    /** @brief AABB 가 겹치고 CollisionLayers 가 그 쌍을 허용하면 true 입니다. */
    inline bool queryOverlaps( const AABB& a, uint8 layerA, const AABB& b, uint8 layerB,
                               const CollisionLayers& layers )
    {
        if ( layers.shouldCollide( layerA, layerB ) == false )
            return false;
        return a.intersects( b );
    }
} // namespace sw
