/**
 * @file ActionPlatformerAvatarComponent.h
 * @brief 액션 플랫포머 키트의 클라이언트 쪽 — 입력 맵 액션을 디렉터(`ActionPlatformerGameModeComponent`)의 입력 명령으로 넘기고, 스테이지 · 몸을 프리팹 인스턴스로 보입니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_ActionPlatformer`, 전용 서버는 짓지 않는다). 디렉터의 명령(`submitPlayerInput`)과 읽기 API(`getTileMap` · `getRun` 등)만 쓰고
 *          몸 · 진행(규칙)을 직접 고치지 않는다. 디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다.
 *          명령 · 그리기는 틱 뒤 게임 스레드에서 한다. 이 컴포넌트는 `PostUpdate` 라 디렉터 걸음 뒤에 돌아, 넘긴 입력은 다음 걸음이 쓰고 그리기는 이번 걸음의 결과를 보인다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
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
    class ActionPlatformerGameModeComponent;
    class GameObjectManager;

    /**
     * @class ActionPlatformerAvatarComponent
     * @brief `Move` 축의 가로 값과 점프 액션을 누른 채인지를 프레임마다 디렉터 입력 명령으로 넘기고 스테이지를 그립니다.
     */
    REFLECT( Category = "ActionPlatformer", DisplayName = "Action Platformer Avatar",
             Tooltip = "Client side of an ActionPlatformer stage: sends input commands to the Action Platformer Game Mode on the same object and draws the stage" )
    class SW_KIT_CLIENT_API ActionPlatformerAvatarComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        ActionPlatformerAvatarComponent();
        virtual ~ActionPlatformerAvatarComponent() override;

        /** @brief 같은 오브젝트의 디렉터를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 액션 이름이 비었으면 오류를 남깁니다(폴백 없음). 틱 그룹은 뷰와 같은 `PostUpdate` 입니다. */
        void onBeginPlay() override;
        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;
        /** @brief 입력 · 명령 · 그리기를 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 스테이지를 그립니다. */
        void updateAvatar( GameObjectManager& manager );
        /** @brief 스테이지를 보입니다. 타일 · 플레이어 자리는 절대 자리다. */
        void showStage( const ActionPlatformerGameModeComponent& director, GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action whose X runs the body (required)" )
        hashed_string _moveAction;
        PROPERTY( Category = "Input", DisplayName = "Jump Action", Tooltip = "InputMap action held to jump (required, trigger Down - the game mode finds the press edge)" )
        hashed_string _jumpAction;
        PROPERTY( Category = "View", DisplayName = "Cube Prefab", Required, Tooltip = "1 m cube stretched into the tiles, the player and the goal post" )
        AssetRef<PrefabAsset> _cubePrefab;
        PROPERTY( Category = "View", DisplayName = "Sphere Prefab", Required, Tooltip = "1 m diameter sphere scaled to a secret" )
        AssetRef<PrefabAsset> _spherePrefab;
        PROPERTY( Category = "View", DisplayName = "Tile Color" )
        float4 _tileColor;
        PROPERTY( Category = "View", DisplayName = "Secret Radius", Min = 0.01, Units = m )
        float32 _secretRadius;
        PROPERTY( Category = "View", DisplayName = "Secret Color" )
        float4 _secretColor;
        PROPERTY( Category = "View", DisplayName = "Player Half Extent", Units = m )
        float3 _playerHalfExtent;
        PROPERTY( Category = "View", DisplayName = "Player Color" )
        float4 _playerColor;
        PROPERTY( Category = "View", DisplayName = "Goal Height", Min = 0.01, Units = m )
        float32 _goalHeight;
        PROPERTY( Category = "View", DisplayName = "Goal Width", Min = 0.01, Units = m )
        float32 _goalWidth;
        PROPERTY( Category = "View", DisplayName = "Goal Color" )
        float4 _goalColor;

        SW_TRANSIENT TComponentHandle<ActionPlatformerGameModeComponent> _director;
        SW_TRANSIENT PrefabViewBatch                                     _cubeViews;
        SW_TRANSIENT PrefabViewBatch                                     _secretViews;
        SW_TRANSIENT uint8                                               _bInputLive    : 1; ///< 이번 틱의 시간이 흘렀다(멈춤 중에는 입력을 넘기지 않고 그리기만 한다)
        SW_TRANSIENT uint8                                               _bActionsValid : 1; ///< 액션 이름이 모두 있다(`onBeginPlay`)
        [[maybe_unused]] uint8                                           _reserved      : 6;
    };
} // namespace sw
