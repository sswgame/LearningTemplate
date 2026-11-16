/**
 * @file CatalogPanel.h
 * @brief 카탈로그 편집기 창입니다. 에셋 종류 표(`AssetTypeRegistry`)에서 경로 규칙이 있는 종류의 카탈로그 파일을 종류별로 늘어놓고, 테이블로 고치고, 저장한 뒤 그 종류의 실제 로더로 검증합니다.
 * @details 언리얼 `UDataTable` / `UPrimaryDataAsset` 편집기와 Fix Up Redirectors 에 해당합니다. 종류 목록을 이 창이 따로 두지 않고 카탈로그 코드 옆의 등록 줄(`SW_ASSET_TYPE`)에서 얻으므로,
 *          새 카탈로그를 더하면 이 창에도 자동으로 나타납니다. 저장하면 파일을 쓰고 로더로 다시 읽어, 모르는 키 · 열거자 · 참조가 있으면 경고를 창에 보입니다.
 *          검증이 실패해도 파일은 쓴 대로 남고 "이전 파일로 되돌리기" 가 저장 직전 글을 되쓴다(저장이 실행 중 게임의 핫 리로드를 부르기 때문에 틀린 파일을 오래 두지 않게 한다).
 *          이 창은 디스크 · 로더 · 그리기만 한다. 파일 목록은 `CatalogFileList`, 열린 파일의 상태는 `CatalogFileSession` 이 맡는다(화면 없이 테스트한다).
 *          여는 길은 문서 기반(`EditorDocumentPanel`, Data 종류 가운데 카탈로그 종류 하나에 맞는 파일)이다 — 목록 클릭 · 콘텐츠 브라우저 · `asset.openScratch` 가 모두 워크스페이스 포커스로 연다.
 *          예전에는 목록 클릭만 여는 자체 길이라 시나리오가 사본(`scratch/…`)을 가리킬 수 없었고, 다른 파일을 고르면 미저장 편집을 확인 없이 버릴 수 있었다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorBackgroundIO.h"
#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"
#include "Editor/Panels/Catalog/CatalogTableEditor.h"
#include "Editor/Panels/Catalog/Model/CatalogFileList.h"
#include "Editor/Panels/Catalog/Model/CatalogFileSession.h"

#include "Engine/Serialization/Inherit/DataInheritance.h"

namespace sw::editor
{
    /**
     * @class CatalogPanel
     * @brief 카탈로그 파일 목록과 테이블 편집, 저장 · 검증을 한 창에 모은 에디터 창입니다.
     */
    class CatalogPanel final : public EditorDocumentPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "catalog_editor";

        CatalogPanel();

        const utf8* getPanelTitle() const override { return "Catalog Editor"; }

        EditorDockSlot     getDefaultDockSlot() const override { return EditorDockSlot::Floating; } // 도구 창 — 문서 기반의 기본(가운데 탭)이 아니라 옮기기 전처럼 떠 있는 창
        EditorTextExtent   getInitialPanelSize() const override { return EditorTextExtent::ofText( 133.0f, 36.0f ); }
        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override;

        /** @brief 목록의 파일 수입니다. 찾기가 끝나기 전에는 0 입니다(감시 값). */
        uint32 getFileCount() const { return static_cast<uint32>( _fileList.getRows().size() ); }
        /** @brief 열린 파일의 행 수입니다(감시 값). */
        uint32 countRows() const { return _session.getDocument().countRows(); }
        /** @brief 고치는 원문의 크기입니다(감시 값). */
        size_t getTextByteCount() const { return _session.getDocument().getText().size(); }
        /** @brief 마지막 검증의 결과입니다(감시 값). */
        CatalogFileSession::ValidationState getValidationState() const { return _session.getValidationState(); }

    protected:
        /** @brief 카탈로그 종류 하나에 맞는 파일만 받습니다(목록에 서는 파일과 같은 규칙 — 종류가 겹치는 파일은 스키마 테스트가 잡는다). */
        bool canAcceptDocumentPath( string_view assetPath ) const override;
        /** @brief 받아들인 문서 경로의 글을 읽어 세션을 엽니다. 읽지 못하면 `Malformed` 라 저장이 막힌다(앞 파일의 글을 이 파일에 쓰지 않는다). */
        ToolAssetLoadResult loadDocument() override;
        /** @brief 파일 색인 잡을 냅니다(아직 색인이 없을 때만). */
        void onPanelOpened() override;

    private:
        void               pollIndexJob();
        void               rebuildFileList( const vector<EditorAssetIndexEntry>& listEntry );
        void               drawFileList();
        void               drawDocumentArea();
        void               drawToolbar();
        void               drawValidation();
        [[nodiscard]] bool writeCurrentFile();
        void               validateCurrentFile();
        /** @brief 툴바 Revert — 마지막으로 저장한 글로 되돌립니다. 되돌리기 기록은 남아 Undo 가 편집을 되살린다(디스크를 다시 읽는 문서 되돌리기와 다르다). */
        void revertToSavedText();
        void restorePreviousFile();
        void syncDirty();

    private:
        fixed_string<constant::buffer::kMax128> _filter;
        CatalogFileList                         _fileList;
        vector<CatalogFileGroup>                _listFileGroup; ///< 거르개에 맞는 파일의 재사용 버퍼
        EditorAssetIndexJob                     _indexJob;
        DataInheritanceResourceSource           _inheritanceSource; ///< 항목 상속의 부모 파일을 읽는 곳(`_session` 의 문서가 가리킨다)
        CatalogFileSession                      _session;
        CatalogTableEditor                      _tableEditor;
        uint8                                   _bIndexLoaded : 1;
        bool                                    _bRawMode; ///< NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
    };
} // namespace sw::editor
