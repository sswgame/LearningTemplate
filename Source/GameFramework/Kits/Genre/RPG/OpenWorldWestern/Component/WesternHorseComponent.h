/**
 * @file WesternHorseComponent.h
 * @brief 말 한 마리의 상태(`WesternHorse` — 스태미나 · 유대 · 겁)를 가지고 같은 오브젝트의 `WesternHorseMountComponent` 에 빌려 주며, 시간을 흘립니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Rule/WesternHorse.h"

namespace sw
{
    class Archive;

    /**
     * @class WesternHorseComponent
     * @brief 말 오브젝트(폰, `MountMovementComponent`, 좌석, `WesternHorseMountComponent`)에 붙습니다. 품종(`_breed`)은 디렉터(`WesternGameModeComponent`)가 연 서부극 카탈로그의 `<Horse id>` 입니다. WildWest 게임에서 키트로 옮겼다.
     * @details 첫 틱에 디렉터(`_gameMode`)의 카탈로그로 말을 열고 같은 오브젝트의 `WesternHorseMountComponent` 에 빌려 준다(`bindHorse`). 디렉터가 판을 열지 못했거나
     *          품종이 카탈로그에 없거나 탈것 컴포넌트가 없으면 오류를 한 번 남기고 서지 않는다(기본 품종으로 바꾸지 않는다).
     *          PostUpdate 틱에서 시간을 흘린다 — 탈것 컴포넌트(PostPhysics)가 그 프레임의 질주로 스태미나를 쓴 뒤다. 게임 시간은 실제 초 × `_gameHoursPerSecond` 이다.
     *          상태는 스냅숏에 싣는다(`writeState` · `restoreState`, 게임 인스턴스가 `getStateSnapshot().registerStatefulComponent` 로 올린다). 복원 바이트는 말을 연 뒤에 적용한다.
     */
    REFLECT( Category = "Western", DisplayName = "Western Horse", Tooltip = "Owns one WesternHorse (stamina, bond, fear), lends it to the Western Horse Mount and runs its time" )
    class SW_KIT_API WesternHorseComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCCUtil::make( "WWHS" );
        static constexpr uint32 kStateVersion = 1;

        WesternHorseComponent();
        ~WesternHorseComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;

        /** @brief 말 상태를 씁니다. 첫 값은 표와 버전이고, 말을 열었는지와 (열었으면) 말 상태가 뒤따릅니다. */
        SW_DECLARE_STATE_IO_IN_PLACE();
        /** @brief `writeState` 의 바이트를 받아 둡니다. 말을 연 뒤에 적용하며, 읽지 못하면 알리고 새 말로 갑니다. */
        void restoreState( vector<uint8>&& bytes );

        /** @brief 말을 열었으면 그 상태이고, 아니면 nullptr 입니다(HUD · 감시 값이 읽는다). */
        const WesternHorse* findHorse() const { return _bOpened == SW_TRUE ? &_horse : nullptr; }
        /** @brief 먹이를 줍니다(`WesternHorse::feed` — 코어 · 게이지 회복, 유대 경험치). 말을 열지 않았거나 카탈로그에 없는 먹이면 false 입니다. */
        [[nodiscard]] bool feed( const hashed_string& foodID ) { return _bOpened == SW_TRUE && _horse.feed( foodID ); }

    private:
        /** @brief 디렉터의 카탈로그로 말을 열고 탈것 컴포넌트에 빌려 줍니다. 열지 못하면 까닭을 오류로 남기고 false 입니다. */
        [[nodiscard]] bool openHorse();
        /** @brief 받아 둔 복원 바이트를 말에 적용합니다. */
        void applyPendingState();

    private:
        PROPERTY( Category = "Horse", DisplayName = "Game Mode", Tooltip = "Object with the western game mode whose catalog has this breed" )
        GameObjectHandle _gameMode;
        PROPERTY( Category = "Horse", DisplayName = "Breed", Tooltip = "<Horse id> in the western catalog" )
        hashed_string _breed;
        PROPERTY( Category = "Horse", DisplayName = "Seed", Min = 0, Tooltip = "Random seed for fear reactions" )
        int32 _seed;
        PROPERTY( Category = "Horse", DisplayName = "Game Hours Per Second", Min = 0.0, Tooltip = "Game time that passes per real second (core drain, brush cooldown)" )
        float32 _gameHoursPerSecond;

        WesternHorse           _horse;
        vector<uint8>          _pendingStateBytes; ///< 말을 열기 전에 받은 복원 바이트
        uint8                  _bOpened  : 1;      ///< 말을 열고 탈것 컴포넌트에 빌려 주었다
        uint8                  _bFailed  : 1;      ///< 열지 못했다(오류를 남겼다 — 다시 시도하지 않는다)
        [[maybe_unused]] uint8 _reserved : 6;
    };
} // namespace sw
