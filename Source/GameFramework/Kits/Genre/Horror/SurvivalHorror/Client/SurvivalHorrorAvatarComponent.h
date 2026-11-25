/**
 * @file SurvivalHorrorAvatarComponent.h
 * @brief 생존 공포 키트의 클라이언트 쪽 — 입력 맵 액션을 걷기 · 쓰기 · 손전등 · 회복 명령으로 바꾸고, 집의 방 · 쓸 수 있는 자리 · 괴물을 원시 도형으로 그리며 HUD 를 채웁니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_SurvivalHorror`, 전용 서버는 짓지 않는다). 디렉터(`SurvivalHorrorGameModeComponent`)의 명령과 읽기 API 만 쓰고 규칙을 직접 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. 명령 · 그리기는 틱 뒤 게임 스레드에서 한다.
 *          `PostUpdate` 라 넘긴 입력은 다음 걸음이 쓰고 그리기는 이번 걸음의 결과를 보인다. 모습은 원시 도형 표식(`PrefabViewBatch`)이라 저장에 싣지 않는다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class SurvivalHorrorGameModeComponent;
    class SurvivalHorrorHUDViewModel;
    class GameObjectManager;

    /**
     * @class SurvivalHorrorAvatarComponent
     * @brief 입력 맵 액션을 프레임마다 디렉터 명령으로 넘기고 판을 그리며 HUD 를 채웁니다.
     */
    REFLECT( Category = "SurvivalHorror", DisplayName = "Survival Horror Avatar",
             Tooltip = "Client side of a survival horror game: sends commands to the Survival Horror Game Mode on the same object, draws the game and fills the HUD" )
    class SW_KIT_CLIENT_API SurvivalHorrorAvatarComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        SurvivalHorrorAvatarComponent();
        virtual ~SurvivalHorrorAvatarComponent() override;

        /** @brief 세운 표식을 지웁니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 판을 그리며 HUD 를 채웁니다. */
        void updateAvatar( GameObjectManager& manager, SurvivalHorrorGameModeComponent& director );
        /** @brief 입력 맵을 읽어 디렉터 명령으로 넘깁니다. */
        void submitInput( const GameModeInput& input, SurvivalHorrorGameModeComponent& director ) const;
        /** @brief 판을 표식으로 그립니다. */
        void drawGame( GameObjectManager& manager, const SurvivalHorrorGameModeComponent& director );
        /** @brief HUD 값을 채웁니다. */
        static void fillHUD( SurvivalHorrorHUDViewModel& outViewModel, const SurvivalHorrorGameModeComponent& director );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that walks the player (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Use Action", Tooltip = "InputMap action: use the nearest spot (required)" )
        hashed_string _useAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Light Action", Tooltip = "InputMap action that turns the flashlight on and off (required)" )
        hashed_string _lightAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Heal Action", Tooltip = "InputMap action: use a healing item or mix two items (required)" )
        hashed_string _healAction;
        PROPERTY( Required, Category = "Look", DisplayName = "Marker Prefab", Tooltip = "Prefab with one mesh, spawned for everything the view shows" )
        AssetRef<PrefabAsset> _markerPrefab;

        PrefabViewBatch                        _markers;
        unique_ptr<SurvivalHorrorHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다 — 먼저 지워지면 HUD 가 스스로 뗀다)
    };
} // namespace sw
