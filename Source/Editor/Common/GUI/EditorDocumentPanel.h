/**
 * @file EditorDocumentPanel.h
 * @brief 포커스한 애셋 경로와 연동되어 필요할 때 여는 도구 패널의 기반 클래스입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Common/Workspace/EditorAssetType.h"

namespace sw::editor
{
    /**
     * @class EditorDocumentPanel
     * @brief 워크스페이스 포커스 경로가 지정한 애셋 종류와 맞으면 문서를 바꿉니다.
     */
    class SW_EDITOR_API EditorDocumentPanel : public IEditorPanel
    {
    public:
        bool            isToolPanel() const override { return true; }
        const utf8*     getPanelTitle() const override;
        EditorDockSlot  getDefaultDockSlot() const override { return EditorDockSlot::Center; }
        EditorAssetType getDocumentKind() const override { return _kind; }

    protected:
        /**
         * @param kind 이 패널이 다루는 애셋 종류입니다.
         * @param bLoadOnOpen true 이면 포커스가 없어도 첫 draw 에서 로드를 요청합니다. 기본 문서를 여는 용도입니다.
         */
        explicit EditorDocumentPanel( EditorAssetType kind, bool bLoadOnOpen );
        /** @brief 쌓여 있는 되돌리기 항목이 사라진 패널을 부르지 않게 표지를 비웁니다(백엔드 교체는 패널을 다시 만들어도 되돌리기 스택을 남긴다). */
        ~EditorDocumentPanel() override;

        /** @brief 포커스가 이 종류이고 현재 로드 경로와 다르면 true 입니다. */
        bool hasNewFocusedDocument() const;
        /** @brief 매칭된 포커스 경로를 반환합니다. 종류가 맞고 `acceptsDocumentPath` 가 받는 경로만이며, 매칭이 없으면 빈 문자열입니다. */
        string_view getMatchingFocusedPath() const;
        /**
         * @brief 이 패널이 @p assetPath(리소스 id)를 문서로 받으면 true 입니다. 기본은 종류만 맞으면 받습니다.
         * @details 한 종류 안의 일부만 다루는 패널이 좁힌다(Data 종류 중 `*.vfx.xml` 만 보는 VFX 미리보기). 포커스 매칭과 "Open..." 목록이 같은 판정을 쓴다.
         */
        virtual bool acceptsDocumentPath( string_view assetPath ) const
        {
            (void)assetPath;
            return true;
        }
        /** @brief 포커스 경로를 로드 경로로 확정하고 로드가 필요함을 표시합니다. */
        void acceptFocusedDocument();
        /** @brief dirty 이면 확인 팝업을 띄우고, 아니면 포커스 경로를 받아들입니다. */
        void updateFocusedDocument();
        /**
         * @brief "Open..." 버튼(이 종류의 에셋을 찾아 고르는 팝업)과 최근 문서 콤보를 한 줄에 그립니다.
         * @details 고른 문서는 워크스페이스 포커스로 넘긴다. 문서 교체와 미저장 확인은 다음 그리기의 `updateFocusedDocument` 가 한다.
         *          레이블은 `<패널 id>.open`, 팝업 검색 입력란은 `<패널 id>.open.search`, 목록 항목은 `<패널 id>.open.<파일 이름>` 이다.
         */
        void drawDocumentOpenBar( const utf8* pMarkPrefix );
        /** @brief 최근에 연 문서 목록을 반환합니다. 새 것이 앞이고 최대 `kMaxRecentDocumentCount` 개입니다. */
        const vector<string>& getRecentDocumentPaths() const { return _listRecentPath; }

        const string& getLoadedAssetPath() const { return _loadedAssetPath; }
        bool          isDocumentLoaded() const;
        bool          isDocumentLoadFailed() const { return _bLoadFailed == SW_TRUE; }
        /** @brief 다른 문서로 바꾸려는데 미저장 편집이 있어 "그래도 바꿀까요?" 확인을 기다리는 중이면 true 입니다. 이 동안 새 문서는 열리지 않습니다(자동화 탐침이 읽는다). */
        bool isSwitchConfirmPending() const { return _bConfirmSwitch == SW_TRUE; }
        bool canSaveDocument() const override { return _bLoadFailed == SW_FALSE; }

        /**
         * @brief 이 패널의 문서를 (다시) 읽어 내용을 채웁니다. 읽음 또는 못 읽음 표시는 기반 클래스가 결과를 보고 하며(`reloadDocument`), 패널은 표시하지 않습니다.
         * @details `Missing` 은 새 문서이며 기본값으로 시작하고 저장하면 파일을 만든다. `Malformed` 는 저장을 막는다. 읽지 못한 파일을 이전 문서로 덮어쓰지 않기 위해서다.
         *          패널이 "읽었다" 표시를 직접 호출하지 않는다. 읽기 결과를 버리고 항상 읽었다고 표시하는 일을 막기 위해서다.
         */
        virtual ToolAssetLoadResult loadDocument() = 0;
        /** @brief 문서를 읽고 결과를 표시합니다. 첫 그리기(`ensureDocumentLoaded`)와 패널의 Load, Reload 버튼이 호출합니다. */
        void reloadDocument();
        /** @brief 아직 읽지 않았으면 읽습니다. 그리기 앞부분(`updateFocusedDocument` 다음)에서 호출합니다. */
        void ensureDocumentLoaded();

        /**
         * @brief 현재 문서를 디스크에 저장합니다. 성공하면 true 입니다.
         * @details 기반(IEditorPanel)에는 "문서 없음" 기본 구현이 있지만, 문서 패널은 반드시 구현해야 하므로 여기서 다시 순수
         *          가상으로 선언한다. 구현을 잊으면 컴파일이 실패한다.
         */
        [[nodiscard]] bool saveDocument() override = 0;
        /** @brief 편집 단위로 Undo 를 남기고 dirty 로 표시합니다. */
        void notifyDocumentEdited( string_view label, string_view coalesceKey = {} );
        /** @brief 로드나 저장 직후에 Undo 기준 텍스트를 맞춥니다. */
        void syncDocumentUndoBaseline();
        /** @brief 현재 문서를 텍스트로 직렬화합니다. */
        virtual string captureDocumentText() const { return {}; }
        /** @brief 텍스트 스냅샷을 문서에 적용합니다. */
        virtual void applyDocumentText( string_view /*text*/ ) {}

        template <typename TItem>
        static int32 nextItemID( const vector<TItem>& list )
        {
            int32 maxID{ 0 };
            for ( const TItem& item : list )
            {
                maxID = MathUtil::max( maxID, item._id );
            }
            return maxID + 1;
        }

    private:
        static constexpr uint32 kMaxRecentDocumentCount = 8;

        /** @brief 이 종류의 에셋 목록 팝업을 그립니다. */
        void drawOpenDocumentPopup( const utf8* pMarkPrefix );
        /** @brief 문서를 엽니다. 워크스페이스 포커스로 넘깁니다. */
        void requestOpenDocument( string_view assetPath );
        /** @brief 최근 문서 목록 맨 앞에 @p assetPath 를 둡니다. 중복은 뺍니다. */
        void rememberRecentDocument( string_view assetPath );
        void drawUnsavedDocumentPopup();
        void restoreDocumentFromUndo( string_view text );
        /** @brief 읽었거나 새 문서임을 표시합니다. 저장을 허용하고 되돌리기 기준을 맞춥니다. */
        void markDocumentLoaded();
        /** @brief 읽지 못했음을 표시합니다. 다시 읽으려 하지 않고(`isDocumentLoaded`) 저장을 막습니다(`canSaveDocument`). 다른 문서로 바꾸거나 다시 읽어 성공하면 풀립니다. */
        void markDocumentLoadFailed( string_view reason );

        EditorAssetType                       _kind;
        string                                _loadedAssetPath;
        string                                _pendingFocusPath;
        string                                _documentUndoBaseline;
        string                                _lastSavedDocumentText;
        shared_ptr<EditorDocumentPanel*>      _pUndoTarget; ///< 되돌리기 항목이 이 패널을 찾는 표지 — 패널이 사라지면 nullptr 이다
        vector<string>                        _listRecentPath;
        vector<string>                        _listOpenCandidate; ///< 열기 팝업을 열 때 모은 이 종류의 에셋(리소스 id)
        fixed_string<constant::kMaxBuffer128> _openFilter;
        uint8                                 _bLoaded        : 1;
        uint8                                 _bConfirmSwitch : 1;
        uint8                                 _bLoadFailed    : 1;
        [[maybe_unused]] uint8                _reserved       : 5;
    };
} // namespace sw::editor
