/**
 * @file DevTools/Maintenance/SavedCleanupSubsystem.h
 * @brief 실행 산출물 폴더(`Saved`)에서 오래된 것을 지웁니다 — 시나리오 실행 · 로그 · 스크린샷 · 트레이스 · 버그 · 크래시 보고 · 실패한 테스트 실행.
 * @details 개발 도구다 — `Saved` 는 실행 파일 옆의 개발 산출물 자리이고 배포 빌드는 이 단위(DevTools)를 짓지 않는다. 언리얼 `FMaintenance::DeleteOldLogs` 와 같은 자리다. 지우는 쪽이 없어 실행마다 쌓였다(자동화 실행 폴더만 100 GB 넘게). 무엇을 몇 개 남길지 · 최소 나이 · 버릴 자리는 데이터(`Config/Dev/SavedCleanup.json`)가 정하고,
 *          최근에 바뀐 것은 지우지 않는다(나란히 도는 다른 프로세스 — ctest 의 백엔드 넷 — 의 폴더를 건드리지 않게).
 *          버릴 것은 먼저 `Saved/.Trash` 로 옮기고(이름만 바꿔 곧바로 끝난다) 지우기는 자기 스레드가 뒤에서 한다 — 기동을 늦추지 않고 공용 일꾼을 붙잡지 않으며, 덜 지운 것은 다음 기동이 잇는다. 셰이더 캐시 · 에디터 상태 · 사용자 설정은 보지 않는다.
 */
#pragma once
#include "Core/Concurrency/thread.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Subsystem/Subsystem.h"

#include "DevTools/DevToolsAPI.h"

#include "Engine/Config/ConfigFileID.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class StopToken;

    /** @class SavedCleanupSubsystem @brief `Saved` 의 오래된 실행 산출물을 지우는 서브시스템입니다. */
    REFLECT()
    class SW_DEVTOOLS_API SavedCleanupSubsystem final : public Subsystem
    {
    public:
        REFLECT_BODY();

        static constexpr const utf8* kSubsystemName = "SavedCleanup";

        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override;
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

    private:
        friend struct SavedCleanupTestAccess;

        // 규칙 파일(`Config/Dev/SavedCleanup.json`)의 스키마다 — 키는 칸 이름 그대로(`JSONSerializer`). 이 클래스만 쓰므로 private 중첩이다(등록기는 위 `REFLECT_BODY()` friend 로 닿는다).
        // 짧은 이름은 레지스트리 전역에서 하나여야 해서 `Retention` 을 붙인다.
        /** @brief 폴더 하나의 보존 규칙입니다. `_bPerChild` 면 그 아래 폴더마다(시나리오마다) 따로 센다. */
        REFLECT()
        struct RetentionRule
        {
            REFLECT_BODY();

            PROPERTY( Required )
            string _folder;
            PROPERTY( Required )
            uint32 _keepCount{ 0 };
            PROPERTY( Required )
            bool _bPerChild{ false }; ///< 그 아래 폴더마다 따로 센다
            PROPERTY( Required )
            vector<string> _listSkipChild; ///< 실행 산출물이 아니라 상태인 하위 폴더(`_bPerChild` 일 때)
        };
        /** @brief 규칙 파일 전체입니다. */
        REFLECT()
        struct RetentionSettings
        {
            REFLECT_BODY();

            static constexpr ConfigFileID<RetentionSettings> kConfigFile{ "Config/Dev/SavedCleanup.json" };

            PROPERTY( Required )
            int64 _minAgeSeconds{ 0 }; ///< 이보다 최근에 바뀐 것은 지우지 않는다 — 나란히 도는 프로세스의 것일 수 있다
            PROPERTY( Required )
            string _trashFolder; ///< 버릴 것을 먼저 옮기는 자리 — 지우기는 뒤에서, 다음 기동이 남은 것을 이어 지운다
            PROPERTY( Required )
            vector<RetentionRule> _listRule;

            /** @brief 폴더 이름이 비지 않았는지 봅니다(`ConfigManager::readConfig` 가 부른다). */
            [[nodiscard]] expected<void> validate() const;
        };

        /** @brief 규칙대로 오래된 산출물을 버릴 자리로 옮기고 그 자리를 비웁니다(일꾼 스레드 본문). */
        static void run( const StopToken& stopToken );

        thread _worker; ///< 지우는 스레드 — 종료가 멈추라 하고 합류한다
    };
} // namespace sw
