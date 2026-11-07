/**
 * @file SourceControlProvider.h
 * @brief 에디터의 버전 관리 잠금(체크아웃) 추상 — 공급자는 **명령을 만들고 출력을 읽기만** 합니다. 띄우는 것은 `EditorSourceControl` 입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Process/Process.h"

namespace sw::editor
{
    /** @brief 잠긴 파일 하나입니다. 경로는 저장소 루트 기준 `/` 구분입니다. */
    struct SourceControlLock
    {
        string _path;
        string _owner;
        string _id;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 작업 트리의 바뀐 파일 하나의 상태입니다(git porcelain 의 두 글자를 뜻으로 바꾼 것). */
    enum class SourceControlChangeKind : uint8
    {
        Modified,  ///< 고쳤다
        Added,     ///< 새로 더했다(색인에 올렸다)
        Deleted,   ///< 지웠다
        Renamed,   ///< 이름을 바꿨다(`_originalPath` 가 옛 경로)
        Untracked, ///< 버전 관리 밖의 새 파일 — 되돌리기는 파일을 지우는 일이라 하지 않는다
        Conflicted ///< 병합 충돌 — 손으로 푼다
    };

    /** @brief 바뀐 파일 하나입니다. 경로는 저장소 루트 기준 `/` 구분입니다. */
    struct SourceControlChange
    {
        string                  _path;
        string                  _originalPath; ///< 이름 바꾸기의 옛 경로. 나머지는 비어 있다
        SourceControlChangeKind _kind{ SourceControlChangeKind::Modified };
        bool                    _bStaged{ false }; ///< 색인에 올렸다(커밋 대기) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /**
     * @struct GitChangeCommands
     * @brief git 작업 트리 질의 · 되돌리기 명령과 출력 해석입니다(git 공급자 둘이 함께 쓴다). 프로세스를 띄우지 않으므로 단위 테스트가 본다.
     * @details 지원 범위: 바뀐 파일 목록(`status`), 파일 하나의 diff(HEAD 대비 — 색인 · 작업 트리 모두), 파일 하나의 이력(최근 20 커밋), 추적 파일 되돌리기
     *          (`restore --staged --worktree`, HEAD 로). 커밋 · 푸시 · 브랜치 · 병합은 하지 않는다(외부 계정 · 서버가 필요한 일은 쓰는 사람의 도구로).
     */
    struct GitChangeCommands
    {
        /** @brief 이력에 보일 커밋 수입니다. */
        static constexpr int32 kHistoryCount = 20;

        static ProcessCommand makeStatusCommand();
        /** @brief `git status --porcelain=v1` 출력을 읽습니다. 모르는 줄이 있으면 false 이고 @p outListChange 는 비어 있습니다. */
        [[nodiscard]] static bool parseStatusOutput( const vector<string>& listLine, vector<SourceControlChange>& outListChange );
        static ProcessCommand     makeDiffCommand( string_view repositoryPath );
        static ProcessCommand     makeHistoryCommand( string_view repositoryPath );
        static ProcessCommand     makeRevertCommand( string_view repositoryPath );
        /** @brief 이 변경을 되돌릴 수 있으면 true 입니다 — 추적 파일의 고침 · 지움 · 이름 바꾸기 · 더하기(색인)만. 추적 밖 파일과 충돌은 되돌리지 않는다. */
        static bool canRevert( const SourceControlChange& change );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class ISourceControlProvider
     * @brief 버전 관리 공급자(언리얼 `ISourceControlProvider` 의 작은 버전) — 잠금 목록 · 잠그기 · 풀기 명령과 그 출력 해석만 듭니다.
     * @details 공급자는 프로세스를 띄우지 않는다. 그래서 명령(실행 파일 · 인자) · 출력 해석을 단위 테스트로 보고, 띄우기 · 기다리기는 한 자리
     *          (`EditorExternalToolJob`)에 둔다. 실행 파일이 빈 명령은 "할 일이 없다" 이다(`NullSourceControlProvider`).
     */
    class ISourceControlProvider
    {
    public:
        virtual ~ISourceControlProvider() = default;

        /** @brief 화면에 보이는 이름입니다("Git LFS" · "None"). */
        virtual string_view getName() const = 0;
        /** @brief 잠그기 · 풀기를 할 수 있으면 true 입니다. */
        virtual bool canLock() const = 0;
        /** @brief 잠금 목록을 묻는 명령입니다. 실행 파일이 비면 묻지 않습니다. */
        virtual ProcessCommand makeRefreshCommand() const = 0;
        /** @brief 잠금 목록 명령의 출력을 읽습니다. 형식이 틀리면 false 이고 @p outListLock 은 비어 있습니다. */
        [[nodiscard]] virtual bool parseRefreshOutput( const vector<string>& listLine, vector<SourceControlLock>& outListLock ) const = 0;
        /** @brief 파일 하나(저장소 기준 경로)를 잠그는 명령입니다. 실행 파일이 비면 잠글 수 없습니다. */
        virtual ProcessCommand makeLockCommand( string_view repositoryPath ) const = 0;
        /** @brief 파일 하나의 잠금을 푸는 명령입니다. 실행 파일이 비면 풀 수 없습니다. */
        virtual ProcessCommand makeUnlockCommand( string_view repositoryPath ) const = 0;
        /** @brief 바뀐 파일 목록 · diff · 이력 · 되돌리기를 할 수 있으면 true 입니다(git 공급자). 명령은 `GitChangeCommands` 다. */
        virtual bool canTrackChanges() const = 0;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 버전 관리가 없을 때의 공급자입니다 — 아무것도 하지 않습니다. 읽기 전용 표시는 파일 속성만으로 합니다. */
    class NullSourceControlProvider final : public ISourceControlProvider
    {
    public:
        string_view        getName() const override { return "None"; }
        bool               canLock() const override { return false; }
        ProcessCommand     makeRefreshCommand() const override { return {}; }
        [[nodiscard]] bool parseRefreshOutput( const vector<string>& /*listLine*/, vector<SourceControlLock>& outListLock ) const override
        {
            outListLock.clear();
            return true;
        }
        ProcessCommand makeLockCommand( string_view /*repositoryPath*/ ) const override { return {}; }
        ProcessCommand makeUnlockCommand( string_view /*repositoryPath*/ ) const override { return {}; }
        bool           canTrackChanges() const override { return false; }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief LFS 잠금 없이 git 만 있을 때의 공급자입니다 — 잠그기는 없고 바뀐 파일 · diff · 이력 · 되돌리기만 한다.
     * @details 예전에는 git LFS 가 없으면 버전 관리 기능이 모두 꺼졌다. 잠금은 LFS 가 있어야 하지만 작업 트리 질의는 git 하나로 된다.
     */
    class GitSourceControlProvider final : public ISourceControlProvider
    {
    public:
        string_view        getName() const override { return "Git"; }
        bool               canLock() const override { return false; }
        ProcessCommand     makeRefreshCommand() const override { return {}; }
        [[nodiscard]] bool parseRefreshOutput( const vector<string>& /*listLine*/, vector<SourceControlLock>& outListLock ) const override
        {
            outListLock.clear();
            return true;
        }
        ProcessCommand makeLockCommand( string_view /*repositoryPath*/ ) const override { return {}; }
        ProcessCommand makeUnlockCommand( string_view /*repositoryPath*/ ) const override { return {}; }
        bool           canTrackChanges() const override { return true; }
        /** @brief 공급자를 쓸 수 있는지 묻는 명령(`git --version`)입니다 — 종료 코드 0 이면 씁니다. */
        static ProcessCommand makeProbeCommand();
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief git LFS 잠금(`git lfs lock` · `unlock` · `locks --json`)을 쓰는 공급자입니다.
     * @details `lockable` 로 표시한 파일(.gitattributes)은 잠그기 전까지 git 이 읽기 전용으로 둔다 — 에디터는 그 속성을 읽기 전용 표시로 보여 준다.
     *          잠금 서버가 없으면 명령이 실패하고 그 출력이 로그로 간다. 이 에디터는 스스로 잠그지 않는다(사용자가 고른 파일만).
     */
    class GitLfsSourceControlProvider final : public ISourceControlProvider
    {
    public:
        string_view        getName() const override { return "Git LFS"; }
        bool               canLock() const override { return true; }
        ProcessCommand     makeRefreshCommand() const override;
        [[nodiscard]] bool parseRefreshOutput( const vector<string>& listLine, vector<SourceControlLock>& outListLock ) const override;
        ProcessCommand     makeLockCommand( string_view repositoryPath ) const override;
        ProcessCommand     makeUnlockCommand( string_view repositoryPath ) const override;
        bool               canTrackChanges() const override { return true; }

        /** @brief 공급자를 쓸 수 있는지 묻는 명령(`git lfs version`)입니다 — 종료 코드 0 이면 씁니다. */
        static ProcessCommand makeProbeCommand();
    };
} // namespace sw::editor
