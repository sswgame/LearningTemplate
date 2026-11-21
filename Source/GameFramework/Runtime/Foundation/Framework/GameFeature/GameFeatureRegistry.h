/**
 * @file GameFeatureRegistry.h
 * @brief 게임 기능(키트가 게임 범위에 거는 서비스 · 카탈로그 묶음)의 등록부와 `SW_GAME_FEATURE` 입니다 — 키트는 모두 링크되어 있고, 키트가 `GameFeatureScope` 에 무엇을 거는지(켜기 함수)만 여기 적습니다.
 * @details 등록은 범위에 걸 것(서비스 · 카탈로그 · 전역 변수)이 있는 키트만 한다 — 빈 켜기 함수를 두지 않는다(규칙 객체를 시연 컴포넌트가 쥐는 키트는 등록하지 않는다).
 *          언리얼 Game Features 의 액션(`UGameFeatureAction::OnGameFeatureActivating` · `OnGameFeatureDeactivating`)과 같은 자리다. 다른 점: 끄기 함수가 없다 —
 *          켜기 함수는 `GameFeatureScope` 만 받고, 범위에 건 것(서비스 · 전역 변수 · 되돌릴 일)은 범위가 역순으로 거둔다. 그래서 키트가 끄기를 빠뜨려 등록이 다음 미니게임으로
 *          새는 모양을 쓸 수 없다. 켜는 쪽은 둘이다 — 게임 인스턴스(gamesettings `gameFeatures`, 게임 범위)와 미니게임(`MinigameDefinition` 의 `features`, `MinigameFlow`).
 *          런타임 모듈 로드 · 언로드는 기각(정적 구성 불가, 미니게임마다 언로드 절차 — `docs/agents/Gameplay.md` "게임 기능 · 미니게임").
 *          등록부는 GameFramework 의 모듈 서브시스템(엔진 범위)이고, 키트의 등록자는 모듈을 올릴 때 붙고 내리기 전에 떨어진다(`ModuleEntryRegistrar` — 개발 명령과 같은 길).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/MemoryTag.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/Subsystem/Subsystem.h"

#include "Engine/Module/ModuleEntryRegistrar.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameFeatureScope;

    /**
     * @brief 게임 기능 하나의 켜기 함수입니다. 서비스 · 카탈로그 · 전역 변수 · 되돌릴 일을 @p scope 에 겁니다(끄기는 범위가 한다).
     * @return 걸지 못했으면(겹친 서비스 · 못 읽은 카탈로그) 이유를 알리고 false 입니다 — 범위 켜기 전체가 지고, 그때까지 건 것은 범위가 거둔다.
     */
    using GameFeatureActivateFunction = bool ( * )( GameFeatureScope& scope );

    /** @brief 게임 기능 하나의 등록 줄입니다. 문자열은 등록한 이미지(키트 모듈)의 정적 저장소에 있습니다. */
    struct GameFeatureRegistration
    {
        const utf8*                 _pName;     ///< `features` · gamesettings `gameFeatures` 에 적는 이름(대소문자 무시, 보통 키트 모듈 이름에서 `GF_` 를 뗀 것)
        GameFeatureActivateFunction _pActivate; ///< 켜기 함수
    };

    /**
     * @class GameFeatureRegistry
     * @brief 올라온 키트가 등록한 게임 기능 켜기 함수의 목록입니다(엔진 범위 — GameFramework 의 모듈 서브시스템). 같은 이름의 둘째 등록은 거절합니다.
     * @details 등록은 모듈을 올리고 내리는 스레드에서, 조회는 게임 스레드에서 오므로 잠금 하나로 지킨다. 찾은 줄은 그 키트 모듈이 내려가기 전까지 산다.
     */
    class SW_GF_API GameFeatureRegistry final : public Subsystem
    {
    public:
        static constexpr MemoryTag   kSubsystemMemoryTag = MemoryTag::Game;
        static constexpr const utf8* kSubsystemName      = "GameFeatureRegistry";

        GameFeatureRegistry();

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 게임 기능을 올립니다. 이름이 비었거나 이미 있으면 알리고 false 입니다. */
        [[nodiscard]] bool registerEntry( const GameFeatureRegistration* pRegistration );
        /** @brief 게임 기능을 뺍니다(없으면 할 일이 없다). */
        void unregisterEntry( const GameFeatureRegistration* pRegistration );
        /** @brief 이름(대소문자 무시)으로 게임 기능을 찾습니다. 없으면 nullptr 입니다. */
        const GameFeatureRegistration* findFeature( string_view name ) const;

    private:
        mutable mutex                                                 _mutex;
        RegistrationList<const GameFeatureRegistration> _registration SW_GUARDED_BY( _mutex ); ///< 이름 필수, 대소문자 무시
    };

    /** @brief `SW_GAME_FEATURE` 이 두는 정적 등록자입니다. */
    using GameFeatureRegistrar = ModuleEntryRegistrar<GameFeatureRegistry, const GameFeatureRegistration*>;
} // namespace sw

/**
 * @brief 키트의 게임 기능 켜기 함수를 키트의 `.cpp` 에서 등록합니다(`namespace sw` 안, 함수 밖). 모든 구성에서 등록한다 — 배포도 게임 기능을 켜고 미니게임에 들어간다.
 * @param varName   파일 안에서 유일한 이름 조각(변수 이름용)
 * @param pName     `features` · `gameFeatures` 에 적는 이름(리터럴)
 * @param pActivate `bool( GameFeatureScope& )` 켜기 함수
 */
#define SW_GAME_FEATURE( varName, pName, pActivate )                                                     \
    static const ::sw::GameFeatureRegistration sw_gameFeatureRegistration_##varName{ pName, pActivate }; \
    static ::sw::GameFeatureRegistrar          sw_gameFeatureRegistrar_##varName                         \
    {                                                                                                    \
        &sw_gameFeatureRegistration_##varName                                                            \
    }
