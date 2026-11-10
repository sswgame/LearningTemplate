#include "pch.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Ability/AbilityCatalogPanel.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct AbilityCatalogPanelInternal
        {
            static constexpr float32     kListLines = 18.0f;           ///< 항목 목록이 보이는 줄 수
            static constexpr string_view kFileName  = "abilities.xml"; ///< 런타임 카탈로그의 파일 이름(`AbilityCatalog` 의 경로 규칙 `game/<게임>/data/abilities.xml` 끝)

            [[nodiscard]] static const AbilityCatalogPanel* findPanel()
            {
                const EditorPanelManager* pManager = editor::findSubsystem<EditorPanelManager>();
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

            /** @brief 종류의 목록 제목입니다. */
            static const utf8* getKindTitle( AbilityEntryKind kind )
            {
                switch ( kind )
                {
                    case AbilityEntryKind::Effect:
                        return SW_LOCKEY( "EdAbilityCatalog", "Effects" );
                    case AbilityEntryKind::Ability:
                        return SW_LOCKEY( "EdAbilityCatalog", "Abilities" );
                    case AbilityEntryKind::AbilitySet:
                        return SW_LOCKEY( "EdAbilityCatalog", "AbilitySets" );
                    case AbilityEntryKind::Presentation:
                        return SW_LOCKEY( "EdAbilityCatalog", "Presentations" );
                    case AbilityEntryKind::Count:
                        break;
                }
                return "";
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

    bool AbilityCatalogPanel::acceptsDocumentPath( string_view assetPath ) const
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
        _pathInput                                = path.c_str();
        const optional<AbsolutePath> absolutePath = getDocumentDiskPath();
        string                       text;
        // 읽지 못하면 저장을 막는다(`Malformed`) — 앞 파일의 글을 이 경로에 저장하지 않는다.
        if ( absolutePath.has_value() == false || FileUtil::readTextFile( absolutePath->str(), text ) == false )
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

    bool AbilityCatalogPanel::saveDocument()
    {
        const string&                openPath     = getLoadedAssetPath();
        const optional<AbsolutePath> absolutePath = getDocumentDiskPath();
        if ( openPath.empty() || absolutePath.has_value() == false || _model.getDocument().isParsed() == false )
        {
            _status = "Nothing to save - open a well-formed file first";
            return false;
        }
        if ( FileUtil::writeTextFile( absolutePath->str(), _model.getDocument().getText() ) == false )
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
        AbilityCatalog catalog;
        _validation = catalog.loadFromXMLText( _model.getDocument().getText(), getLoadedAssetPath() ) ? ValidationState::Passed : ValidationState::Failed;
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
                ImGui::SeparatorText( Internal::getKindTitle( kind ) );
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
        const string preview( current.empty() ? string_view( SW_LOCKEY( "EdAbilityCatalog", "NoneChoice" ) ) : current );
        const string comboID = string( "##" ) + pID; // 이름표는 줄 앞(`drawPropertyRowLabel`)이 이미 그렸다
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
                    EditorSelfTestMarks::record( ( string( pID ) + ".first" ).c_str() );
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
