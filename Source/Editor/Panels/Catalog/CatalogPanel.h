/**
 * @file CatalogPanel.h
 * @brief 카탈로그 편집기 창입니다. 데이터 파일 종류 레지스트리(`AssetDataKindRegistry`)에 올라온 카탈로그 파일을 종류별로 늘어놓고, 테이블로 고치고, 저장한 뒤 그 종류의 실제 로더로 검증합니다.
 * @details 언리얼 `UDataTable` / `UPrimaryDataAsset` 편집기와 Fix Up Redirectors 에 해당합니다. 종류 목록을 이 창이 따로 두지 않고 카탈로그 코드 옆의 등록 줄(`SW_RESOURCE_DATA_KIND`)에서 얻으므로,
 *          새 카탈로그를 더하면 이 창에도 자동으로 나타납니다. 저장하면 파일을 쓰고 로더로 다시 읽어, 모르는 키 · 열거자 · 참조가 있으면 경고를 창에 보입니다.
 *          검증이 실패해도 파일은 쓴 대로 남고 "이전 파일로 되돌리기" 가 저장 직전 글을 되쓴다(저장이 실행 중 게임의 핫 리로드를 부르기 때문에 틀린 파일을 오래 두지 않게 한다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorBackgroundIO.h"
#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Panels/Catalog/CatalogTableEditor.h"
#include "Editor/Panels/Catalog/Model/CatalogDocument.h"

namespace sw::editor
{
    /**
     * @class CatalogPanel
     * @brief 카탈로그 파일 목록과 테이블 편집, 저장 · 검증을 한 창에 모은 에디터 창입니다.
     */
    class CatalogPanel final : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "catalog_editor";

        CatalogPanel();

        const utf8*        getPanelTitle() const override { return "Catalog Editor"; }
        bool               isToolPanel() const override { return true; }
        EditorTextExtent   getInitialPanelSize() const override { return EditorTextExtent::ofText( 133.0f, 36.0f ); }
        void               drawContent() override;
        [[nodiscard]] bool saveDocument() override;
        void               revertDocument() override;

    private:
        /** @brief 목록의 파일 하나입니다. */
        struct FileRow
        {
            string _assetID{};
            string _kindLabel{};
        };

        /** @brief 마지막 검증의 결과입니다. */
        enum class ValidationState : uint8
        {
            None = 0,
            Passed,
            Failed
        };

        /** @brief 종류 이름, 같으면 리소스 id 순입니다. */
        static bool isFileBefore( const FileRow& lhs, const FileRow& rhs );

        void               pollIndexJob();
        void               rebuildFileList( const vector<EditorAssetIndexEntry>& listEntry );
        void               drawFileList();
        void               drawDocumentArea();
        void               drawToolbar();
        void               drawValidation();
        void               openFile( const string& assetID );
        [[nodiscard]] bool writeCurrentFile();
        void               validateCurrentFile();
        void               restorePreviousFile();
        void               syncDirty();

        fixed_string<constant::kMaxBuffer128> _filter;
        vector<FileRow>                       _listFile;
        vector<string>                        _listWarning;
        EditorAssetIndexJob                   _indexJob;
        CatalogDocument                       _document;
        CatalogTableEditor                    _tableEditor;
        string                                _selectedID;
        string                                _selectedKindLabel;
        string                                _previousFileText;
        string                                _statusText;
        ValidationState                       _validationState;
        bool                                  _bIndexLoaded;
        bool                                  _bRawMode;
    };
} // namespace sw::editor
