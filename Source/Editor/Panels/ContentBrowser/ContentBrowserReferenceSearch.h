/**
 * @file ContentBrowserReferenceSearch.h
 * @brief 콘텐츠 브라우저의 참조 찾기입니다. 참조 역색인을 워커에서 최신으로 유지하고 Find References 와 Show Dependencies 의 결과를 가집니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Commands/Asset/EditorReferenceIndex.h"
#include "Editor/Common/Commands/EditorBackgroundIO.h"

namespace sw::editor
{
    class AssetHotReload;

    /**
     * @class ContentBrowserReferenceSearch
     * @brief 참조 역색인과 마지막 질의의 결과입니다. 언리얼 Content Browser 의 Reference Viewer 를 테이블 하나로 줄인 것입니다.
     * @details 역색인은 처음 한 번, 그리고 `Content/` 의 변경 번호가 바뀔 때마다 워커에서 다시 스캔한다. 스캔은 하나씩만 돌린다.
     *          질의와 결과는 UI 스레드에서만 다룬다(`EditorReferenceIndex` 의 규칙).
     *          "색인이 있다"(`EditorReferenceIndex::isReady`)와 "색인이 지금 디스크와 맞다"(`isCurrent`)는 다르다. 스캔은 시작한 순간의 트리를 읽으므로,
     *          스캔 도중에 바뀐 파일(시나리오의 `<Scratch>` 복사, 다른 저장)은 그 결과에 없을 수 있다. 결과를 넣은 뒤에도 변경 번호가 앞서 있으면 다시 스캔하고,
     *          그 사이에는 `isCurrent` 가 false 다. 답이 맞아야 하는 쪽(옮기기 · 이름 바꾸기, 자동화 감시 값, 시계 잡기 조건)은 `isCurrent` 를 본다.
     *          남는 틈: 에디터 밖의 쓰기(`<Scratch>` 복사 · 탐색기)는 OS 파일 감시가 알려야 변경 번호가 오른다. 그 알림이 오기 전에는 `isCurrent` 가 낡은 색인을 맞다고 본다
     *          (보통 몇 ms — 스캔 하나(Release 수 초, Debug 수십 초)보다 짧다). 에디터 자신의 이동은 `EditorAssetFileCommands::moveAsset` 이 색인을 그 자리에서 고친다.
     *          시나리오의 사본 쓰기는 시작 차단 조건(`AssetHotReload::pollContentWatchFence`)이 감시 사건을 기다려 막는다.
     *
     *          **증분 갱신**: 색인이 있고 스캔이 돌지 않을 때 변경 번호가 오르면, 바뀐 파일의 목록(`AssetHotReload::takeChangedPaths`)이 모두 "있던 파일의 내용 변경"이면
     *          그 파일들만 다시 읽는다(`EditorReferenceIndex::refreshFile`). 파일이 생기거나 사라지거나(색인이 아는 파일 목록이 달라진다) 폴더가 바뀌었거나 목록을 모르면 전체를 다시 스캔한다 —
     *          없던 파일이 생기면 다른 파일의 참조가 "있는 파일" 로 바뀌므로 한 파일만 읽어서는 맞지 않다.
     */
    class ContentBrowserReferenceSearch
    {
    public:
        /** @brief 아직 스캔하지 않은 상태로 만듭니다. */
        ContentBrowserReferenceSearch();

        /**
         * @brief 워커가 끝낸 스캔이 있으면 역색인에 넣고, 처음이거나 @p contentChangeSerial 이 마지막 스캔 때와 다르면 스캔을 요청합니다. 스캔 중이면 끝날 때까지 미룹니다.
         * @details 결과 꺼내기와 다시 요청할지의 판단은 같은 `take` 결과 하나로 한다 — 따로 다시 읽으면 그 사이 끝난 결과를 버린다.
         */
        void update( uint64 contentChangeSerial, AssetHotReload* pChangeSource );
        /**
         * @brief 진행 중이거나 끝났지만 아직 넣지 않은 스캔이 있으면 버리고 새로 요청합니다. 이동이 고친 색인을 이전 스캔 결과가 덮어쓰지 않게 합니다.
         * @details 버리기만 하면 그 스캔이 담은 다른 변경(에디터 밖의 쓰기)도 함께 사라진다. 그래서 새 스캔으로 다시 읽고, 끝날 때까지 `isCurrent` 는 false 다.
         */
        void restartPendingScan( uint64 contentChangeSerial );
        /** @brief 색인이 있고, 넣지 않은 스캔이 없고, 마지막 스캔 뒤로 @p contentChangeSerial 이 오르지 않았으면 true 입니다. */
        bool isCurrent( uint64 contentChangeSerial ) const;
        /**
         * @brief 에셋의 참조를 찾아 결과를 바꿉니다.
         * @param bDependencies false 면 그 에셋을 사용하는 쪽(Find References)을, true 면 그 에셋이 사용하는 쪽(Show Dependencies)을 찾습니다.
         */
        void query( string_view absolutePath, bool bDependencies );

        /** @brief 참조 역색인을 반환합니다. */
        const EditorReferenceIndex& getIndex() const { return _index; }
        /**
         * @brief 이름 바꾸기와 옮기기가 참조를 고치며 갱신할 역색인입니다. `isCurrent` 가 아니면 nullptr 입니다.
         * @details 옮기기는 색인이 아는 참조자만 고친다. 스캔 전의 색인으로 옮기면 그 뒤에 생긴 참조자(사본 폴더의 씬)를 고치지 않고 "0 개 고침" 으로 끝난다 —
         *          그래서 고치는 쪽은 맞는 색인만 받는다.
         */
        EditorReferenceIndex* findCurrentIndex( uint64 contentChangeSerial ) { return isCurrent( contentChangeSerial ) ? &_index : nullptr; }
        /** @brief 역색인 스캔이 워커에서 진행 중이면 true 입니다. */
        bool isScanPending() const { return _job.isPending(); }
        /** @brief 마지막 질의의 대상 리소스 id 를 반환합니다. */
        const string& getQueryID() const { return _queryID; }
        /** @brief 마지막 질의가 Show Dependencies 였으면 true 입니다. */
        bool isDependencyQuery() const { return _bDependencyQuery == SW_TRUE; }
        /** @brief 마지막 질의의 결과 행을 반환합니다. */
        const vector<EditorAssetReference>& getResultList() const { return _listResult; }

    private:
        /** @brief @p contentChangeSerial 시점의 트리를 읽는 스캔을 요청합니다. 그 결과를 넣을 때까지 `isCurrent` 는 false 입니다. */
        void requestScan( uint64 contentChangeSerial );
        /**
         * @brief 바뀐 파일 @p listChangedPath 가 모두 색인이 아는 파일의 내용 변경이면 그 파일들만 다시 읽고 true 입니다. 파일이 생기거나 사라졌으면 아무것도 하지 않고 false 입니다(전체를 다시 스캔한다).
         */
        [[nodiscard]] bool tryRefreshChangedFiles( const vector<string>& listChangedPath );

    private:
        EditorReferenceIndex         _index;
        EditorReferenceIndexJob      _job;
        AssetHotReload*              _pChangeSource; ///< 바뀐 파일을 알려 주는 쪽(`update` 가 받아 둔다). 없으면 늘 전체를 스캔한다
        string                       _queryID;       ///< 마지막 질의의 대상 리소스 id
        vector<EditorAssetReference> _listResult;
        uint64                       _scanSerial;           ///< 마지막으로 요청한 스캔이 시작할 때의 `AssetHotReload::getContentChangeSerial` 값
        uint8                        _bScanRequested   : 1; ///< 스캔을 한 번이라도 요청했다
        uint8                        _bScanInFlight    : 1; ///< 요청한 스캔의 결과를 아직 색인에 넣지 않았다(도는 중이거나 끝나서 꺼내기를 기다린다)
        uint8                        _bDependencyQuery : 1; ///< 마지막 질의가 Show Dependencies 였다
        [[maybe_unused]] uint8       _reservedFlags    : 5;
    };
} // namespace sw::editor
