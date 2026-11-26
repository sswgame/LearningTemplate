#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Ability/Model/AbilityCatalogEditModel.h"

#include <imgui.h>

namespace sw
{
    // 에디터 확장 GameplayTools 의 글(엔진 현지화 폴더의 `editorplugins/gameplaytools/`) — 이 모듈이 올라온 동안만 조회 사슬에 든다. 배포 팩에 담지 않는다.
    SW_LOCALIZATION_PROJECT( EditorPluginGameplayToolsText, "engine/localization/editorplugins/gameplaytools/gameplaytools.locproject.xml" );
} // namespace sw

namespace sw::editor
{
    /**
     * @class AbilityCatalogPanel
     * @brief 파일 머리말 참고.
     */
    class AbilityCatalogPanel final : public EditorDocumentPanel
    {
    public:
        /** @brief 패널 id 입니다. */
        static constexpr const utf8* kPanelID = "ability_catalog";
        /** @brief 처음 패널 크기를 정하는 글 칸 수입니다(폭 · 높이). */
        static constexpr float32 kInitialTextWidth  = 110.0f;
        static constexpr float32 kInitialTextHeight = 36.0f;

        /** @brief 마지막 런타임 검증 결과입니다(감시 값 `Editor.AbilityCatalogValidation`). */
        enum class ValidationState : uint8
        {
            NotRun = 0,
            Passed,
            Failed,
        };

        AbilityCatalogPanel();

        const utf8*      getPanelTitle() const override { return "Ability Catalog"; }
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( kInitialTextWidth, kInitialTextHeight ); }
        /** @brief 떠 있는 도구 창입니다(문서 기반의 기본인 가운데 탭이 아니다 — 옮기기 전 자리를 지킨다). */
        EditorDockSlot     getDefaultDockSlot() const override { return EditorDockSlot::Floating; }
        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument( AssetWriter& writer ) override;

        const AbilityCatalogEditModel& getModel() const { return _model; }
        ValidationState                getValidationState() const { return _validation; }

    protected:
        /** @brief 이름이 `abilities.xml` 인 파일만 받습니다(런타임 카탈로그의 파일 이름 — 게임 폴더 밖의 사본도 받는다). */
        bool canAcceptDocumentPath( string_view assetPath ) const override;
        /** @brief 받아들인 문서 경로의 글을 읽어 모델을 엽니다. 읽지 못하면 `Malformed` 라 저장이 막힌다(앞 파일의 글을 이 파일에 쓰지 않는다). */
        ToolAssetLoadResult loadDocument() override;

    private:
        /** @brief 경로 칸 · 열기 · 저장 · 되돌리기 · 실행 취소 줄과 검증 결과입니다. */
        void drawFileRow();
        /** @brief 종류별 항목 목록(거르개 · 문제가 있는 항목은 빨간 표시 하나)과 더하기 단추입니다. */
        void drawEntryList();
        /** @brief 고른 항목의 쓰는 것 · 쓰이는 곳 · 문제 · 복제 · 지우기입니다. */
        void drawEntryDetail( int32 entry );
        /** @brief 참조 하나의 줄입니다 — 이름, 대상 고르기(그 종류의 id 목록), 대상으로 가기. */
        void drawReferenceRow( int32 reference );
        /** @brief 어빌리티의 빈 비용 · 쿨다운 칸과 세트의 구성 더하기입니다. */
        void drawAddReferenceRows( int32 entry );
        /** @brief id 목록 콤보입니다. 골랐으면 true 이고 @p outID 에 고른 id 입니다. */
        bool drawIDCombo( const utf8* pID, AbilityEntryKind kind, string_view current, string& outID );
        /** @brief 툴바 Revert — 마지막으로 저장한 글로 되돌립니다. 되돌리기 기록은 남아 Undo 가 편집을 되살린다(디스크를 다시 읽는 문서 되돌리기와 다르다). */
        void revertToSavedText();
        void validateText();
        /** @brief 편집 결과를 반영합니다 — 성공이면 dirty, 실패면 상태 줄에 이유. */
        void reportEdit( bool bEdited );
        void syncDirty();

    private:
        AbilityCatalogEditModel                 _model;
        DataInheritanceAssetSource              _inheritanceSource; ///< 항목 `_base` 의 부모를 읽는 곳(같은 파일 · 상속 규칙 표)
        fixed_string<constant::buffer::kMax256> _pathInput;         ///< 경로 칸의 글이다. 확정하면 문서 열기(포커스)로 넘긴다 — 연 문서 경로는 문서 기반이 든다
        fixed_string<constant::buffer::kMax64>  _filter;
        string                                  _status;
        vector<int32>                           _listScratchReference;
        vector<string>                          _listScratchID;
        int32                                   _selectedEntry;
        ValidationState                         _validation;
    };

    namespace
    {
        struct AbilityCatalogPanelInternal
        {
            static constexpr float32     kListLines = 18.0f;           ///< 항목 목록이 보이는 줄 수
            static constexpr string_view kFileName  = "abilities.xml"; ///< 런타임 카탈로그의 파일 이름(`AbilityCatalog` 의 경로 규칙 `game/<게임>/data/abilities.xml` 끝)

            [[nodiscard]] static const AbilityCatalogPanel* findPanel()
            {
                const EditorPanelManager* pManager = EditorPanelManager::findInEditorContext();
                return pManager != nullptr ? pManager->findPanel<AbilityCatalogPanel>() : nullptr;
            }

            [[nodiscard]] static bool readEntryCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AbilityCatalogPanel* pPanel = findPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getModel().getEntries().size() );
                return true;
            }

            [[nodiscard]] static bool readIssueCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AbilityCatalogPanel* pPanel = findPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getModel().getIssues().size() );
                return true;
            }

            [[nodiscard]] static bool readValidation( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AbilityCatalogPanel* pPanel = findPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getValidationState() );
                return true;
            }

            static void recordIndexedMark( const utf8* pPrefix, int32 index )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                fixed_string<constant::buffer::kMax64> key;
                formatstring( key.data(), key.capacity(), "%#.%#", pPrefix, index );
                EditorSelfTestMarks::record( key.c_str() );
            }

            static void drawErrorText( const string& text )
            {
                ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ) );
                ImGui::TextWrapped( "%s", text.c_str() );
                ImGui::PopStyleColor();
            }
        };
    } // namespace

    SW_EDITOR_PANEL( AbilityCatalogPanel, AbilityCatalogPanel::kPanelID, EditorPanelCategory::Custom, 9305 );
    SW_AUTOMATION_WATCH( editorAbilityCatalogEntryCount, "Editor.AbilityCatalogEntryCount", "Entries (effects, abilities, sets, presentations) of the file open in the Ability Catalog window",
                         &AbilityCatalogPanelInternal::readEntryCount );
    SW_AUTOMATION_WATCH( editorAbilityCatalogIssueCount, "Editor.AbilityCatalogIssueCount", "Unresolved references and repeated ids the Ability Catalog window shows",
                         &AbilityCatalogPanelInternal::readIssueCount );
    SW_AUTOMATION_WATCH( editorAbilityCatalogValidation, "Editor.AbilityCatalogValidation", "Last runtime-loader check of the Ability Catalog window: 0 not run, 1 passed, 2 failed",
                         &AbilityCatalogPanelInternal::readValidation );

    AbilityCatalogPanel::AbilityCatalogPanel()
        : EditorDocumentPanel( EditorAssetType{ EditorAssetTypeIDs::kData }, false )
        , _model{}
        , _inheritanceSource{}
        , _pathInput{}
        , _filter{}
        , _status{}
        , _listScratchReference{}
        , _listScratchID{}
        , _selectedEntry{ -1 }
        , _validation{ ValidationState::NotRun }
    {
    }

    void AbilityCatalogPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        drawFileRow();
        if ( getLoadedAssetPath().empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdAbilityCatalog", "NoFileHint" ) );
            return;
        }
        if ( isDocumentLoadFailed() )
        {
            // 모델은 앞 파일의 것이다 — 이 경로의 항목처럼 보이지 않게 까닭만 보인다(저장도 기반이 막았다).
            AbilityCatalogPanelInternal::drawErrorText( _status );
            return;
        }
        if ( _model.getDocument().isParsed() == false )
        {
            AbilityCatalogPanelInternal::drawErrorText( _model.getDocument().getError() );
            return;
        }
        constexpr ImGuiTableFlags kSplitFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;
        if ( ImGui::BeginTable( "##AbilityCatalogSplit", 2, kSplitFlags, ImVec2{ 0.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() } ) )
        {
            ImGui::TableSetupColumn( "##Entries", ImGuiTableColumnFlags_WidthStretch, 0.35f );
            ImGui::TableSetupColumn( "##Detail", ImGuiTableColumnFlags_WidthStretch, 0.65f );
            ImGui::TableNextColumn();
            drawEntryList();
            ImGui::TableNextColumn();
            if ( 0 <= _selectedEntry && _selectedEntry < static_cast<int32>( _model.getEntries().size() ) )
                drawEntryDetail( _selectedEntry );
            else
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdAbilityCatalog", "PickEntryHint" ) );
            ImGui::EndTable();
        }
        EditorWidgets::drawPanelStatus( _status.c_str() );
    }

    void AbilityCatalogPanel::drawFileRow()
    {
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdAbilityCatalog", "File" ), SW_LOCKEY( "EdAbilityCatalog", "FileTip" ) );
        if ( EditorWidgets::drawAssetPathField( "##AbilityCatalogPath", _pathInput, "game/<game>/data/abilities.xml", ".xml", "abilityCatalog.path" )._bDeactivatedAfterEdit )
            requestOpenDocument( _pathInput.c_str() ); // 콘텐츠 브라우저와 같은 길 — 다음 그리기의 `updateFocusedDocument` 가 미저장을 묻고 연다
        const bool bOpen = getLoadedAssetPath().empty() == false;
        ImGui::BeginDisabled( bOpen == false );
        if ( ImGui::Button( SW_LOCKEY( "EdAbilityCatalog", "Save" ) ) )
            (void)saveDocumentAndClearDirty(); // 실패하면 dirty 가 남고 이유는 상태 줄에 있다
        EditorSelfTestMarks::record( "abilityCatalog.save" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdAbilityCatalog", "Undo" ) ) );
        ImGui::BeginDisabled( _model.getDocument().canUndo() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdAbilityCatalog", "Undo" ) ) )
        {
            _model.editDocument().undo();
            _model.rebuild();
            syncDirty();
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "abilityCatalog.undo" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdAbilityCatalog", "Redo" ) ) );
        ImGui::BeginDisabled( _model.getDocument().canRedo() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdAbilityCatalog", "Redo" ) ) )
        {
            _model.editDocument().redo();
            _model.rebuild();
            syncDirty();
        }
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdAbilityCatalog", "Revert" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdAbilityCatalog", "Revert" ) ) )
            revertToSavedText();
        ImGui::EndDisabled();
        // 검증 결과는 상태다 — 통과는 Success, 실패는 Error 이고 문장으로 말한다(자세한 이유는 로그).
        if ( _validation == ValidationState::Passed )
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Success" ) ), "%s", SW_LOCKEY( "EdAbilityCatalog", "ValidationPassed" ) );
        else if ( _validation == ValidationState::Failed )
            AbilityCatalogPanelInternal::drawErrorText( SW_LOCKEY( "EdAbilityCatalog", "ValidationFailed" ) );
    }

    bool AbilityCatalogPanel::canAcceptDocumentPath( string_view assetPath ) const
    {
        string_view fileName;
        FileUtil::getFileNamePart( assetPath, fileName );
        return fileName == AbilityCatalogPanelInternal::kFileName;
    }

    ToolAssetLoadResult AbilityCatalogPanel::loadDocument()
    {
        const string path = acceptDocumentToLoad();
        if ( path.empty() )
            return ToolAssetLoadResult::Missing; // 아직 연 파일이 없다 — 모델도 비어 있어 저장할 것이 없다
        _pathInput                            = path.c_str();
        const optional<DiskPath> absolutePath = getDocumentDiskPath();
        string                   text;
        // 읽지 못하면 저장을 막는다(`Malformed`) — 앞 파일의 글을 이 경로에 저장하지 않는다.
        if ( absolutePath.has_value() == false || FileUtil::readTextFile( *absolutePath, text ) == false )
        {
            StringBuilder<constant::buffer::kMax256> statusBuilder;
            statusBuilder.appendFormat( "Could not read '%#'", path );
            _status = string( statusBuilder.view() );
            return ToolAssetLoadResult::Malformed;
        }
        if ( _model.load( text, &_inheritanceSource ) == false )
        {
            StringBuilder<constant::buffer::kMax256> statusBuilder2;
            statusBuilder2.appendFormat( "'%#' is not well-formed XML: %#", path, _model.getDocument().getError() );
            _status = string( statusBuilder2.view() );
        }
        else
        {
            StringBuilder<constant::buffer::kMax256> statusBuilder;
            statusBuilder.appendFormat( "Opened %#", path );
            _status = string( statusBuilder.view() );
        }
        _selectedEntry = _model.getEntries().empty() ? -1 : 0;
        _validation    = ValidationState::NotRun;
        SW_LOG_INFO_ID( "AbilityCatalogPanel.Opened", "Ability catalog opened %#", path.c_str() );
        // 잘 짜이지 않은 글도 열린 문서다 — 원문 오류를 보이고 저장은 `saveDocument` 가 막는다(미저장 표시는 읽은 뒤 기반이 지운다).
        return ToolAssetLoadResult::Loaded;
    }

    bool AbilityCatalogPanel::saveDocument( AssetWriter& writer )
    {
        const string&            openPath     = getLoadedAssetPath();
        const optional<DiskPath> absolutePath = getDocumentDiskPath();
        if ( openPath.empty() || absolutePath.has_value() == false || _model.getDocument().isParsed() == false )
        {
            _status = "Nothing to save - open a well-formed file first";
            return false;
        }
        if ( writer.writeText( *absolutePath, _model.getDocument().getText() ) != AssetWriteResult::Written )
        {
            StringBuilder<constant::buffer::kMax256> statusBuilder;
            statusBuilder.appendFormat( "Could not write '%#'", openPath );
            _status = string( statusBuilder.view() );
            return false;
        }
        _model.editDocument().markSaved();
        StringBuilder<constant::buffer::kMax256> statusBuilder;
        statusBuilder.appendFormat( "Saved %#", openPath );
        _status = string( statusBuilder.view() );
        validateText();
        return true;
    }

    void AbilityCatalogPanel::revertToSavedText()
    {
        _model.editDocument().revert();
        _model.rebuild();
        _status = "Reverted to the saved text - Undo brings the edits back";
        syncDirty();
    }

    void AbilityCatalogPanel::validateText()
    {
        // 런타임 로더 그대로 읽어 본다 — 클래스 이름 · 참조 · 칸의 판정은 로더의 것이고 이 창이 따로 적지 않는다(이유는 로그에 남는다).
        _validation = _model.validateWithRuntimeLoader( getLoadedAssetPath() ) ? ValidationState::Passed : ValidationState::Failed;
    }

    void AbilityCatalogPanel::reportEdit( bool bEdited )
    {
        if ( bEdited )
        {
            _validation = ValidationState::NotRun;
            _status.clear();
        }
        else if ( _model.getDocument().getError().empty() == false )
        {
            _status = _model.getDocument().getError();
        }
        syncDirty();
    }

    void AbilityCatalogPanel::syncDirty()
    {
        if ( _model.getDocument().isDirty() )
            markDocumentDirty();
        else
            clearDocumentDirty();
    }

    void AbilityCatalogPanel::drawEntryList()
    {
        using Internal                           = AbilityCatalogPanelInternal;
        const vector<AbilityCatalogEntry>& list  = _model.getEntries();
        uint32                             shown = 0;
        for ( const AbilityCatalogEntry& entry : list )
        {
            shown += _filter.empty() || StringUtil::contains( entry._id, _filter.c_str(), true ) ? 1u : 0u;
        }
        EditorWidgets::drawFilterBar( "##AbilityCatalogFilter", _filter, SW_LOCKEY( "EdAbilityCatalog", "FilterHint" ), shown, static_cast<uint32>( list.size() ), nullptr, "abilityCatalog.filter" );
        if ( ImGui::BeginChild( "##AbilityCatalogEntries", ImVec2{ 0.0f, ImGui::GetTextLineHeightWithSpacing() * Internal::kListLines }, ImGuiChildFlags_Borders ) )
        {
            for ( uint32 kindIndex = 0; kindIndex < static_cast<uint32>( AbilityEntryKind::Count ); ++kindIndex )
            {
                const AbilityEntryKind kind = static_cast<AbilityEntryKind>( kindIndex );
                ImGui::SeparatorText( LocalizedText::findEnum( kind ) );
                for ( int32 entryIndex = 0; entryIndex < static_cast<int32>( list.size() ); ++entryIndex )
                {
                    const AbilityCatalogEntry& entry = list[static_cast<size_t>( entryIndex )];
                    if ( entry._kind != kind || ( _filter.empty() == false && StringUtil::contains( entry._id, _filter.c_str(), true ) == false ) )
                        continue;
                    bool bIssue = false;
                    for ( const AbilityCatalogIssue& issue : _model.getIssues() )
                    {
                        bIssue = bIssue || issue._entry == entryIndex;
                    }
                    ImGui::PushID( entryIndex );
                    // 문제가 있는 항목은 빨간 글 하나다(까닭은 오른쪽 상세의 문장).
                    if ( bIssue )
                        ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ) );
                    if ( ImGui::Selectable( entry._id.empty() ? "(no id)" : entry._id.c_str(), _selectedEntry == entryIndex ) )
                        _selectedEntry = entryIndex;
                    if ( bIssue )
                        ImGui::PopStyleColor();
                    Internal::recordIndexedMark( "abilityCatalog.entry", entryIndex );
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();
        // 더하기는 종류마다 — 새 id 는 겹치지 않는다(`new`, `new_2`).
        if ( ImGui::Button( SW_LOCKEY( "EdAbilityCatalog", "AddEffect" ) ) )
        {
            const expected<int32> added = _model.addEntry( AbilityEntryKind::Effect );
            reportEdit( added.has_value() );
            if ( added.has_value() )
                _selectedEntry = *added;
        }
        EditorSelfTestMarks::record( "abilityCatalog.addEffect" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdAbilityCatalog", "AddAbility" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdAbilityCatalog", "AddAbility" ) ) )
        {
            const expected<int32> added = _model.addEntry( AbilityEntryKind::Ability );
            reportEdit( added.has_value() );
            if ( added.has_value() )
                _selectedEntry = *added;
        }
        EditorSelfTestMarks::record( "abilityCatalog.addAbility" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdAbilityCatalog", "AddAbilitySet" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdAbilityCatalog", "AddAbilitySet" ) ) )
        {
            const expected<int32> added = _model.addEntry( AbilityEntryKind::AbilitySet );
            reportEdit( added.has_value() );
            if ( added.has_value() )
                _selectedEntry = *added;
        }
    }

    bool AbilityCatalogPanel::drawIDCombo( const utf8* pID, AbilityEntryKind kind, string_view current, string& outID )
    {
        bool bPicked = false;
        _model.collectIDs( kind, _listScratchID );
        const string                            preview( current.empty() ? string_view( SW_LOCKEY( "EdAbilityCatalog", "NoneChoice" ) ) : current );
        fixed_string<constant::buffer::kMax128> comboID;
        formatstring( comboID.data(), comboID.capacity(), "##%#", pID ); // 이름표는 줄 앞(`drawPropertyRowLabel`)이 이미 그렸다
        EditorChrome::setNextItemWidthColumns( 24.0f );
        if ( EditorWidgets::beginCombo( comboID.c_str(), preview.c_str() ) )
        {
            for ( size_t index = 0; index < _listScratchID.size(); ++index )
            {
                const string& id = _listScratchID[index];
                if ( ImGui::Selectable( id.c_str(), string_view( id ) == current ) )
                {
                    outID   = id;
                    bPicked = true;
                }
                if ( index == 0 )
                    EditorSelfTestMarks::recordFormat( "%#.first", pID );
            }
            ImGui::EndCombo();
        }
        return bPicked;
    }

    void AbilityCatalogPanel::drawReferenceRow( int32 reference )
    {
        const AbilityCatalogReference& ref = _model.getReferences()[static_cast<size_t>( reference )];
        ImGui::PushID( reference );
        EditorWidgets::drawPropertyRowLabel( ref._label.c_str(), ref._bInherited == SW_TRUE ? SW_LOCKEY( "EdAbilityCatalog", "InheritedTip" ) : nullptr );
        string     picked;
        const bool bUnresolved = ref._target.has_value() == false && ref._bRequired == SW_TRUE;
        if ( bUnresolved )
            ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ) );
        StringBuilder<constant::buffer::kMax256> comboIDBuilder;
        comboIDBuilder.appendFormat( "abilityCatalog.ref.%#", reference );
        const string comboID{ comboIDBuilder.view() };
        const bool   bPicked = drawIDCombo( comboID.c_str(), ref._targetKind, ref._targetID, picked );
        if ( bUnresolved )
            ImGui::PopStyleColor();
        EditorSelfTestMarks::record( comboID.c_str() );
        if ( bPicked )
            reportEdit( _model.setReference( reference, picked ) );
        // 가리키는 항목으로 간다(Reference Viewer 처럼 따라간다).
        const optional<int32> target = ref._target;
        if ( target.has_value() )
        {
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdAbilityCatalog", "Go" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdAbilityCatalog", "Go" ) ) )
                _selectedEntry = *target;
        }
        ImGui::PopID();
    }

    void AbilityCatalogPanel::drawAddReferenceRows( int32 entry )
    {
        const AbilityCatalogEntry& item = _model.getEntries()[static_cast<size_t>( entry )];
        string                     picked;
        if ( item._kind == AbilityEntryKind::Ability )
        {
            // 비용 · 쿨다운이 없으면 고를 칸을 보인다(있으면 위 참조 줄이 고친다).
            _model.collectReferencesFrom( entry, _listScratchReference );
            bool bHasCost     = false;
            bool bHasCooldown = false;
            for ( const int32 reference : _listScratchReference )
            {
                bHasCost     = bHasCost || _model.getReferences()[static_cast<size_t>( reference )]._attribute == "cost";
                bHasCooldown = bHasCooldown || _model.getReferences()[static_cast<size_t>( reference )]._attribute == "cooldown";
            }
            if ( bHasCost == false )
            {
                EditorWidgets::drawPropertyRowLabel( "cost" );
                if ( drawIDCombo( "abilityCatalog.addCost", AbilityEntryKind::Effect, {}, picked ) )
                    reportEdit( _model.setAbilityEffectSlot( entry, "cost", picked ) );
            }
            if ( bHasCooldown == false )
            {
                EditorWidgets::drawPropertyRowLabel( "cooldown" );
                if ( drawIDCombo( "abilityCatalog.addCooldown", AbilityEntryKind::Effect, {}, picked ) )
                    reportEdit( _model.setAbilityEffectSlot( entry, "cooldown", picked ) );
            }
        }
        if ( item._kind == AbilityEntryKind::AbilitySet )
        {
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdAbilityCatalog", "AddAbilityToSet" ) );
            if ( drawIDCombo( "abilityCatalog.addSetAbility", AbilityEntryKind::Ability, {}, picked ) )
                reportEdit( _model.addSetMember( entry, AbilityEntryKind::Ability, picked ) );
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdAbilityCatalog", "AddEffectToSet" ) );
            if ( drawIDCombo( "abilityCatalog.addSetEffect", AbilityEntryKind::Effect, {}, picked ) )
                reportEdit( _model.addSetMember( entry, AbilityEntryKind::Effect, picked ) );
        }
    }

    void AbilityCatalogPanel::drawEntryDetail( int32 entry )
    {
        using Internal                 = AbilityCatalogPanelInternal;
        const AbilityCatalogEntry item = _model.getEntries()[static_cast<size_t>( entry )]; // 편집이 목록을 바꾸므로 사본으로 그린다
        ImGui::TextUnformatted( item._id.c_str() );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", AbilityCatalogEditModel::getElementName( item._kind ) );
        for ( const AbilityCatalogIssue& issue : _model.getIssues() )
        {
            if ( issue._entry == entry )
                Internal::drawErrorText( issue._message );
        }

        ImGui::SeparatorText( SW_LOCKEY( "EdAbilityCatalog", "Uses" ) );
        _model.collectReferencesFrom( entry, _listScratchReference );
        const vector<int32> listFrom = _listScratchReference;
        if ( listFrom.empty() )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdAbilityCatalog", "UsesNothing" ) );
        for ( const int32 reference : listFrom )
        {
            if ( reference < static_cast<int32>( _model.getReferences().size() ) )
                drawReferenceRow( reference );
        }
        drawAddReferenceRows( entry );

        ImGui::SeparatorText( SW_LOCKEY( "EdAbilityCatalog", "UsedBy" ) );
        _model.collectReferencesTo( entry, _listScratchReference );
        if ( _listScratchReference.empty() )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdAbilityCatalog", "UsedByNothing" ) );
        for ( const int32 reference : _listScratchReference )
        {
            const AbilityCatalogReference& ref  = _model.getReferences()[static_cast<size_t>( reference )];
            const AbilityCatalogEntry&     user = _model.getEntries()[static_cast<size_t>( ref._from )];
            ImGui::PushID( 10000 + reference );
            StringBuilder<constant::buffer::kMax256> labelBuilder;
            labelBuilder.appendFormat( "%#  (%#)", user._id, ref._label );
            const string label{ labelBuilder.view() };
            if ( ImGui::Selectable( label.c_str(), false ) )
                _selectedEntry = ref._from;
            Internal::recordIndexedMark( "abilityCatalog.usedBy", reference );
            ImGui::PopID();
        }

        ImGui::Separator();
        if ( ImGui::Button( SW_LOCKEY( "EdAbilityCatalog", "Duplicate" ) ) )
        {
            const int32 copy = _model.duplicateEntry( entry );
            reportEdit( copy >= 0 );
            _selectedEntry = copy >= 0 ? copy : entry;
        }
        EditorSelfTestMarks::record( "abilityCatalog.duplicate" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdAbilityCatalog", "Remove" ) ) );
        if ( ImGui::Button( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdAbilityCatalog", "Remove" ), "Remove##abilityEntry" ) ) )
        {
            const bool bRemoved = _model.removeEntry( entry );
            reportEdit( bRemoved );
            if ( bRemoved )
                _selectedEntry = -1;
        }
        EditorSelfTestMarks::record( "abilityCatalog.remove" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdAbilityCatalog", "RemoveTip" ) );
    }
} // namespace sw::editor
