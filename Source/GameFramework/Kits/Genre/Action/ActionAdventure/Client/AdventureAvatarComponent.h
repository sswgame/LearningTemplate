/**
 * @file AdventureAvatarComponent.h
 * @brief 액션 어드벤처 키트의 클라이언트 쪽 — 입력 맵 액션을 디렉터(`AdventureGameModeComponent`)의 입력 명령으로 넘기고, 지역 경계 · 봉화 · 야영지 · 플레이어를 프리팹 인스턴스로 보입니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_ActionAdventure`, 전용 서버는 짓지 않는다). 디렉터의 명령(`submitPlayerInput`)과 읽기 API 만 쓰고 규칙을 직접 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. 명령 · 그리기는 틱 뒤 게임 스레드에서 한다. 이 컴포넌트는 `PostUpdate` 라 디렉터 걸음 뒤에 돌아,
 *          넘긴 입력은 다음 걸음이 쓰고 그리기는 이번 걸음의 결과를 보인다(`ActionCombatAvatarComponent` 와 같은 한 프레임).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AdventureGameModeComponent;
    class GameObjectManager;
    struct AdventureRegionDef;

    /**
     * @class AdventureAvatarComponent
     * @brief `Move` 축 · 쓰기 액션을 프레임마다 디렉터 입력 명령으로 넘기고 판을 그립니다.
     */
    REFLECT( Category = "ActionAdventure", DisplayName = "Adventure Avatar",
             Tooltip = "Client side of an action adventure: sends input commands to the Adventure Game Mode on the same object and draws its regions, beacons, camps and player" )
    class SW_KIT_CLIENT_API AdventureAvatarComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        AdventureAvatarComponent();
        virtual ~AdventureAvatarComponent() override;

        /** @brief 같은 오브젝트의 디렉터를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 액션 이름이 비었으면 오류를 남깁니다(폴백 없음). 틱 그룹은 뷰와 같은 `PostUpdate` 입니다. */
        void onBeginPlay() override;
        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;
        /** @brief 입력 · 명령 · 그리기를 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 그립니다. */
        void updateAvatar( GameObjectManager& manager );
        /** @brief 판을 보입니다. 지역 표 · 플레이어 자리는 절대 자리다. */
        void showWorld( const AdventureGameModeComponent& director, GameObjectManager& manager );
        /** @brief 지역 경계 넷을 얇은 상자로 보입니다 — 씬에 땅 메시가 없는 지역(확장 팩)도 어디까지 걸을 수 있는지 보인다. */
        void showBorder( const AdventureRegionDef& region, GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that walks the player (required)" )
        hashed_string _moveAction;
        PROPERTY( Category = "Input", DisplayName = "Use Action", Tooltip = "InputMap action that lights a beacon or rests at a camp (required)" )
        hashed_string _useAction;
        PROPERTY( Category = "View", DisplayName = "Cube Prefab", Required, Tooltip = "1 m cube stretched into region borders and beacons" )
        AssetRef<PrefabAsset> _cubePrefab;
        PROPERTY( Category = "View", DisplayName = "Sphere Prefab", Required, Tooltip = "1 m diameter sphere scaled to camps and the player" )
        AssetRef<PrefabAsset> _spherePrefab;
        PROPERTY( Category = "View", DisplayName = "Border Color" )
        float4 _borderColor;
        PROPERTY( Category = "View", DisplayName = "Border Width", Min = 0.01, Units = m )
        float32 _borderWidth;
        PROPERTY( Category = "View", DisplayName = "Beacon Size", Tooltip = "Full size of a beacon box; it stands on the floor", Units = m )
        float3 _beaconSize;
        PROPERTY( Category = "View", DisplayName = "Lit Beacon Color" )
        float4 _litBeaconColor;
        PROPERTY( Category = "View", DisplayName = "Unlit Beacon Color" )
        float4 _unlitBeaconColor;
        PROPERTY( Category = "View", DisplayName = "Camp Radius", Min = 0.01, Units = m )
        float32 _campRadius;
        PROPERTY( Category = "View", DisplayName = "Camp Height", Tooltip = "Height of a camp's center above the floor", Units = m )
        float32 _campHeight;
        PROPERTY( Category = "View", DisplayName = "Camp Color" )
        float4 _campColor;
        PROPERTY( Category = "View", DisplayName = "Player Radius", Min = 0.01, Units = m )
        float32 _playerRadius;
        PROPERTY( Category = "View", DisplayName = "Player Height", Tooltip = "Height of the player's center above the floor", Units = m )
        float32 _playerHeight;
        PROPERTY( Category = "View", DisplayName = "Player Color" )
        float4 _playerColor;

        TComponentHandle<AdventureGameModeComponent> _director;
        PrefabViewBatch                              _cubeViews;
        PrefabViewBatch                              _sphereViews;
        uint8                                        _bInputLive    : 1; ///< 이번 틱의 시간이 흘렀다(멈춤 중에는 입력을 넘기지 않고 그리기만 한다)
        uint8                                        _bActionsValid : 1; ///< 액션 이름이 모두 있다(`onBeginPlay`)
        [[maybe_unused]] uint8                       _reserved      : 6;
    };
} // namespace sw
