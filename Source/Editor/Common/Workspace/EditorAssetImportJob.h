/**
 * @file EditorAssetImportJob.h
 * @brief 바뀐 원본을 워커에서 임포트하는 에디터 핫 리로드의 백그라운드 잡입니다.
 * @details 핫 리로드(`AssetHotReload`, Workspace 층)가 쓰므로 같은 층에 둔다. 공통 규약(락, 세대, 완료 플래그)은 `EditorBackgroundTask` 에 있다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Task/EditorBackgroundTask.h"

namespace sw
{
    class TaskArgs;
} // namespace sw

namespace sw::editor
{
    /** @brief 임포트 잡이 워커에서 부를 임포터와 그 원본입니다. 임포터는 `AssetSourceImporterFunc` 이며 UI 와 공유 상태를 건드리지 않는 것만 올립니다. */
    struct EditorAssetImportItem
    {
        string _relativePath; ///< 리소스 루트 기준 원본 경로
        bool ( *_pfnImport )( string_view relativePath ){ nullptr };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 임포트 잡 한 번의 입력입니다. 목록 순서대로 하나씩 임포트합니다. */
    struct EditorAssetImportInput
    {
        vector<EditorAssetImportItem> _listItem;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 원본 하나의 임포트 결과입니다. */
    struct EditorAssetImportItemResult
    {
        string _relativePath;
        bool   _bRan{ false };     ///< 임포터를 불렀다(취소로 건너뛰었으면 false)
        bool   _bHandled{ false }; ///< 임포터가 원본으로 처리했다(false 면 호출한 쪽이 캐시를 다시 읽어야 한다)
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 임포트 잡 한 번의 결과입니다. 입력 순서와 같습니다. */
    struct EditorAssetImportResult
    {
        vector<EditorAssetImportItemResult> _listItem;
        bool                                _bCanceled{ false }; ///< 취소를 만나 남은 원본을 건너뛰었다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorAssetImportJob
     * @brief 바뀐 원본을 워커에서 임포트하고 에디터 스레드가 `take()` 로 결과를 가져갑니다. 에디터 핫 리로드가 쓴다.
     * @details 임포트는 몇 초에서 몇 분 걸린다 — 에디터 스레드에서 동기로 돌면 그동안 프레임이 멈춘다. 결과 적용(캐시 리로드 · 씬 알림)은 가져간 쪽(에디터 스레드)이 한다.
     *          **한 번에 하나만 돈다**(`request` 가 false 면 앞 잡이 아직 돈다). 두 워커가 같은 결과 자리(`.mesh` 와 옆 폴더, `import.stamp`)를 쓰면 서로 지우고 쓰기 때문이다.
     *          그래서 실행 중에 원본이 또 바뀌면 `cancel` 로 앞 잡이 다음 원본 전에 멈추게 하고(진행 중인 원본 하나는 끝까지 간다), 끝난 뒤 다시 요청한다.
     *          `take()` 가 `Ready` 를 주기 전에 다시 `request` 하지 않는다(안 가져간 결과가 사라진다).
     */
    class EditorAssetImportJob final : public EditorBackgroundTask<EditorAssetImportInput, EditorAssetImportResult>
    {
    public:
        EditorAssetImportJob();

        /** @brief 워커에 임포트를 요청합니다. 앞 잡이 아직 돌면 아무것도 하지 않고 false 입니다. */
        [[nodiscard]] bool request( vector<EditorAssetImportItem> listItem );

        /** @brief 도는 잡이 다음 원본을 임포트하기 전에 멈추게 합니다(이미 끝났으면 아무 일도 없다). 건너뛴 원본은 결과에 `_bRan == false` 로 남는다. */
        void cancel();

    private:
        static void runJob( const TaskArgs& args );

        shared_ptr<atomic<bool>> _pCancel; ///< 지금 요청의 취소 표시. 워커는 자기 요청의 표시를 쥔다
    };
} // namespace sw::editor
