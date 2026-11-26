/**
 * @file EditorAssetImportJob.h
 * @brief 바뀐 원본을 워커에서 임포트하는 에디터 핫 리로드의 백그라운드 잡입니다.
 * @details 핫 리로드(`AssetHotReload`, Workspace 층)가 쓰므로 같은 층에 둔다. 공통 규약(락, 세대, 완료 플래그)은 `EditorBackgroundJob` 에 있다.
 */
#pragma once
#include "Core/Concurrency/StopToken.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Job/EditorBackgroundJob.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

namespace sw
{
    class AssetPaths;
    class TaskArgs;
    class TaskManager;
} // namespace sw

namespace sw::editor
{
    /** @brief 임포트 잡이 워커에서 부를 임포터와 그 원본입니다. 임포터는 `AssetSourceBatchImporterFunc` 이며 UI 와 공유 상태를 건드리지 않는 것만 올립니다. */
    struct EditorAssetImportItem
    {
        string                       _relativePath; ///< 리소스 루트 기준 원본 경로
        AssetSourceBatchImporterFunc _pfnImport{ nullptr };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 임포트 잡 한 번의 입력입니다. 같은 임포터의 원본은 한 번에 넘깁니다(`AssetSourceBatch`). */
    struct EditorAssetImportInput
    {
        vector<EditorAssetImportItem> _listItem;
        const AssetPaths*             _pAssetPaths{ nullptr };  ///< 임포터에 넘길 경로 상태(엔진 수명 — 잡보다 오래 산다). 워커는 찾지 않고 요청한 스레드가 넣는다
        TaskManager*                  _pTaskManager{ nullptr }; ///< 원본을 나눠 돌 작업 시스템(빌림 — 요청한 스레드가 찾는다). nullptr 이면 차례로 돈다
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
    class EditorAssetImportJob final : public EditorBackgroundJob<EditorAssetImportInput, EditorAssetImportResult>
    {
    public:
        EditorAssetImportJob();

        /**
         * @brief 워커에 임포트를 요청합니다. @p assetPaths 는 임포터마다 넘긴다(에디터 스레드가 찾는다). 앞 잡이 아직 돌면 아무것도 하지 않고 false 입니다.
         * @param pTaskManager 원본을 나눠 돌 작업 시스템(선택 — 없으면 차례로 돈다). 워커는 에디터 서비스 범위를 읽지 않아 요청한 스레드가 넘긴다.
         */
        [[nodiscard]] bool request( vector<EditorAssetImportItem> listItem, const AssetPaths& assetPaths, TaskManager* pTaskManager );

        /**
         * @brief @p listItem 을 임포터마다 한 묶음으로 나눕니다(값은 입력 목록의 번호, 묶음 순서는 임포터가 처음 나온 순서, 묶음 안은 입력 순서). 묶음은 차례로 돌고 묶음 안의 병렬은 임포터가 합니다.
         * @details 임포터 하나의 원본을 폴더로 쪼개지 않는다 — 한 원본 폴더의 `import.stamp` 는 하위 폴더까지 한 파일이고, 다른 폴더의 원본 결과를 읽는 원본이 있다(`IRawAssetImporter::hasOrderDependency`).
         */
        [[nodiscard]] static vector<vector<uint32>> planBatches( const vector<EditorAssetImportItem>& listItem );

        /** @brief 도는 잡이 다음 원본을 임포트하기 전에 멈추게 합니다(이미 끝났으면 아무 일도 없다). 건너뛴 원본은 결과에 `_bRan == false` 로 남는다. */
        void cancel();

    private:
        static void runJob( const TaskArgs& args );

        StopSource _cancel; ///< 지금 요청의 취소. 워커는 자기 요청의 토큰을 쥔다
    };
} // namespace sw::editor
