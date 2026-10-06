/**
 * @file WesternHorseMountComponent.h
 * @brief 서부극 말을 탈것 이동에 잇습니다 — 질주는 스태미나(`WesternHorse::gallop`)가 버틸 때만, 유대 능력이 없으면 질주 없음, 탄 동안 유대가 쌓이고, 겁으로 탄 사람을 떨어뜨리면 강제 하차.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Rpg/OpenWorldWestern/WesternHorse.h"

namespace sw
{
    /**
     * @class WesternHorseMountComponent
     * @brief 말 오브젝트(폰 · 캐릭터 컨트롤러 · `MountMovementComponent` · 좌석)에 붙습니다. 말 상태(`WesternHorse`)는 게임 · 디렉터가 들고 이 컴포넌트는
     *        빌려 씁니다(`bindHorse` — 시작하기 전에 묶는다). 시간 흐름(`WesternHorse::update`)은 말을 든 쪽이 돌립니다.
     * @details PostPhysics 틱에서 이번 프레임에 고른 걸음새를 읽습니다 — 질주였으면 스태미나를 쓰고(`gallop`), 바닥나면 질주를 막았다가 `_resumeGallopRatio`
     *          만큼 돌아오면 풉니다. 운전석에 누가 앉아 있으면 `setRidden( true )`. 겁은 `frighten` 으로 줍니다 — 반응이 `Bucked` 면 앉은 이 모두를 강제로 내린다
     *          (결정 9 — 탑승 중 쓰러지거나 말이 떨어뜨리면 강제 하차).
     */
    REFLECT( Category = "Western", DisplayName = "Western Horse Mount", Tooltip = "Ties a WesternHorse to mount movement: gallop on stamina, bond abilities, buck the rider off" )
    class SW_GF_API WesternHorseMountComponent : public Component
    {
    public:
        REFLECT_BODY();

        WesternHorseMountComponent();
        ~WesternHorseMountComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 말 상태를 빌립니다(소유하지 않는다). nullptr 이면 풉니다. */
        void          bindHorse( WesternHorse* pHorse ) { _pHorse = pHorse; }
        WesternHorse* findHorse() const { return _pHorse; }
        /** @brief 겁을 줍니다(총소리 · 뱀). 반응이 `Bucked` 면 앉은 이를 모두 강제로 내립니다(틱 안이면 틱 뒤에). 말이 묶이지 않았으면 `Calm` 입니다. */
        WesternHorseReaction frighten( float32 amount );

    private:
        /** @brief 이 말의 좌석에 앉은 이를 모두 강제로 내립니다. */
        void throwRiders();
        /** @brief 운전석에 누가 앉아 있는지입니다. */
        bool isRidden() const;

    private:
        PROPERTY( Category = "Western", DisplayName = "Gallop Ability", Tooltip = "Bond ability the horse needs before it can gallop (empty: any horse gallops)" )
        hashed_string _gallopAbility;
        PROPERTY( Category = "Western", DisplayName = "Resume Gallop Ratio", Min = 0.0, Max = 1.0, Tooltip = "Stamina ratio at which a horse that ran out may gallop again" )
        float32 _resumeGallopRatio;

        WesternHorse* _pHorse; ///< 빌린 말 상태(게임 · 디렉터가 소유)
    };
} // namespace sw
