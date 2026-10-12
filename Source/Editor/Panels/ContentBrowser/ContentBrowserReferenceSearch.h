/**
 * @file ContentBrowserReferenceSearch.h
 * @brief 콘텐츠 브라우저의 참조 찾기입니다. 참조 역색인을 워커에서 최신으로 유지하고 Find References 와 Show Dependencies 의 결과를 가집니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Commands/EditorBackgroundIO.h"
#include "Editor/Common/Commands/EditorReferenceIndex.h"

namespace sw::editor
{
    /**
     * @class ContentBrowserReferenceSearch
     * @brief 참조 역색인과 마지막 질의의 결과입니다. 언리얼 Content Browser 의 Reference Viewer 를 표 하나로 줄인 것입니다.
     * @details 역색인은 처음 한 번, 그리고 `Resource/` 의 변경 번호가 바뀔 때마다 워커에서 다시 스캔한다. 스캔은 하나씩만 돌린다.
     *          질의와 결과는 UI 스레드에서만 다룬다(`EditorReferenceIndex` 의 규칙).
     */
    class ContentBrowserReferenceSearch
    {
    public:
        /** @brief 아직 스캔하지 않은 상태로 만듭니다. */
        ContentBrowserReferenceSearch();

        /** @brief 워커가 끝낸 스캔이 있으면 역색인에 넣습니다. */
        void takeFinishedScan();
        /** @brief 처음이거나 @p contentChangeSerial 이 마지막 스캔 때와 다르면 스캔을 요청합니다. 스캔 중이면 끝날 때까지 미룹니다. */
        void requestScanIfStale( uint64 contentChangeSerial );
        /** @brief 진행 중인 스캔이 있으면 다시 요청해 그 결과를 버립니다. 이동이 고친 색인을 이전 스캔 결과가 덮어쓰지 않게 합니다. */
        void discardPendingScan();
        /**
         * @brief 에셋의 참조를 찾아 결과를 바꿉니다.
         * @param bDependencies false 면 그 에셋을 사용하는 쪽(Find References)을, true 면 그 에셋이 사용하는 쪽(Show Dependencies)을 찾습니다.
         */
        void query( string_view absolutePath, bool bDependencies );

        /** @brief 참조 역색인을 반환합니다. */
        const EditorReferenceIndex& getIndex() const { return _index; }
        /** @brief 이름 바꾸기와 옮기기가 참조를 고치며 갱신할 역색인을 반환합니다. */
        EditorReferenceIndex& getIndex() { return _index; }
        /** @brief 역색인 스캔이 워커에서 진행 중이면 true 입니다. */
        bool isScanPending() const { return _job.isPending(); }
        /** @brief 마지막 질의의 대상 리소스 id 를 반환합니다. */
        const string& getQueryID() const { return _queryID; }
        /** @brief 마지막 질의가 Show Dependencies 였으면 true 입니다. */
        bool isDependencyQuery() const { return _bDependencyQuery == SW_TRUE; }
        /** @brief 마지막 질의의 결과 행을 반환합니다. */
        const vector<EditorAssetReference>& getResultList() const { return _listResult; }

    private:
        EditorReferenceIndex         _index;
        EditorReferenceIndexJob      _job;
        string                       _queryID; ///< 마지막 질의의 대상 리소스 id
        vector<EditorAssetReference> _listResult;
        uint64                       _scanSerial;           ///< 스캔을 요청할 때의 `AssetHotReload::getContentChangeSerial` 값
        uint8                        _bScanRequested   : 1; ///< 스캔을 한 번이라도 요청했다
        uint8                        _bDependencyQuery : 1; ///< 마지막 질의가 Show Dependencies 였다
        [[maybe_unused]] uint8       _reservedFlags    : 6;
    };
} // namespace sw::editor
