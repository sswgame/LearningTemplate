/**
 * @file ExperienceManager.h
 * @brief 게임 인스턴스의 Experience 관리자입니다 — Experience 를 읽고 검사해 켜고(키트 · 카탈로그 · 입력 문맥 · UI) 그 맵으로 들어가며, 나올 때 끕니다.
 * @details 언리얼 Lyra `ULyraExperienceManagerComponent` 자리다(게임 상태 대신 게임 인스턴스 서브시스템 — 이 엔진은 게임 인스턴스가 게임 범위의 주인이다).
 *          두 층이다. 허브(gamesettings `hubExperience`)는 `onInitialize` 뒤 첫 씬 전에 켜 인스턴스 내내 바탕으로 두고(Lyra 기본 Experience 자리 — 지역 효과 시연처럼
 *          허브 밖 씬도 그 카탈로그를 읽는다), 미니게임은 그 위에 하나만 켠다. 새 미니게임으로 들어가면 앞의 것을 먼저 끈다 — 앞 Experience 의 등록이 다음으로 새지 않고,
 *          끈 뒤 남은 것은 해제 검사가 알린다(`ExperienceScope`).
 *          켜기는 맵을 요청하기 전이다 — 맵의 컴포넌트는 씬이 적용될 때(`beginPlay`, 맵 로드 완료 알림보다 앞) 키트 서비스 · 카탈로그를 찾는다
 *          (Lyra 도 Experience 를 다 올린 뒤에 `OnExperienceLoaded` 로 폰을 세운다).
 *          들어가기: `SceneSwitch` 는 허브 씬 안에서 들어가면 허브를 `GameStateSnapshot`(목적 `Snapshot`)으로 찍고 맵을 바꾸며, 돌아올 때 허브 맵을 연 뒤 복원한다.
 *          `SubLevel` 은 허브 씬에 맵을 하위 레벨(이름 = Experience 이름, 자리 = 원점 — 맵을 허브 좌표로 놓는다)로 더하고 나올 때 뺀다. 허브 틱은 멈추지 않는다 —
 *          언리얼 레벨 인스턴스 · 유니티 Additive 씬도 바탕 레벨을 멈추지 않고, 미니게임 입력은 `inputContexts` 레이어가 위에서 가린다.
 *          진입: 개발 명령 `experience.enter <이름>` · `experience.leave`, 허브 사물의 `ExperienceTriggerComponent`(틱 뒤에 맡기고 `update` 가 틱 밖에서 처리 — 스냅숏은
 *          씬을 도는 동안 찍지 않는다), 실행 전역 변수 `-gv_firstExperience=<이름>`(첫 씬 `gv_firstScene` 보다 위 — `enterFirstExperience`). 허브 이름으로 들어가면 허브로 돌아가기다.
 *          전역 변수라 시나리오 `<Launch>` 가 그대로 쓰고 체인 단계마다 되돌려진다(`AutomationSession::startStage`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceDefinition.h"
#include "GameFramework/Runtime/Foundation/Framework/GameInstanceSubsystem.h"

namespace sw
{
    class ExperienceScope;
    class GameInstance;
    class Scene;
    class SubsystemScope;
    struct GamePackConfig;

    /**
     * @class ExperienceManager
     * @brief 게임 인스턴스 서브시스템입니다(`game::findSubsystem<ExperienceManager>()`). 게임 스레드에서만 부릅니다.
     */
    class SW_GF_API ExperienceManager final : public GameInstanceSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "ExperienceManager";
        /** @brief Experience 파일의 팩 안 폴더입니다(`<팩>/experiences/<이름>.experience.xml`). */
        static constexpr const utf8* kExperienceFolder = "experiences/";
        /** @brief Experience 파일의 접미입니다. */
        static constexpr const utf8* kExperienceSuffix = ".experience.xml";

        /** @brief @p gameScope 는 키트가 서비스를 거는 게임 범위, @p packConfig 는 Experience · 카탈로그 경로의 팩 설정, @p owner 는 허브 스냅숏을 찍고 복원하는 인스턴스입니다. 셋 다 이 서브시스템보다 오래 산다. */
        ExperienceManager( SubsystemScope& gameScope, const GamePackConfig& packConfig, GameInstance& owner );
        ~ExperienceManager() override;

        ExperienceManager( const ExperienceManager& )            = delete;
        ExperienceManager& operator=( const ExperienceManager& ) = delete;

        const utf8* getSubsystemName() const override { return kSubsystemName; }
        /** @brief 미니게임 · 허브를 끄고 기다리던 진입 · 허브 스냅숏을 잊습니다 — 게임 인스턴스 종료(`onShutdown` 뒤)가 부른다. */
        void shutdownSubsystem() override;

        /**
         * @brief gamesettings `hubExperience` 가 있으면 허브를 켭니다(맵은 열지 않는다 — 첫 씬은 그대로다). 게임 인스턴스가 `onInitialize` 뒤 첫 씬 전에 부른다.
         * @return 비었으면 true, 못 읽거나 검사에서 지거나 `entry` 가 `SceneSwitch` 가 아니거나 켜기가 지면 알리고 false 입니다(게임 초기화 실패 — 필수 데이터 없이 띄우지 않는다).
         */
        [[nodiscard]] bool activateHub();

        /**
         * @brief Experience @p name 으로 들어갑니다. 허브 이름이면 `returnToHub` 입니다.
         * @details 미니게임은 읽고(`ExperienceDefinition::loadFromAsset`) 검사한 뒤(`validate`) 앞 미니게임을 끄고, 허브 씬에서 씬 전환이면 허브를 찍고, 새 것을 켜고, 맵을 열거나(`SceneSwitch`)
         *          허브 씬에 하위 레벨로 더한다(`SubLevel` — 맵을 기다리지 않고 바로 켜진 것이다).
         * @return 못 읽거나 검사에서 지거나, 허브 씬 밖에서 `SubLevel` 이거나, 다른 진입 · 돌아가기가 맵을 기다리는 중이면 알리고 false 입니다(켜진 것은 그대로).
         *         허브를 찍지 못하거나 켜기가 지거나 맵 · 하위 레벨 요청이 거절되어도 false 이고, 그때는 앞 미니게임도 이미 꺼져 켜진 미니게임이 없다.
         */
        [[nodiscard]] bool enter( string_view name );
        /**
         * @brief 미니게임을 끄고 허브로 돌아갑니다. 하위 레벨이면 빼기만 하고(허브는 그대로 섰다), 허브 씬 밖이면 허브 맵을 열고 찍어 둔 허브가 있으면 복원한다.
         * @return 허브가 없거나 다른 진입 · 돌아가기가 맵을 기다리는 중이거나 허브 맵 요청이 거절되면 알리고 false 입니다.
         */
        [[nodiscard]] bool returnToHub();
        /**
         * @brief 틱 뒤 작업(트리거)에서 진입을 맡깁니다 — 다음 `update`(틱 밖)가 `enter` 합니다. 이미 맡긴 것이 있으면 경고하고 이것을 버립니다.
         * @details 씬 전환의 허브 스냅숏은 씬을 도는 동안 찍지 않는다(자동 저장과 같은 자리 — `GameInstance::update`).
         */
        void requestEnter( string_view name );
        /** @brief 켜진 범위의 다시 읽힌 카탈로그를 새 판으로 걸고(`ExperienceScope::followReloadedCatalogs`) 맡긴 진입을 처리합니다 — `GameInstance::update` 앞머리(틱 밖)가 부른다. */
        void update();
        /**
         * @brief 실행 전역 변수 `gv_firstExperience` 가 있으면 그 Experience 로 들어갑니다 — 게임 인스턴스의 첫 씬 자리(`GameInstance::startFirstScene`)가 부른다.
         * @return 변수가 있으면(들어가기가 졌어도 — 다른 씬으로 대신하지 않는다) true, 비었으면 false 입니다.
         */
        bool enterFirstExperience();

        /** @brief 켜진 미니게임입니다. 없으면 nullptr 입니다. */
        const ExperienceDefinition* findActiveDefinition() const;
        /** @brief 켜진 허브입니다. 없으면 nullptr 입니다. */
        const ExperienceDefinition* findHubDefinition() const;
        /** @brief 켰고 미니게임 맵이 서기를 기다리는 중이면 true 입니다. */
        bool isEntering() const { return _bEntering == SW_TRUE; }
        /** @brief 허브 맵이 서기를 기다리는 중이면 true 입니다. */
        bool isReturning() const { return _bReturning == SW_TRUE; }
        /** @brief 이 인스턴스에서 끈 범위들이 남긴 것(해제 검사)의 합입니다. 감시 값 `Experience.LeakCount` 가 읽는다. */
        uint32 getLeakCount() const { return _leakCount; }
        /** @brief 씬 전환 미니게임에서 돌아와 허브 스냅숏을 복원한 수입니다. 감시 값 `Experience.HubRestoreCount` 가 읽는다. */
        uint32 getHubRestoreCount() const { return _hubRestoreCount; }

        /** @brief 이름 @p name 의 Experience 파일 리소스 id 입니다(`<팩>/experiences/<이름>.experience.xml`). */
        static string makeAssetPath( const GamePackConfig& packConfig, string_view name );

    private:
        /** @brief 진입한 미니게임 맵이 섰습니다(`GameSceneFlow::requestSceneThen`). @p pScene 이 nullptr 이면 실패로 알리고 켠 것을 끈다. */
        void onMapLoaded( Scene* pScene );
        /** @brief 돌아갈 허브 맵이 섰습니다. 찍어 둔 허브가 있으면 복원합니다. */
        void onHubLoaded( Scene* pScene );
        /** @brief @p name 의 정의를 읽고 이름 · 참조를 봅니다. 지면 알리고 false 입니다. */
        [[nodiscard]] bool loadDefinition( string_view name, ExperienceDefinition& outDefinition ) const;
        /** @brief @p definition 을 켭니다 — 키트(적은 순서) → 카탈로그 맞춤 → 입력 문맥 → UI. 하나라도 지면 그때까지 건 것을 거두고(해제 검사 포함) nullptr 입니다. */
        [[nodiscard]] unique_ptr<ExperienceScope> activate( const ExperienceDefinition& definition );
        /** @brief 켜진 미니게임을 끕니다(하위 레벨이면 허브 씬에서 뺀다). 없으면 할 일이 없다. 맵은 바꾸지 않는다. */
        void deactivateActive();
        /** @brief @p pScope 를 끄고 남은 것을 `_leakCount` 에 더한 뒤 비웁니다(없으면 할 일이 없다). */
        void deactivateScope( unique_ptr<ExperienceScope>& pScope );
        /** @brief 다른 진입 · 돌아가기가 맵을 기다리는 중이면 @p name 의 요청을 거절한다고 알리고 true 입니다. */
        bool refuseWhileLoading( string_view name ) const;
        /** @brief 활성 씬이 허브 맵이면 그 씬입니다. 허브가 없거나 다른 씬이면 nullptr 입니다. */
        Scene* findActiveHubScene() const;
        /** @brief 입력 맵 레이어를 스택에 넣어 켜고(`InputMap::pushLayer`) 끌 때 빼서 넣기 전의 켬으로 되돌립니다(`popLayer` — 한 쌍). 없는 레이어 · 입력 서비스가 없는 호스트에 적은 레이어는 오류입니다. */
        [[nodiscard]] static bool pushInputLayers( ExperienceScope& scope );

    private:
        SubsystemScope&             _gameScope;       ///< 키트가 서비스를 거는 게임 범위
        const GamePackConfig&       _packConfig;      ///< Experience · 카탈로그 경로의 팩 설정
        GameInstance&               _owner;           ///< 허브 스냅숏을 찍고 복원하는 인스턴스
        unique_ptr<ExperienceScope> _pHub;            ///< 켜진 허브(없으면 nullptr) — 인스턴스 내내 바탕
        unique_ptr<ExperienceScope> _pActive;         ///< 켜진 미니게임(없으면 nullptr)
        vector<uint8>               _hubSnapshot;     ///< 허브 씬에서 씬 전환 미니게임에 들어갈 때 찍은 허브(돌아와 복원하면 비운다)
        string                      _pendingEnter;    ///< 틱 뒤 작업에서 맡긴 진입(`requestEnter`) — 비었으면 없음
        uint32                      _leakCount;       ///< 끈 범위들이 남긴 것의 합
        uint32                      _hubRestoreCount; ///< 허브 스냅숏 복원 수
        uint8                       _bEntering  : 1;  ///< 켠 미니게임의 맵을 기다리는 중이면 1(하나만 — 겹친 진입은 거절한다)
        uint8                       _bReturning : 1;  ///< 허브 맵을 기다리는 중이면 1
    };
} // namespace sw
