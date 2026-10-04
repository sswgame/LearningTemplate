/**
 * @file SourceControlProvider.h
 * @brief 에디터의 버전 관리 잠금(체크아웃) 추상 — 공급자는 **명령을 만들고 출력을 읽기만** 합니다. 띄우는 것은 `EditorSourceControl` 입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

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
    /**
     * @class ISourceControlProvider
     * @brief 버전 관리 공급자(언리얼 `ISourceControlProvider` 의 작은 판) — 잠금 목록 · 잠그기 · 풀기 명령과 그 출력 해석만 듭니다.
     * @details 공급자는 프로세스를 띄우지 않는다. 그래서 명령 문자열 · 출력 해석을 단위 시험으로 보고, 띄우기 · 기다리기는 한 자리
     *          (`EditorExternalToolJob`)에 둔다. 빈 명령은 "할 일이 없다" 이다(`NullSourceControlProvider`).
     */
    class ISourceControlProvider
    {
    public:
        virtual ~ISourceControlProvider() = default;

        /** @brief 화면에 보이는 이름입니다("Git LFS" · "None"). */
        virtual string_view getName() const = 0;
        /** @brief 잠그기 · 풀기를 할 수 있으면 true 입니다. */
        virtual bool canLock() const = 0;
        /** @brief 잠금 목록을 묻는 명령입니다. 비면 묻지 않습니다. */
        virtual string makeRefreshCommand() const = 0;
        /** @brief 잠금 목록 명령의 출력을 읽습니다. 형식이 틀리면 false 이고 @p outListLock 은 비어 있습니다. */
        [[nodiscard]] virtual bool parseRefreshOutput( const vector<string>& listLine, vector<SourceControlLock>& outListLock ) const = 0;
        /** @brief 파일 하나(저장소 기준 경로)를 잠그는 명령입니다. 비면 잠글 수 없습니다. */
        virtual string makeLockCommand( string_view repositoryPath ) const = 0;
        /** @brief 파일 하나의 잠금을 푸는 명령입니다. 비면 풀 수 없습니다. */
        virtual string makeUnlockCommand( string_view repositoryPath ) const = 0;
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
        string             makeRefreshCommand() const override { return {}; }
        [[nodiscard]] bool parseRefreshOutput( const vector<string>& /*listLine*/, vector<SourceControlLock>& outListLock ) const override
        {
            outListLock.clear();
            return true;
        }
        string makeLockCommand( string_view /*repositoryPath*/ ) const override { return {}; }
        string makeUnlockCommand( string_view /*repositoryPath*/ ) const override { return {}; }
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
        string             makeRefreshCommand() const override;
        [[nodiscard]] bool parseRefreshOutput( const vector<string>& listLine, vector<SourceControlLock>& outListLock ) const override;
        string             makeLockCommand( string_view repositoryPath ) const override;
        string             makeUnlockCommand( string_view repositoryPath ) const override;

        /** @brief 공급자를 쓸 수 있는지 묻는 명령(`git lfs version`)입니다 — 종료 코드 0 이면 씁니다. */
        static string makeProbeCommand();
    };
} // namespace sw::editor
