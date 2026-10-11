/**
 * @file EditorSourceControl.h
 * @brief 에디터의 체크아웃(잠금)을 담당합니다. 공급자를 고르고, 잠금 목록을 보관하며, 사용자가 선택한 파일만 잠그거나 풉니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/SourceControl/SourceControlProvider.h"

namespace sw::editor
{
    class EditorExternalToolJob;

    /** @brief 지금 실행 중이거나 대기 중인 버전 관리 작업의 종류입니다. */
    enum class SourceControlOperation : uint8
    {
        None = 0,
        Probe,   ///< 공급자를 쓸 수 있는지 확인한다(`git lfs version`)
        Refresh, ///< 잠금 목록을 다시 읽는다
        Lock,    ///< 파일 하나를 잠근다
        Unlock,  ///< 파일 하나의 잠금을 해제한다
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
    class EditorSourceControl
    {
    public:
        /** @brief @p repositoryRoot 를 작업 폴더로 씁니다. 비어 있으면 공급자를 확인하지 않습니다. */
        explicit EditorSourceControl( string_view repositoryRoot );
        ~EditorSourceControl();

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

    private:
        /** @brief 순서를 기다리는 작업 하나입니다. */
        struct PendingOperation
        {
            SourceControlOperation _operation{ SourceControlOperation::None };
            string                 _repositoryPath;
        };

        void applyResult( const PendingOperation& operation, int32 exitCode, bool bLaunched, const vector<string>& listLine );
        void enqueue( SourceControlOperation operation, string_view repositoryPath );

        string                             _repositoryRoot;
        unique_ptr<ISourceControlProvider> _pProvider;
        unique_ptr<EditorExternalToolJob>  _pJob;
        vector<SourceControlLock>          _listLock;
        vector<PendingOperation>           _listPending;
        PendingOperation                   _running;
    };
} // namespace sw::editor
