/**
 * @file PrefabViewBatch.h
 * @brief 규칙 객체의 목록을 프리팹 인스턴스로 보이는 묶음입니다 — 프레임마다 `begin` → `show` … → `end` 로 지금 보일 자리를 적으면 그만큼만 켜 둡니다.
 * @details 신원이 없는 모습(트랙 조각 · 노트 · 카드 · 병사 표식)용이다 — N 번째로 적은 것은 N 번째 인스턴스가 보인다. 신원과 되돌리기가 필요한 모습은 `PrefabPool` · `SimulationViewSync` 를 쓴다.
 *          자리는 절대(`AbsolutePosition`)로 받아 원점 기준으로 옮긴다. 인스턴스는 파생 결과라 저장하지 않는다(`GameObject::setTransient`) — 상태를 되살린 씬에서는 다음 `show` 가 다시 세운다.
 *          프리팹의 첫 메시가 자리 · 크기 · 색을 받는다. 자리 · 크기는 프리팹이 적은 값에 더하고 곱한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Appearance/MaterialTintCache.h"

namespace sw
{
    class GameObjectManager;
    class PrefabAsset;
    struct AbsolutePosition;

    /**
     * @class PrefabViewBatch
     * @brief 프리팹 하나의 인스턴스 묶음입니다. 게임 스레드(틱 뒤)에서만 씁니다.
     */
    class SW_GF_API PrefabViewBatch
    {
    public:
        PrefabViewBatch();

        /** @brief 이번 프레임에 보일 것을 처음부터 적습니다. */
        void begin() { _shownCount = 0; }
        /**
         * @brief 하나를 @p position(절대 자리)에 @p scale(프리팹 크기의 배율) · @p yaw(라디안) · @p tint 로 보입니다. 인스턴스가 모자라면 @p prefab 을 스폰합니다.
         * @return 프리팹을 읽지 못했거나 메시가 없으면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool show( GameObjectManager& manager, const AssetRef<PrefabAsset>& prefab, const AbsolutePosition& position, const float3& scale, float32 yaw, const float4& tint );
        /**
         * @brief 하나를 바닥 자리 @p foot(절대 자리) 위에 세웁니다 — 한 변 1 m 이고 가운데가 원점인 프리팹을 @p size(m) 로 늘리고 그 높이의 절반만큼 올립니다.
         * @details 세우지 못하는 까닭은 `show` 가 한 번 알린다.
         */
        void showStanding( GameObjectManager& manager, const AssetRef<PrefabAsset>& prefab, const AbsolutePosition& foot, const float3& size, const float4& tint );
        /** @brief 적지 않은 나머지 인스턴스를 끕니다. */
        void end( GameObjectManager& manager );
        /** @brief 세운 인스턴스를 모두 지웁니다(플레이 끝). */
        void destroyAll( GameObjectManager& manager );

        /** @brief 이번 프레임에 적은 수입니다. */
        uint32 getShownCount() const { return _shownCount; }

    private:
        /** @brief 인스턴스 하나입니다. */
        struct Slot
        {
            GameObjectHandle _object{};
            float3           _prefabOffset{};                  ///< 프리팹이 적은 메시 자리(바닥에 앉히는 높이 따위 — 보일 자리에 더한다)
            float3           _prefabScale{ 1.0f, 1.0f, 1.0f }; ///< 프리팹이 적은 메시 크기
        };

        vector<Slot>      _listSlot;
        MaterialTintCache _tintCache;
        uint32            _shownCount;
        uint8             _bRefused; ///< 이 프리팹은 세우지 못했다 — 프레임마다 다시 해 보지 않는다
    };
} // namespace sw
