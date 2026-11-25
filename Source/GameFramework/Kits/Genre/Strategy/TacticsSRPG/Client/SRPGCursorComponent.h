/**
 * @file SRPGCursorComponent.h
 * @brief 택틱스 SRPG 키트의 클라이언트 쪽 — 입력 맵 액션으로 칸 커서를 옮기고 디렉터(`SRPGGameModeComponent`)에 명령을 내며, 전장을 프리팹 인스턴스로 보입니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_TacticsSRPG`, 전용 서버는 짓지 않는다). 디렉터의 명령(`confirmCell` · `endPlayerPhase`)과 읽기 API(`getField` 등)만 쓰고
 *          전장(규칙)을 직접 고치지 않는다. 디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다.
 *          명령과 그리기는 틱 뒤 게임 스레드에서 한다(디렉터 규칙도 틱 뒤에 돈다 — `GameModeComponent::kTickReadSafe`).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class DebugHUDSectionWriter;
    class GameObjectManager;
    class SRPGGameModeComponent;

    /**
     * @class SRPGCursorComponent
     * @brief `Move` 축을 한 번 기울일 때 커서를 한 칸 옮기고, 확정 액션은 커서 칸 확정(`confirmCell`), 차례 끝 액션은 아군 페이즈 끝(`endPlayerPhase`)으로 디렉터에 넘깁니다.
     */
    REFLECT( Category = "TacticsSRPG", DisplayName = "SRPG Cursor", Tooltip = "Client side of a TacticsSRPG battle: moves the cell cursor from input, sends commands to the SRPG Game Mode on the same object and draws the field" )
    class SW_KIT_CLIENT_API SRPGCursorComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        SRPGCursorComponent();
        virtual ~SRPGCursorComponent() override;

        void onBeginPlay() override;
        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getCursorX() const { return _cursor._x; }
        int32 getCursorY() const { return _cursor._y; }

        /** @brief 디렉터 전장 유닛의 지금 레벨 · 경험치와 최근 성장 기록을 씁니다(HUD 섹션 `tacticsgrowth` — 성장 디버거). 디렉터가 없으면 그 줄 하나입니다. */
        void writeGrowthDebugLines( DebugHUDSectionWriter& writer, const GameObjectManager& manager ) const;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 읽어 커서 · 명령을 내고 전장을 그립니다. */
        void updateCursor( GameObjectManager& manager, SRPGGameModeComponent& director );
        void submitInput( const GameModeInput& input, SRPGGameModeComponent& director );
        /** @brief `Move` 축을 한 번 기울일 때 커서를 한 칸 옮깁니다(놓았다가 다시 기울여야 다음 칸). */
        void moveCursor( const GameModeInput& input, const SRPGGameModeComponent& director );
        /** @brief 전장과 커서를 보입니다. 칸 (x, y) 는 절대 자리 (x, 0, y) 다. */
        void showField( const SRPGGameModeComponent& director, GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that steps the cursor one cell per tilt (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Confirm Action", Tooltip = "InputMap action that confirms the cursor cell: select, wait, move or attack (required)" )
        hashed_string _confirmAction;
        PROPERTY( Required, Category = "Input", DisplayName = "End Turn Action", Tooltip = "InputMap action that ends the player phase (required)" )
        hashed_string _endTurnAction;
        PROPERTY( Category = "View", DisplayName = "Cube Prefab", Required, Tooltip = "1 m cube stretched into the cells and the cursor" )
        AssetRef<PrefabAsset> _cubePrefab;
        PROPERTY( Category = "View", DisplayName = "Sphere Prefab", Required, Tooltip = "1 m diameter sphere scaled to a unit" )
        AssetRef<PrefabAsset> _spherePrefab;
        PROPERTY( Category = "View", DisplayName = "Tile Half Extent", Tooltip = "Half size of one drawn cell; its top is the floor", Units = m )
        float3 _tileHalfExtent;
        PROPERTY( Category = "View", DisplayName = "Tile Color" )
        float4 _tileColor;
        PROPERTY( Category = "View", DisplayName = "Unit Radius", Min = 0.01, Units = m )
        float32 _unitRadius;
        PROPERTY( Category = "View", DisplayName = "Unit Height", Tooltip = "Height of a unit's center above the floor", Units = m )
        float32 _unitHeight;
        PROPERTY( Category = "View", DisplayName = "Player Color" )
        float4 _playerColor;
        PROPERTY( Category = "View", DisplayName = "Selected Color" )
        float4 _selectedColor;
        PROPERTY( Category = "View", DisplayName = "Enemy Color" )
        float4 _enemyColor;
        PROPERTY( Category = "View", DisplayName = "Cursor Half Extent", Units = m )
        float3 _cursorHalfExtent;
        PROPERTY( Category = "View", DisplayName = "Cursor Height", Units = m )
        float32 _cursorHeight;
        PROPERTY( Category = "View", DisplayName = "Cursor Color" )
        float4 _cursorColor;

        SW_TRANSIENT PrefabViewBatch _cubeViews;
        SW_TRANSIENT PrefabViewBatch _unitViews;
        SW_TRANSIENT int2            _cursor;
        int2                         _lastStep;          ///< 지난 프레임의 축 방향(-1 · 0 · 1) — 놓아야 다음 칸을 옮긴다
        SW_TRANSIENT uint8           _bCursorPlaced : 1; ///< 판이 열린 뒤 커서를 첫 아군 칸에 놓았다
        [[maybe_unused]] uint8       _reserved      : 7;
    };
} // namespace sw
