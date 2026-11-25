/**
 * @file FarmPlotViewComponent.h
 * @brief 농장 생활 키트의 클라이언트 쪽 — 스스로 가꾸는 밭(`FarmPlotGameModeComponent`)의 칸을 프리팹 하나씩으로 세우고, 빨리 감기 입력을 디렉터 명령으로 넘깁니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_Farming`, 전용 서버는 짓지 않는다). 디렉터의 명령(`submitFastForward`)과 읽기 API(`getField` · `getFieldRevision`)만 쓰고 밭(규칙)을 직접 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. 명령과 스폰은 틱 뒤 게임 스레드에서 한다. `PostUpdate` 라 넘긴 입력은 다음 걸음이 쓰고 모습은 이번 걸음의 결과를 보인다.
 *          세운 모습은 파생이라 상태에 들지 않는다(`SpawnLifetime::Derived`).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Presentation/SimulationViewSync.h"
#include "GameFramework/Runtime/Gameplay/Appearance/MaterialTintCache.h"

namespace sw
{
    class FarmPlotGameModeComponent;
    class GameObjectManager;

    /**
     * @class FarmPlotViewComponent
     * @brief 밭 칸마다 판 하나를 세우고 칸 상태(풀 · 갈림 · 물 줌 · 다 익음)의 색을 입힙니다. 밭이 바뀐 때(`getFieldRevision`)에 모습이 바뀐 칸만 다시 세웁니다.
     */
    REFLECT( Category = "Farming", DisplayName = "Farm Plot View",
             Tooltip = "Client side of a farm plot: spawns the field tiles of the Farm Plot Game Mode on the same object and sends the fast forward input to it" )
    class SW_KIT_CLIENT_API FarmPlotViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        FarmPlotViewComponent();
        virtual ~FarmPlotViewComponent() override;

        /** @brief 세운 모습을 걷습니다. */
        void onEndPlay() override;

    private:
        /** @brief 세운 모습을 모두 지웁니다(플레이 끝 · 다시 세울 때). 모습은 파생이라 상태를 실을 때 걷지 않는다(`SpawnLifetime::Derived`). */
        void despawnViews();
        /** @brief 틱 뒤 게임 스레드: 빨리 감기 입력을 넘기고, 밭이 바뀌었으면 모습을 맞춥니다. */
        void updateView( GameObjectManager& manager, FarmPlotGameModeComponent& director );
        void submitInput( const GameModeInput& input, FarmPlotGameModeComponent& director );
        /** @brief 지금 밭의 칸과 세운 모습을 견줘 바뀐 칸만 다시 세웁니다. */
        void syncTiles( GameObjectManager& manager, const FarmPlotGameModeComponent& director );
        /** @brief 칸 하나를 @p center(절대 자리)에 세웁니다. 프리팹을 읽지 못하면 빈 핸들입니다. */
        GameObjectHandle spawnTile( GameObjectManager& manager, const float3& center, uint8 look );

    private:
        PROPERTY( Category = "Prefabs", DisplayName = "Tile Prefab", Tooltip = "One tile view (a mesh)" )
        AssetRef<PrefabAsset> _tilePrefab;
        PROPERTY( Required, Category = "Input", DisplayName = "Fast Forward Action", Tooltip = "InputMap action held to fast-forward the clock (required)" )
        hashed_string _fastForwardAction;
        PROPERTY( Category = "Look", DisplayName = "Ready Color", Tooltip = "Tile tint when the crop is fully grown" )
        float4 _readyColor;
        PROPERTY( Category = "Look", DisplayName = "Watered Color" )
        float4 _wateredColor;
        PROPERTY( Category = "Look", DisplayName = "Tilled Color" )
        float4 _tilledColor;
        PROPERTY( Category = "Look", DisplayName = "Grass Color", Tooltip = "Tile tint before it is tilled" )
        float4 _grassColor;
        PROPERTY( Category = "Look", DisplayName = "Tile Spacing", Tooltip = "Distance between field tiles", Min = 0.01, Units = m )
        float32 _tileSpacing;

        SW_TRANSIENT MaterialTintCache _tintCache;
        SimulationViewSync<uint8>      _tileViews;     ///< 칸 번호(y × 너비 + x) → 모습 번호(.cpp 의 TileLook, 0 은 칸 없음)
        uint32                         _shownRevision; ///< 맞춘 밭의 판(0 이면 아직 — 열린 디렉터의 판은 1 부터다)
    };
} // namespace sw
