/**
 * @file EditorDocumentPanel.h
 * @brief 포커스 애셋 경로와 연동되는 온디맨드 도구 패널
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Common/Workspace/EditorAssetType.h"

namespace sw::editor
{
    /**
     * @class EditorDocumentPanel
     * @brief 워크스페이스 포커스 경로가 지정 애셋 종류와 맞으면 문서를 바꿉니다.
     */
    class EditorDocumentPanel : public IEditorPanel
    {
    public:
        bool        isToolPanel() const override { return true; }
        const utf8* getPanelTitle() const override;

    protected:
        /**
         * @param kind 이 패널이 다루는 애셋 종류
         * @param bLoadOnOpen true면 포커스가 없어도 첫 draw에서 로드를 요청합니다 (기본 문서).
         */
        explicit EditorDocumentPanel( EditorAssetType kind, bool bLoadOnOpen );

        /** @brief 포커스가 이 종류이고 현재 로드 경로와 다르면 true입니다. */
        bool hasNewFocusedDocument() const;
        /** @brief 매칭된 포커스 경로입니다. 매칭이 없으면 empty입니다. */
        string_view getMatchingFocusedPath() const;
        /** @brief 포커스 경로를 로드 경로로 확정하고 로드가 필요함을 표시합니다. */
        void acceptFocusedDocument();
        /** @brief dirty면 확인 팝업, 아니면 포커스 경로를 받아들입니다. */
        void updateFocusedDocument();

        const string& getLoadedAssetPath() const { return _loadedAssetPath; }
        bool          isDocumentLoaded() const;
        bool          isDocumentLoadFailed() const { return _bLoadFailed == SW_TRUE; }
        bool          canSaveDocument() const override { return _bLoadFailed == SW_FALSE; }

        /**
         * @brief 이 패널의 문서를 (다시) 읽어 내용을 채웁니다. **읽음 · 못 읽음 표시는 기반이 결과로 합니다**(`reloadDocument`) — 패널은 표시하지 않습니다.
         * @details `Missing` 은 새 문서(기본값으로 시작하고 저장하면 만든다), `Malformed` 는 저장을 막는다(읽지 못한 파일을 앞 문서로 덮지 않게).
         *          패널이 "읽었다" 표시를 직접 부르지 않는다 — 읽기 결과를 버리고 늘 읽었다고 표시하는 일을 막는다.
         */
        virtual ToolAssetLoadResult loadDocument() = 0;
        /** @brief 문서를 읽고 결과를 표시합니다. 첫 그리기(`ensureDocumentLoaded`)와 패널의 Load · Reload 단추가 부릅니다. */
        void reloadDocument();
        /** @brief 아직 읽지 않았으면 읽습니다. 그리기 머리(`updateFocusedDocument` 다음)에서 부릅니다. */
        void ensureDocumentLoaded();

        /**
         * @brief 현재 문서를 디스크에 저장합니다. 성공하면 true입니다.
         * @details 기반(IEditorPanel)에는 "문서 없음" 기본 구현이 있지만, 문서 패널은 반드시 구현해야 하므로 여기서 다시 순수
         *          가상으로 선언합니다. 구현을 잊으면 컴파일이 막힙니다.
         */
        [[nodiscard]] bool saveDocument() override = 0;
        /** @brief 편집 단위 Undo를 남기고 dirty로 표시합니다. */
        void notifyDocumentEdited( string_view label, string_view coalesceKey = {} );
        /** @brief 로드/저장 직후 Undo 기준 텍스트를 맞춥니다. */
        void syncDocumentUndoBaseline();
        /** @brief 현재 문서를 텍스트로 직렬화합니다. */
        virtual string captureDocumentText() const { return {}; }
        /** @brief 텍스트 스냅샷을 문서에 적용합니다. */
        virtual void applyDocumentText( string_view /*text*/ ) {}

        template <typename TItem>
        static int32 nextItemId( const vector<TItem>& list )
        {
            int32 maxId{ 0 };
            for ( const TItem& item : list )
            {
                maxId = MathUtil::max( maxId, item._id );
            }
            return maxId + 1;
        }

    private:
        void drawUnsavedDocumentPopup();
        void restoreDocumentFromUndo( string_view text );
        /** @brief 읽었다(또는 새 문서) — 저장을 허용하고 되돌리기 기준을 맞춥니다. */
        void markDocumentLoaded();
        /** @brief 읽지 못했다 — 다시 읽으려 하지 않고(`isDocumentLoaded`), **저장을 막습니다**(`canSaveDocument`). 다른 문서로 바꾸거나 다시 읽어 성공하면 풀린다. */
        void markDocumentLoadFailed( string_view reason );

        EditorAssetType        _kind;
        string                 _loadedAssetPath;
        string                 _pendingFocusPath;
        string                 _documentUndoBaseline;
        string                 _lastSavedDocumentText;
        uint8                  _bLoaded        : 1;
        uint8                  _bConfirmSwitch : 1;
        uint8                  _bLoadFailed    : 1;
        [[maybe_unused]] uint8 _reserved       : 5;
    };
} // namespace sw::editor
