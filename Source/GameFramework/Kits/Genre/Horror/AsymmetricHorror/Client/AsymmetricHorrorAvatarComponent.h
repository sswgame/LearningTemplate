/**
 * @file AsymmetricHorrorAvatarComponent.h
 * @brief 비대칭 공포 키트의 클라이언트 쪽 — 로컬 사용자마다 입력을 같은 번호의 생존자 명령으로 바꾸고, 무대 · 생존자 · 살인마를 원시 도형으로 그리며 HUD 를 채웁니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_AsymmetricHorror`, 전용 서버는 짓지 않는다). 디렉터(`AsymmetricHorrorGameModeComponent`)의 명령과 읽기 API 만 쓰고 규칙을 직접 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. 명령 · 그리기는 틱 뒤 게임 스레드에서 한다.
 *          `PostUpdate` 라 넘긴 입력은 다음 걸음이 쓰고 그리기는 이번 걸음의 결과를 보인다. 모습은 원시 도형 표식(`PrimitiveMarkerBatch`)이라 저장에 싣지 않는다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Appearance/PrimitiveMarkerBatch.h"

namespace sw
{
    class AsymmetricHorrorGameModeComponent;
    class AsymmetricHorrorHUDViewModel;
    class GameObjectManager;

    /**
     * @class AsymmetricHorrorAvatarComponent
     * @brief 입력 맵 액션을 프레임마다 디렉터 명령으로 넘기고 판을 그리며 HUD 를 채웁니다.
     */
    REFLECT( Category = "AsymmetricHorror", DisplayName = "Asymmetric Horror Avatar",
             Tooltip = "Client side of an asymmetric horror match: sends commands to the Asymmetric Horror Game Mode on the same object, draws the game and fills the HUD" )
    class SW_KIT_CLIENT_API AsymmetricHorrorAvatarComponent : public Component
    {
    public:
        REFLECT_BODY();

        AsymmetricHorrorAvatarComponent();
        virtual ~AsymmetricHorrorAvatarComponent() override;

        /** @brief 같은 오브젝트의 디렉터를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 액션 이름이 비었으면 오류를 남깁니다(폴백 없음). 틱 그룹은 뷰와 같은 `PostUpdate` 입니다. */
        void onBeginPlay() override;
        /** @brief 세운 표식을 지웁니다. */
        void onEndPlay() override;
        /** @brief 입력 · 명령 · 그리기를 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 판을 그리며 HUD 를 채웁니다. */
        void updateAvatar( GameObjectManager& manager );
        /** @brief 입력 맵을 읽어 디렉터 명령으로 넘깁니다. */
        void submitInput( AsymmetricHorrorGameModeComponent& director ) const;
        /** @brief 판을 표식으로 그립니다. */
        void drawGame( GameObjectManager& manager, const AsymmetricHorrorGameModeComponent& director );
        /** @brief HUD 값을 채웁니다. */
        static void fillHUD( AsymmetricHorrorHUDViewModel& outViewModel, const AsymmetricHorrorGameModeComponent& director );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that walks a survivor (required)" )
        hashed_string _moveAction;
        PROPERTY( Category = "Input", DisplayName = "Use Action", Tooltip = "InputMap action: press to repair, open, rescue, heal or answer a skill check, hold to struggle (required)" )
        hashed_string _useAction;
        PROPERTY( Required, Category = "Look", DisplayName = "Marker Prefab", Tooltip = "Prefab with one mesh, spawned for everything the view shows" )
        AssetRef<PrefabAsset> _markerPrefab;

        TComponentHandle<AsymmetricHorrorGameModeComponent> _director;
        PrimitiveMarkerBatch                                _markers;
        unique_ptr<AsymmetricHorrorHUDViewModel>            _hudViewModel;      ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다 — 먼저 지워지면 HUD 가 스스로 뗀다)
        uint8                                               _bInputLive    : 1; ///< 이번 틱의 시간이 흘렀다(멈춤 중에는 입력을 넘기지 않고 그리기만 한다)
        uint8                                               _bActionsValid : 1; ///< 액션 이름이 모두 있다(`onBeginPlay`)
        [[maybe_unused]] uint8                              _reserved      : 6;
    };
} // namespace sw
