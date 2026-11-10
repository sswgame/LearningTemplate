/**
 * @file AutomationUserDataRoot.h
 * @brief 시나리오 실행의 사용자 데이터 뿌리(`SW_USER_DATA_ROOT` — `UserDataPath`)를 자동화 폴더(`<Saved>/Automation/UserData/`) 아래로 가둡니다.
 * @details 와일드웨스트 체인에서 백엔드별 자식들이 사람의 `%LOCALAPPDATA%/SWEngine/<게임>/saves` 를 함께 써서, 한 프로세스가 다른 프로세스의 쓰는 중인 임시 파일을 잔재로 지웠다.
 *          러너가 환경 변수를 잘 주는 데 기대지 않는다 — `-scenario` 실행은 기동 때(호스트 서비스 · 게임 · 로컬 저장보다 먼저) 뿌리를 정하고, 자동화 폴더 밖이면 거절한다(종료 11, 로드 오류 —
 *          시나리오를 시작할 수 없는 실행이다). 뿌리가 없으면 프로세스마다 새 폴더(`run_p<pid>`)를 만들어 건다. 백엔드별 부모는 자식마다 `<뿌리>/<백엔드>` 를 건다(`AutomationPerBackend`).
 *          두 번 기동 쌍(`sequence/` · `statedir/`)은 러너가 묶음마다 같은 뿌리를 둘 모두에 건다(`Test/Scenario/RunStateDirPair.cmake`).
 *          사람이 일부러 실제 폴더(또는 자동화 폴더 밖 뿌리)를 쓰려면 `-gv_scenarioAllowOutsideUserData=true` 를 명시한다.
 *          뿌리 폴더를 `Content/scratch/` 에 두지 않는 까닭: 그곳은 리소스 도메인이라 에디터의 파일 감시 · 색인이 세이브를 에셋으로 본다 — 실행 산출물 자리(`Saved/Automation`)에 둔다
 *          (사용자 설정의 무인 자리 `Saved/Automation/UserSettings/` 와 같은 곳). 언리얼 Automation 의 `FPaths::AutomationDir`(Saved/Automation) · 유니티 Test Runner 가
 *          persistentDataPath 를 비우는 관례와 같은 자리다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    /** @struct AutomationUserDataRoot @brief 자동화 사용자 데이터 뿌리의 규칙입니다(상태 없음). */
    struct SW_DEVTOOLS_API AutomationUserDataRoot
    {
        /** @brief 뿌리 폴더 이름의 앞머리입니다(`run_p<pid>` — `AutomationScratch::makeInstanceID` 와 같은 꼬리라 죽은 실행의 폴더를 거둔다). */
        static constexpr string_view kRunFolderName = "run";

        /** @brief 자동화 사용자 데이터 뿌리들을 두는 폴더(`<Saved>/Automation/UserData`)입니다. */
        static string getBaseFolder();
        /**
         * @brief 뿌리 @p root 가 @p baseFolder 의 아래(그 폴더 자신은 아님)인 엄격한 절대 경로인지 봅니다. `..` 마디는 거절합니다(정규화 전 글로 폴더를 벗어날 수 있다).
         * @return 아니면 그 이유(뿌리가 대상)입니다.
         */
        [[nodiscard]] static expected<void> checkUnderBase( string_view root, string_view baseFolder );
        /**
         * @brief 이 프로세스의 뿌리를 정합니다. 환경 변수가 있으면 `checkUnderBase` 로 보고 그대로 쓰고, 없으면 `<base>/run_p<pid>` 를 비운 뒤 환경 변수에 겁니다(다시 불러도 같은 값).
         * @param bAllowOutside `-gv_scenarioAllowOutsideUserData` — 참이면 검사하지 않는다. 환경 변수가 없으면 빈 글(OS 사용자 폴더 그대로)이다.
         * @return 정한 뿌리(OS 폴더를 그대로 쓰면 빈 글)입니다. 자동화 폴더 밖이거나 환경 변수를 걸지 못하면 오류입니다.
         */
        [[nodiscard]] static expected<string> resolve( bool bAllowOutside );
        /** @brief 백엔드별 자식 하나의 뿌리(`<부모 뿌리>/<백엔드>`)입니다. */
        static string makeChildRoot( string_view parentRoot, string_view backend );
        /** @brief `-gv_scenarioAllowOutsideUserData` 의 값입니다(백엔드별 부모도 같은 값을 본다). */
        static bool isOutsideAllowed();
    };

    /** @struct UserDataFolderStamp @brief 폴더 아래 항목의 수와 가장 늦은 쓰기 시각입니다 — 같은 실행 안에서 폴더가 바뀌었는지 본다(`UserData.PlatformFolderChanged`). */
    struct UserDataFolderStamp
    {
        uint64 _entryCount{ 0 };
        int64  _latestWriteTicks{ 0 };
        bool   _bExists{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다

        [[nodiscard]] bool operator==( const UserDataFolderStamp& other ) const
        {
            return _entryCount == other._entryCount && _latestWriteTicks == other._latestWriteTicks && _bExists == other._bExists;
        }
        [[nodiscard]] bool operator!=( const UserDataFolderStamp& other ) const { return ( *this == other ) == false; }

        /** @brief @p folder 와 그 아래 모든 항목을 훑어 찍습니다(폴더가 없으면 `_bExists` 가 거짓). */
        [[nodiscard]] static UserDataFolderStamp take( string_view folder );
    };

    /**
     * @class AutomationUserDataIsolation
     * @brief `-scenario` 실행의 기동 단계에서 뿌리를 정하고(`AutomationUserDataRoot::resolve`), 자동화 폴더 밖이면 기동을 멈춥니다(종료 11). 개발 도구 클라이언트가 붙인다(`DevToolsClient`).
     * @details 기동 범위에 서므로 호스트 서비스(사용자 설정 · 게임 · 로컬 저장 `LocalStoreChannel`)가 뿌리를 읽기 전이다. 이 실행의 시작 때 사람의 데이터 폴더(활성 게임)를 찍어 두고,
     *          감시 값 `UserData.PlatformFolderChanged` 가 지금과 견준다.
     */
    class SW_DEVTOOLS_API AutomationUserDataIsolation final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "AutomationUserDataIsolation";

        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 정한 뿌리입니다(시나리오 실행이 아니거나 OS 폴더를 그대로 쓰면 빈 글). */
        const string& getRoot() const { return _root; }
        /** @brief 사람의 데이터 폴더(OS 규칙 · 활성 게임) 경로입니다. 모르면 빈 글. */
        const string& getPlatformFolder() const { return _platformFolder; }
        /** @brief 기동 때 찍은 사람의 데이터 폴더입니다. */
        const UserDataFolderStamp& getPlatformStartStamp() const { return _platformStartStamp; }

    private:
        string              _root{};
        string              _platformFolder{};
        UserDataFolderStamp _platformStartStamp{};
    };
} // namespace sw
