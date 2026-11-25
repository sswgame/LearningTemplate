/**
 * @file ArenaDeathSquashComponent.h
 * @brief 쓰러진 아레나 유닛의 모습 — 디렉터가 걷을 때까지 메시를 납작하게 누릅니다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class ArenaDeathSquashComponent
     * @brief 유닛 프리팹(메시 · `ArenaUnitComponent` 와 같은 오브젝트)의 클라이언트 컴포넌트입니다. 유닛의 쓰러짐 비율(`getDeathRemainRatio`)만큼 메시의 높이를 줄입니다.
     * @details 유닛과 같은 오브젝트라 같은 워커에서 그 뒤에 돈다. 줄이기 시작할 때의 크기를 들고 있다가 풀에 돌아가면 되돌린다.
     */
    REFLECT( Category = "AbilityArena", DisplayName = "Arena Death Squash", Tooltip = "Flattens the mesh of a fallen arena unit until the game mode removes it" )
    class SW_KIT_CLIENT_API ArenaDeathSquashComponent : public Component
    {
    public:
        REFLECT_BODY();

        ArenaDeathSquashComponent();
        virtual ~ArenaDeathSquashComponent() override = default;

        void onTick( TickContext& context ) override;
        /** @brief 풀 반환: 줄인 메시 크기를 되돌립니다. */
        [[nodiscard]] SpawnStateReset resetToSpawnState() override;

        /** @brief 메시 높이를 처음 크기의 @p remainRatio 배로 줄입니다(`_minSquashRatio` 아래로는 줄이지 않는다). */
        void squash( float32 remainRatio );

    private:
        PROPERTY( Category = "Look", DisplayName = "Min Squash Ratio", Tooltip = "Height the fallen body keeps at the end, as a share of its spawn height", Min = 0.0, Max = 1.0 )
        float32 _minSquashRatio;

        SW_TRANSIENT optional<float3> _spawnScale; ///< 줄이기 시작할 때의 크기. 줄이지 않았으면 비어 있다
    };
} // namespace sw
