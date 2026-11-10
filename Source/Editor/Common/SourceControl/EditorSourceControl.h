/**
 * @file EditorSourceControl.h
 * @brief 에디터의 체크아웃(잠금)을 담당합니다. 공급자를 고르고, 잠금 목록을 보관하며, 사용자가 선택한 파일만 잠그거나 풉니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/SourceControl/SourceControlProvider.h"

namespace sw::editor
{
    class EditorExternalToolJob;

    /** @brief 지금 실행 중이거나 대기 중인 버전 관리 작업의 종류입니다. */
    enum class SourceControlOperation : uint8
    {
        None = 0,
        Probe,    ///< 공급자를 쓸 수 있는지 확인한다(`git lfs version`)
        Refresh,  ///< 잠금 목록을 다시 읽는다
        Lock,     ///< 파일 하나를 잠근다
        Unlock,   ///< 파일 하나의 잠금을 해제한다
        ProbeGit, ///< git LFS 가 없을 때 git 만 쓸 수 있는지 확인한다(`git --version`)
        Status,   ///< 바뀐 파일 목록을 다시 읽는다
        Diff,     ///< 파일 하나의 diff(HEAD 대비)를 읽는다
        History,  ///< 파일 하나의 최근 커밋을 읽는다
        Revert,   ///< 추적 파일 하나를 HEAD 로 되돌린다(색인 · 작업 트리)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorSourceControl
     * @brief 버전 관리 공급자 하나와 잠금 목록을 보관합니다. 처음에는 git LFS 를 쓸 수 있는지 확인하고, 안 되면 아무것도 하지 않는 공급자로 남습니다.
     * @details 모든 명령은 전용 스레드(`EditorExternalToolJob`)에서 실행하고 `update()` 가 결과를 가져간다. UI 가 git 을 기다리지 않는다.
     *          작업은 한 번에 하나이고 뒤의 요청은 순서를 기다린다. 잠그기와 풀기는 사용자가 선택한 파일에만 하며, 저장이나 열기가 스스로 잠그지 않는다.
     *          언리얼의 "저장할 때 자동 체크아웃" 은 일부러 두지 않았다. 잠금 서버가 없는 저장소에서는 저장할 때마다 실패 로그가 쌓이기 때문이다.
     */
    class EditorSourceControl : public EditorSubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "EditorSourceControl";
        /** @brief @p repositoryRoot 를 작업 폴더로 씁니다. 비어 있으면 공급자를 확인하지 않습니다. */
        explicit EditorSourceControl( string_view repositoryRoot );
        ~EditorSourceControl() override;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        SubsystemInitResult initializeSubsystem() override
        {
            initialize();
            return SubsystemInitResult::Succeeded;
        }
        /** @brief 도는 도구(`git` 과 그 손자)를 끝내고 기다립니다 — 종료 시간(`EditorContext.ShutdownMs`)에 잡히도록 소멸자가 아니라 여기서 한다. */
        void        shutdownSubsystem() override;
        const utf8* getSubsystemName() const override { return kSubsystemName; }

        EditorSourceControl( const EditorSourceControl& )            = delete;
        EditorSourceControl& operator=( const EditorSourceControl& ) = delete;

        /** @brief 공급자를 확인하고 잠금 목록을 읽기 시작합니다. 비동기로 실행합니다. */
        void initialize();
        /** @brief 에디터 프레임마다 호출합니다. 끝난 작업을 반영하고 다음 작업을 시작합니다. */
        void update();

        /** @brief 잠금 목록을 다시 읽도록 요청합니다. */
        void requestRefresh();
        /** @brief 파일 하나(절대 경로)를 잠가 달라고 요청합니다. 공급자가 잠글 수 없으면 false 입니다. */
        [[nodiscard]] bool requestLock( string_view absolutePath );
        /** @brief 파일 하나(절대 경로)의 잠금을 풀어 달라고 요청합니다. 공급자가 풀 수 없으면 false 입니다. */
        [[nodiscard]] bool requestUnlock( string_view absolutePath );

        /** @brief 그 파일(절대 경로)의 잠금 정보를 반환합니다. 잠겨 있지 않으면 nullptr 입니다. */
        const SourceControlLock* findLock( string_view absolutePath ) const;
        /** @brief 현재 공급자를 반환합니다. */
        const ISourceControlProvider& getProvider() const { return *_pProvider; }
        /** @brief 작업이 실행 중이거나 대기 중이면 true 입니다. */
        bool isBusy() const;

        /**
         * @brief 파일 하나의 상태 문구(툴팁)를 만듭니다. 잠금이나 읽기 전용이 아니면 빈 문자열입니다.
         * @param bReadOnly 파일이 읽기 전용인지 나타냅니다(`FileUtil::isReadOnlyFile`). 목록을 만들 때 측정한 값을 넘깁니다.
         */
        string describeStatus( string_view absolutePath, bool bReadOnly ) const;

        /** @brief 절대 경로를 저장소 기준의 `/` 경로로 바꿉니다. 저장소 밖이면 빈 문자열입니다. */
        static string makeRepositoryPath( string_view repositoryRoot, string_view absolutePath );
        /** @brief 공급자를 바꿉니다. 테스트와 공급자 탐지 결과가 씁니다. 잠금 목록은 비웁니다. */
        void setProvider( unique_ptr<ISourceControlProvider> pProvider );
        /** @brief 잠금 목록을 바꿉니다. 테스트와 갱신 결과가 씁니다. */
        void setLocks( vector<SourceControlLock>&& listLock ) { _listLock = std::move( listLock ); }

        // ------------------------------------------------------------------------------
        // 변경 목록 · diff · 이력 · 되돌리기(git 공급자만 — `ISourceControlProvider::canTrackChanges`)
        // ------------------------------------------------------------------------------
        /** @brief 바뀐 파일 목록을 다시 읽도록 요청합니다. 공급자가 변경을 다루지 않으면 false 입니다. */
        bool requestStatus();
        /** @brief 파일 하나(저장소 기준 경로)의 diff 를 읽도록 요청합니다. 결과는 `getDetailLines` 입니다. */
        bool requestDiff( string_view repositoryPath );
        /** @brief 파일 하나의 최근 커밋을 읽도록 요청합니다. 결과는 `getDetailLines` 입니다. */
        bool requestHistory( string_view repositoryPath );
        /**
         * @brief 추적 파일 하나를 HEAD 로 되돌리도록 요청합니다(저장하지 않은 변경을 버린다 — 부르는 쪽이 먼저 확인을 받는다). 끝나면 변경 목록을 다시 읽는다.
         * @return 공급자가 변경을 다루지 않거나 그 변경이 되돌릴 수 없는 종류(추적 밖 · 충돌)면 false 입니다.
         */
        bool requestRevert( const SourceControlChange& change );
        /** @brief 지난 변경 목록입니다. */
        const vector<SourceControlChange>& getChanges() const { return _listChange; }
        /** @brief 변경 목록을 한 번이라도 읽었으면 true 입니다(빈 목록과 "아직 모름" 을 가른다). */
        bool hasStatus() const { return _bStatusKnown == SW_TRUE; }
        /** @brief 지난 diff · 이력 결과의 파일입니다. */
        const string& getDetailPath() const { return _detailPath; }
        /** @brief 지난 diff · 이력의 종류입니다(`Diff` · `History`, 없으면 `None`). */
        SourceControlOperation getDetailOperation() const { return _detailOperation; }
        /** @brief 지난 diff · 이력의 줄입니다. */
        const vector<string>& getDetailLines() const { return _listDetailLine; }
        /** @brief 마지막으로 실패한 변경 작업의 이유입니다(창이 보인다). 성공하면 비운다. */
        const string& getLastError() const { return _lastError; }
        /** @brief 변경 목록 · 상세가 바뀔 때마다 오르는 번호입니다. */
        uint32 getChangeRevision() const { return _changeRevision; }

    private:
        /** @brief 순서를 기다리는 작업 하나입니다. */
        struct PendingOperation
        {
            SourceControlOperation _operation{ SourceControlOperation::None };
            string                 _repositoryPath;
        };

        void applyResult( const PendingOperation& operation, const optional<int32>& exitCode, bool bLaunched, const vector<string>& listLine );
        void enqueue( SourceControlOperation operation, string_view repositoryPath );

    private:
        string                             _repositoryRoot;
        unique_ptr<ISourceControlProvider> _pProvider;
        unique_ptr<EditorExternalToolJob>  _pJob;
        vector<SourceControlLock>          _listLock;
        vector<PendingOperation>           _listPending;
        PendingOperation                   _running;
        vector<SourceControlChange>        _listChange;
        string                             _detailPath;
        vector<string>                     _listDetailLine;
        string                             _lastError;
        uint32                             _changeRevision;
        SourceControlOperation             _detailOperation;
        uint8                              _bStatusKnown : 1;
        [[maybe_unused]] uint8             _reserved     : 7;
    };
} // namespace sw::editor
