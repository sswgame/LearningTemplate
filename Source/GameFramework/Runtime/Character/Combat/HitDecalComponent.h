/**
 * @file HitDecalComponent.h
 * @brief 맞으면 뒤 벽이나 발밑 땅에 데칼(피 · 흙탕 · 기름)을 튀깁니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

namespace sw
{
    struct CharacterRayHit;
    struct HitInfo;

    class GameObjectManager;

    /**
     * @class HitDecalComponent
     * @brief 맞음 사건(`GameObjectEvents::_hitReceived`)마다 때린 방향 뒤의 면에, 없으면 맞은 자리 아래의 면에 데칼 프리팹을 하나 세웁니다.
     * @details 무엇이 튀는지(색 · 텍스처 · 수명 · 비 씻김)는 프리팹의 `DecalComponent` 가 정한다 — 피는 비에 씻기고(`_rainWashPerSecond`) 시간이 지나면 흐려진다. 언리얼 `SpawnDecalAtLocation` 을
     *          피격 이벤트에 거는 자리다. 움직이는 것(동적 · 키네마틱 강체, 캐릭터) 위에는 세우지 않는다(데칼은 투영이라 미끄러진다). 같은 맞음의 치명 사본(`_bFatal` — 체력 원천이 다시 낸다)은
     *          세지 않는다. 동시에 남는 수는 `_maxDecals` 이고 넘으면 가장 오래된 것부터 지운다. 전용 서버는 그리지 않으므로 세우지 않는다.
     */
    REFLECT( Category = "Combat", DisplayName = "Hit Decal", Tooltip = "Spawns a decal (blood) on the surface behind or below each received hit" )
    class SW_GF_API HitDecalComponent : public Component
    {
    public:
        REFLECT_BODY();

        HitDecalComponent();
        ~HitDecalComponent() override = default;

        /** @brief 맞음 사건을 구독하고 난수를 씨앗에서 다시 시작합니다. */
        void onRegister( GameObjectManager& manager ) override;

        /** @brief 지금까지 세운 데칼 수(지워진 것 포함)입니다. */
        uint32 getSpawnedCount() const { return _spawnedCount; }
        /**
         * @brief 맞음 @p hit 의 데칼이 놓일 면을 찾습니다 — 때린 방향으로 @p reach 안, 없으면 아래로 @p reach 안입니다. 3D 물리만 봅니다.
         * @return 움직이지 않는 면을 찾았으면 true 입니다.
         */
        [[nodiscard]] static bool findSurface( const GameObjectManager& manager, const HitInfo& hit, float32 reach, uint64 ignoreObjectID, CharacterRayHit& outSurface );
        /** @brief 면 위 데칼 상자의 월드 행렬입니다 — 위(+Y)가 법선이고 법선 둘레로 @p angle 만큼 돌며, 가로 · 세로 @p size, 깊이 @p depth 입니다. */
        static float4x4 computeDecalWorld( const float3& point, const float3& normal, float32 angle, float32 size, float32 depth );

    private:
        void handleHit( const HitInfo& hit );

        PROPERTY( Required, Category = "Decal", DisplayName = "Decal Prefab", Tooltip = "Prefab whose primary component is a Decal (texture, colour, lifetime, rain wash)" )
        AssetRef<PrefabAsset> _decalPrefab;
        PROPERTY( Category = "Decal", DisplayName = "Reach", Min = 0.0, Units = m, Tooltip = "How far behind (then below) the hit a surface is searched" )
        float32 _reach;
        PROPERTY( Category = "Decal", DisplayName = "Size Min", Min = 0.01, Units = m, Tooltip = "Smallest decal side" )
        float32 _sizeMin;
        PROPERTY( Category = "Decal", DisplayName = "Size Max", Min = 0.01, Units = m, Tooltip = "Largest decal side" )
        float32 _sizeMax;
        PROPERTY( Category = "Decal", DisplayName = "Depth", Min = 0.01, Units = m, Tooltip = "Projection depth of the decal box" )
        float32 _depth;
        PROPERTY( Category = "Decal", DisplayName = "Min Damage", Min = 0.0, Tooltip = "Hits below this damage (after the zone multiplier) leave no decal" )
        float32 _minDamage;
        PROPERTY( Category = "Decal", DisplayName = "Max Decals", Min = 1, Tooltip = "Decals kept at once; the oldest is removed beyond this" )
        uint32 _maxDecals;
        PROPERTY( Category = "Decal", DisplayName = "Seed", Min = 1, Tooltip = "Seed of the size and angle sequence" )
        uint32 _seed;

        GameRandom               _random;
        vector<GameObjectHandle> _listDecal; ///< 세운 데칼(오래된 것부터 — 스스로 사라진 것은 다음에 세울 때 뺀다)
        uint32                   _spawnedCount;
        Subscription             _hitSubscription; ///< 맞음 사건 구독(`handleHit`)
    };
} // namespace sw
