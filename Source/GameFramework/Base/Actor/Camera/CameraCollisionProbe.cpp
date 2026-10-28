#include "pch.h"

#include "GameFramework/Base/Actor/Camera/CameraCollisionProbe.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/ContinuousCollision.h"
#include "Engine/Physics/Component/ScenePhysics.h"
#include "Engine/Physics/PhysicsSettings.h"

namespace sw
{
    namespace
    {
        /** @brief 구를 상자 하나에 쓸어 닿는 거리(@p from 기준)를 @p inoutNearest 보다 가까우면 줄입니다. */
        void sweepAgainst( const float3& from, const float3& displacement, float32 length, float32 radius, const AABB& box,
                           float32& inoutNearest, bool& inoutHit )
        {
            SweepHit hit{};
            if ( ContinuousCollision::sweepSphere( from, radius, displacement, box, hit ) == false )
                return;
            const float32 distance = hit._hitFraction * length;
            if ( inoutHit == false || distance < inoutNearest )
            {
                inoutNearest = distance;
                inoutHit     = true;
            }
        }
    } // namespace
} // namespace sw

namespace sw
{
    CameraBoxCollisionProbe::CameraBoxCollisionProbe()
        : _listBox{}
    {
    }

    bool CameraBoxCollisionProbe::sweepSphere( const float3& from, const float3& to, float32 radius, float32& outDistance ) const
    {
        const float3  displacement = to - from;
        const float32 length       = displacement.getLength();
        float32       nearest      = length;
        bool          bHit         = false;
        if ( length <= MathUtil::kEpsilon )
            return false;
        for ( const AABB& box : _listBox )
        {
            sweepAgainst( from, displacement, length, radius, box, nearest, bHit );
        }
        outDistance = nearest;
        return bHit;
    }

    SceneCameraProbe::SceneCameraProbe( const GameObjectManager& manager, uint64 ignoredObjectID )
        : _manager{ manager }
        , _ignoredObjectID{ ignoredObjectID }
    {
    }

    bool SceneCameraProbe::sweepSphere( const float3& from, const float3& to, float32 radius, float32& outDistance ) const
    {
        const float3  displacement = to - from;
        const float32 length       = displacement.getLength();
        if ( length <= MathUtil::kEpsilon )
            return false;
        // 강체 — 세상의 막는 것(Default · Static)만. 래그돌 뼈 · 파편 · 캐릭터 캡슐에 암이 걸리지 않게 한다.
        uint32                 layerMask = MathUtil::kMaxUInt32;
        const PhysicsSettings* pSettings = _manager.getSubsystem<sw::ScenePhysics>().findSettings();
        uint8                  layer     = 0;
        if ( pSettings != nullptr )
        {
            layerMask = 0;
            if ( pSettings->findLayerIndex( hashed_string( "Default" ), layer ) )
                layerMask |= 1u << layer;
            if ( pSettings->findLayerIndex( hashed_string( "Static" ), layer ) )
                layerMask |= 1u << layer;
        }
        float32         nearest = length;
        bool            bHit    = false;
        CharacterRayHit hit;
        if ( CharacterHitUtil::sphereCast3D( _manager, from, displacement, length, radius, layerMask, _ignoredObjectID, hit ) )
        {
            nearest = hit._distance;
            bHit    = true;
        }
        // 2D 씬(2D 콜라이더 포함)은 XY 평면 — 암의 평면 성분으로 원을 쓸고, 닿은 비율을 3D 길이로 되돌린다. 암이 Z 로만 뻗으면 평면에서 걸리는 것이 없다.
        const float32 planarLength = float2{ displacement._x, displacement._y }.getLength();
        if ( planarLength > MathUtil::kEpsilon &&
             CharacterHitUtil::circleCast2D( _manager, from, displacement, planarLength, radius, layerMask, _ignoredObjectID, hit ) )
        {
            const float32 distance = hit._distance / planarLength * length;
            if ( bHit == false || distance < nearest )
            {
                nearest = distance;
                bHit    = true;
            }
        }
        outDistance = nearest;
        return bHit;
    }
} // namespace sw
