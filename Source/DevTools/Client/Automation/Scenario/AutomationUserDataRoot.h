/**
 * @file AutomationUserDataRoot.h
 * @brief 자동화 실행(`-unattended` · `-scenario`, `CommandLineManager::isAutomatedRun`)의 사용자 데이터 뿌리(`SW_USER_DATA_ROOT`, `UserDataPath`)를 자동화 폴더(`<Saved>/Automation/`) 아래로 가둡니다.
 * @details 와일드웨스트 체인에서 백엔드별 자식들이 사람의 `%LOCALAPPDATA%/SWEngine/<게임>/saves` 를 함께 써서, 한 프로세스가 다른 프로세스의 쓰는 중인 임시 파일을 잔재로 지웠다.
 *          러너가 환경 변수를 잘 주는 데 기대지 않는다. 자동화 실행은 기동 때(호스트 서비스 · 게임 · 로컬 저장보다 먼저) 뿌리를 정하고, 자동화 폴더 밖이면 거절한다(종료 11, 로드 오류:
 *          시나리오를 시작할 수 없는 실행이다). 뿌리가 없으면 실행 뿌리(`ProcessRoots::_run`) 아래 `userdata` 를 건다 — 실행 뿌리가 실행마다 새 폴더라 pid 꼬리도 형제 거두기도 없다.
 *          백엔드별 부모는 자식마다 제 실행 뿌리(`<부모 실행 뿌리>/<백엔드>`)를 주고 그 아래 `userdata` 를 건다(`AutomationPerBackend`).
 *          두 번 기동 쌍(`sequence/` · `statedir/`)은 러너가 묶음마다 같은 뿌리를 둘 모두에 건다(`Test/Scenario/RunStateDirPair.cmake`).
 *          사람이 일부러 실제 폴더(또는 자동화 폴더 밖 뿌리)를 쓰려면 `-gv_automationAllowOutsideUserData=true` 를 명시한다.
 *          실행마다 따로 써야 하는 상태 자리 변수(`gv_editorStateDir` · `gv_userSettingsFile`)를 명령줄로 주면 그 자리도 사람의 폴더(`Saved/Editor` · OS 사용자 폴더)면 거절하고,
 *          백엔드별 부모는 그 변수를 자식 모두에 넘기지 않는다(한 자리를 함께 쓰게 된다 — 거절).
 *          뿌리 폴더를 리소스 도메인에 두지 않는 까닭: 에디터의 파일 감시 · 색인이 세이브를 에셋으로 본다 — 실행 산출물 자리(`Saved/Automation`)에 둔다
 *          (사용자 설정의 무인 자리 `Saved/Automation/UserSettings/` 와 같은 곳). 언리얼 Automation 의 `FPaths::AutomationDir`(Saved/Automation) · 유니티 Test Runner 가
 *          persistentDataPath 를 비우는 관례와 같은 자리다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Subsystem/Subsystem.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    /** @struct AutomationUserDataRoot @brief 자동화 사용자 데이터 뿌리의 규칙입니다(상태 없음). */
    struct SW_DEVTOOLS_API AutomationUserDataRoot
    {
        /** @brief 실행 뿌리 아래 사용자 데이터 폴더 이름입니다(`<실행 뿌리>/userdata`). */
        static constexpr string_view kRunSubfolder = "userdata";

        /** @brief 자동화 사용자 데이터 뿌리가 놓일 수 있는 폴더(`<Saved>/Automation` — 실행 뿌리 `Runs/…` 와 손으로 고른 `UserData/…`)입니다. */
        static AbsolutePath getBaseFolder();
        /** @brief 실행 뿌리 @p runRoot 의 사용자 데이터 뿌리(`<실행 뿌리>/userdata`)입니다. */
        static AbsolutePath makeRunUserDataRoot( const AbsolutePath& runRoot );
        /**
         * @brief 뿌리 @p root 가 @p baseFolder 의 아래(그 폴더 자신은 아님)인 엄격한 절대 경로인지 봅니다. `..` 마디는 거절합니다(정규화 전 글로 폴더를 벗어날 수 있다).
         * @return 아니면 그 이유(뿌리가 대상)입니다.
         */
        [[nodiscard]] static expected<void> validateUnderBase( string_view root, string_view baseFolder );
        /**
         * @brief 이 프로세스의 뿌리를 정합니다. 환경 변수가 있으면 `validateUnderBase` 로 보고 그대로 쓰고, 없으면 `<실행 뿌리>/userdata` 를 환경 변수에 겁니다(다시 불러도 같은 값).
         * @param bAllowOutside `-gv_automationAllowOutsideUserData` — 참이면 검사하지 않는다. 환경 변수가 없으면 빈 글(OS 사용자 폴더 그대로)이다.
         * @return 정한 뿌리(OS 폴더를 그대로 쓰면 빈 글)입니다. 자동화 폴더 밖이거나 환경 변수를 걸지 못하면 오류입니다.
         */
        [[nodiscard]] static expected<string> resolve( bool bAllowOutside );
        /** @brief `-gv_automationAllowOutsideUserData` 의 값입니다(백엔드별 부모도 같은 값을 본다). */
        static bool isOutsideAllowed();
        /** @brief 사람의 폴더들입니다 — 에디터 상태(`<프로젝트>/Saved/Editor`)와 OS 사용자 데이터 · 설정 폴더(게임들의 부모, 모르면 뺀다). */
        static vector<string> makePersonFolders();
        /** @brief 상태 자리 변수 @p name 의 값 @p path 가 @p listPersonFolder 의 어느 것 자신이거나 아래면 오류입니다(대소문자는 OS 규칙). */
        [[nodiscard]] static expected<void> validateStatePath( string_view name, string_view path, const vector<string>& listPersonFolder );
        /** @brief 넘겨줄 인자 @p listArgument 가운데 상태 자리 변수(`-gv_editorStateDir=…` 등)가 있으면 그 인자입니다 — 백엔드별 자식이 모두 같은 자리를 쓰게 된다. */
        [[nodiscard]] static optional<string> findSharedStateArgument( const vector<string>& listArgument );
    };

    /** @struct UserDataFolderStamp @brief 폴더 아래 항목의 수와 가장 늦은 쓰기 시각입니다 — 같은 실행 안에서 폴더가 바뀌었는지 본다(`UserData.PlatformFolderChanged`). */
    struct UserDataFolderStamp
    {
        uint64 _entryCount{ 0 };
        int64  _latestWriteTicks{ 0 };
        bool   _bExists{ false };

        [[nodiscard]] bool operator==( const UserDataFolderStamp& other ) const
        {
            return _entryCount == other._entryCount && _latestWriteTicks == other._latestWriteTicks && _bExists == other._bExists;
        }
        [[nodiscard]] bool operator!=( const UserDataFolderStamp& other ) const { return ( *this == other ) == false; }

        /** @brief @p folder 와 그 아래 모든 항목을 훑어 찍습니다(폴더가 없으면 `_bExists` 가 거짓). */
        [[nodiscard]] static UserDataFolderStamp take( const AbsolutePath& folder );
    };

    /**
     * @class AutomationUserDataIsolation
     * @brief 자동화 실행의 기동 단계에서 뿌리를 정하고(`AutomationUserDataRoot::resolve`), 자동화 폴더 밖이거나 상태 자리가 사람의 폴더면 기동을 멈춥니다(종료 11). 개발 도구 클라이언트가 붙인다(`DevToolsClient`).
     * @details 기동 범위에 서므로 호스트 서비스(사용자 설정 · 게임 · 로컬 저장 `LocalStoreChannel`)가 뿌리를 읽기 전이다. 이 실행의 시작 때 사람의 데이터 폴더(활성 게임)를 찍어 두고,
     *          감시 값 `UserData.PlatformFolderChanged` 가 지금과 견준다.
     */
    class SW_DEVTOOLS_API AutomationUserDataIsolation final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "AutomationUserDataIsolation";

        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 정한 뿌리입니다(시나리오 실행이 아니거나 OS 폴더를 그대로 쓰면 빈 글). */
        const string& getRoot() const { return _root; }
        /** @brief 사람의 데이터 폴더(OS 규칙 · 활성 게임) 경로입니다. 모르면 빈 글. */
        const AbsolutePath& getPlatformFolder() const { return _platformFolder; }
        /** @brief 기동 때 찍은 사람의 데이터 폴더입니다. */
        const UserDataFolderStamp& getPlatformStartStamp() const { return _platformStartStamp; }

    private:
        /** @brief 거절을 알리고 로드 오류(11)로 끝낼 종료 코드를 건 뒤 실패를 돌려줍니다. */
        static SubsystemInitResult reject( const Error& error );

    private:
        string              _root{};
        AbsolutePath        _platformFolder{};
        UserDataFolderStamp _platformStartStamp{};
    };
} // namespace sw
