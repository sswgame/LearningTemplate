/**
 * @file ExperienceManager.h
 * @brief 게임 인스턴스의 Experience 관리자입니다 — Experience 를 읽고 검사해 그 맵으로 들어가고, 맵이 서면 켜고(키트 · 입력 문맥 · UI · 카탈로그), 나올 때 끕니다.
 * @details 언리얼 Lyra `ULyraExperienceManagerComponent` 자리다(게임 상태 대신 게임 인스턴스 서브시스템 — 이 엔진은 게임 인스턴스가 게임 범위의 주인이다).
 *          켜진 Experience 는 하나다. 새로 들어가면 앞의 것을 먼저 끈다 — 앞 Experience 의 등록이 다음으로 새지 않는다(`ExperienceScope`).
 *          진입: 개발 명령 `experience.enter <이름>`, 개발 실행 인자 `-experience=<이름>`(첫 씬 `gv_firstScene` 보다 위 — `GameInstance::startFirstScene`).
 *          허브의 사물 상호작용 · 트리거가 부르는 진입과 허브로 돌아오기(`returnTo` · 허브 스냅숏)는 허브 이전 ② 에서 붙는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceDefinition.h"
#include "GameFramework/Runtime/Foundation/Framework/GameInstanceSubsystem.h"

namespace sw
{
    class ExperienceScope;
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

        /** @brief @p gameScope 는 키트가 서비스를 거는 게임 범위, @p packConfig 는 Experience · 카탈로그 경로의 팩 설정입니다. 둘 다 이 서브시스템보다 오래 산다. */
        ExperienceManager( SubsystemScope& gameScope, const GamePackConfig& packConfig );
        ~ExperienceManager() override;

        ExperienceManager( const ExperienceManager& )            = delete;
        ExperienceManager& operator=( const ExperienceManager& ) = delete;

        const utf8* getSubsystemName() const override { return kSubsystemName; }
        /** @brief 켜진 Experience 를 끄고 기다리던 진입을 잊습니다 — 게임 인스턴스 종료(`onShutdown` 뒤)가 부른다. */
        void shutdownSubsystem() override;

        /**
         * @brief Experience @p name 으로 들어갑니다 — 읽고(`ExperienceDefinition::loadFromAsset`) 검사한 뒤(`validate`) 켜진 것을 끄고 그 맵을 엽니다. 맵이 서면 켭니다.
         * @return 못 읽거나 검사에서 지거나, 아직 지원하지 않는 진입(`SubLevel`)이거나, 다른 진입이 맵을 기다리는 중이거나, 맵 요청이 거절되면 알리고 false 입니다(켜진 것은 그대로).
         */
        [[nodiscard]] bool enter( string_view name );
        /** @brief 켜진 Experience 를 끕니다(없으면 할 일이 없다). 맵은 바꾸지 않는다. */
        void leave();

        /** @brief 켜진 Experience 입니다. 없으면 nullptr 입니다. */
        const ExperienceDefinition* findActiveDefinition() const;
        /** @brief 맵이 서기를 기다리는 진입이 있으면 true 입니다. */
        bool isEntering() const { return _pending.has_value(); }

        /** @brief 이름 @p name 의 Experience 파일 리소스 id 입니다(`<팩>/experiences/<이름>.experience.xml`). */
        static string makeAssetPath( const GamePackConfig& packConfig, string_view name );

    private:
        /** @brief 진입한 맵이 섰습니다(`GameSceneFlow::requestSceneThen`) — 기다리던 Experience 를 켭니다. @p pScene 이 nullptr 이면 실패로 알린다. */
        void onMapLoaded( Scene* pScene );
        /** @brief @p definition 을 켭니다 — 키트(적은 순서) → 카탈로그 맞춤 → 입력 문맥 → UI. 하나라도 지면 그때까지 건 것을 거두고 false 입니다. */
        [[nodiscard]] bool activate( const ExperienceDefinition& definition );
        /** @brief 입력 맵 레이어를 켜고(`InputMap::pushLayer`) 끌 때 뺍니다. 없는 레이어 · 입력 서비스가 없는 호스트에 적은 레이어는 오류입니다. */
        [[nodiscard]] static bool pushInputLayers( ExperienceScope& scope );

    private:
        SubsystemScope&                _gameScope;  ///< 키트가 서비스를 거는 게임 범위
        const GamePackConfig&          _packConfig; ///< Experience · 카탈로그 경로의 팩 설정
        optional<ExperienceDefinition> _pending;    ///< 맵이 서기를 기다리는 진입(하나만 — 겹친 진입은 거절한다)
        unique_ptr<ExperienceScope>    _pActive;    ///< 켜진 Experience(없으면 nullptr)
    };
} // namespace sw
