#include "pch.h"

#include "Editor/Panels/Catalog/CatalogPanel.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Resource/ResourceDataKind.h"
#include "Engine/Resource/ResourcePaths.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 검증이 도는 동안 로더가 남기는 경고 · 오류 줄을 모읍니다. */
        class CatalogWarningCollector
        {
        public:
            CatalogWarningCollector()
                : _mutex{}
                , _listWarning{}
                , _handle{}
            {
                _handle = Logger::registerGlobalListener( LogWrittenDelegate::create<&CatalogWarningCollector::onLog>( this ) );
            }

            ~CatalogWarningCollector() { Logger::unregisterGlobalListener( _handle ); }

            CatalogWarningCollector( const CatalogWarningCollector& )            = delete;
            CatalogWarningCollector& operator=( const CatalogWarningCollector& ) = delete;

            vector<string> take()
            {
                std::scoped_lock<mutex> lock{ _mutex };
                return std::move( _listWarning );
            }

        private:
            void onLog( const LogEntry& entry )
            {
                if ( entry._level != LogLevel::Warning && entry._level != LogLevel::Error )
                    return;
                std::scoped_lock<mutex> lock{ _mutex };
                _listWarning.push_back( entry._message );
            }

            mutex          _mutex;
            vector<string> _listWarning;
            DelegateHandle _handle;
        };

        // 시나리오 탐침이 읽는 값입니다. 패널이 그릴 때 갱신하고 탐침은 게임 스레드에서 읽습니다.
        std::atomic<int32> s_catalogFileCount{ 0 };
        std::atomic<int32> s_catalogRowCount{ 0 };
        std::atomic<int32> s_catalogValidation{ 0 }; ///< 0 아직, 1 통과, 2 실패

        struct CatalogPanelInternal
        {
            [[nodiscard]] static bool readFileCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( s_catalogFileCount.load() );
                return true;
            }

            [[nodiscard]] static bool readRowCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( s_catalogRowCount.load() );
                return true;
            }

            [[nodiscard]] static bool readValidation( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( s_catalogValidation.load() );
                return true;
            }

            static constexpr float32 kFileListWidth = 300.0f; ///< 96 DPI 기준입니다
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    CatalogPanel::CatalogPanel()
        : IEditorPanel( false )
        , _filter{}
        , _listFile{}
        , _listWarning{}
        , _indexJob{}
        , _document{}
        , _tableEditor{}
        , _selectedID{}
        , _selectedKindLabel{}
        , _previousFileText{}
        , _statusText{}
        , _validationState{ ValidationState::None }
        , _bIndexLoaded{ false }
        , _bRawMode{ false }
    {
    }

    void CatalogPanel::drawContent()
    {
        pollIndexJob();
        drawToolbar();
        ImGui::Separator();

        EditorSectionDesc listDesc{};
        listDesc._pID       = "##CatalogFileList";
        listDesc._kind      = EditorSectionKind::Child;
        listDesc._childSize = float2{ CatalogPanelInternal::kFileListWidth, 0.0f };
        listDesc._flags     = EditorSectionFlags::Border | EditorSectionFlags::ResizeX;
        if ( EditorChrome::beginSection( listDesc ) )
            drawFileList();
        EditorChrome::endSection();

        ImGui::SameLine();
        ImGui::BeginGroup();
        drawDocumentArea();
        ImGui::EndGroup();
    }

    void CatalogPanel::pollIndexJob()
    {
        vector<EditorResourceIndexEntry> listEntry;
        const EditorTaskPoll             poll = _indexJob.take( listEntry );
        if ( poll == EditorTaskPoll::Ready )
        {
            rebuildFileList( listEntry );
            _bIndexLoaded = true;
        }
        else if ( _bIndexLoaded == false && poll == EditorTaskPoll::Idle )
        {
            _indexJob.request();
        }
    }

    void CatalogPanel::rebuildFileList( const vector<EditorResourceIndexEntry>& listEntry )
    {
        _listFile.clear();
        vector<const ResourceDataKind*> listKind;
        for ( const EditorResourceIndexEntry& entry : listEntry )
        {
            ResourceDataKindUtil::collectMatches( entry._path, listKind );
            if ( listKind.size() != 1 )
                continue; // 카탈로그가 아니거나(0) 종류가 겹치는(스키마 테스트가 잡는다) 파일은 이 창이 다루지 않는다
            FileRow row;
            row._resourceID = entry._path;
            row._kindLabel  = listKind[0]->_pLabel;
            _listFile.push_back( std::move( row ) );
        }
        std::sort( _listFile.begin(), _listFile.end(), &CatalogPanel::isFileBefore );
        s_catalogFileCount.store( static_cast<int32>( _listFile.size() ) );
    }

    bool CatalogPanel::isFileBefore( const FileRow& lhs, const FileRow& rhs )
    {
        const int32 byKind = StringUtil::compare( lhs._kindLabel, rhs._kindLabel );
        return byKind != 0 ? byKind < 0 : StringUtil::compare( lhs._resourceID, rhs._resourceID ) < 0;
    }

    void CatalogPanel::drawToolbar()
    {
        if ( ImGui::Button( "Refresh files" ) )
            _bIndexLoaded = false;
        EditorSelfTestMarks::note( "catalog.refresh" );
        ImGui::SameLine();
        ImGui::BeginDisabled( _selectedID.empty() );
        if ( ImGui::Button( "Save" ) )
            (void)saveDocumentAndClearDirty(); // 실패하면 상태 줄에 이유가 있다
        EditorSelfTestMarks::note( "catalog.save" );
        ImGui::SameLine();
        if ( ImGui::Button( "Revert" ) )
            revertDocument();
        EditorSelfTestMarks::note( "catalog.revert" );
        ImGui::SameLine();
        ImGui::BeginDisabled( _document.canUndo() == false );
        if ( ImGui::Button( "Undo" ) )
        {
            SW_LOG_INFO( "Catalog undo" );
            _document.undo();
            _tableEditor.reset();
            syncDirty();
        }
        EditorSelfTestMarks::note( "catalog.undo" );
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( _document.canRedo() == false );
        if ( ImGui::Button( "Redo" ) )
        {
            _document.redo();
            _tableEditor.reset();
            syncDirty();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if ( ImGui::Button( "Validate" ) )
        {
            validateCurrentFile();
            syncDirty(); // 검증 결과 탐침(`Editor.CatalogValidation`)도 여기서 갱신된다 — 저장 · 복원 길과 같다
        }
        EditorSelfTestMarks::note( "catalog.validate" );
        ImGui::SameLine();
        ImGui::Checkbox( "Raw text", &_bRawMode );
        ImGui::EndDisabled();
    }

    void CatalogPanel::drawFileList()
    {
        EditorWidgets::drawSearchField( "##catalogFilter", _filter, "Filter files", -1.0f, false );
        if ( _bIndexLoaded == false )
        {
            EditorWidgets::drawEmptyHint( "Scanning Resource..." );
            return;
        }
        if ( _listFile.empty() )
        {
            EditorWidgets::drawEmptyHint( "No catalog kinds are registered" );
            return;
        }

        const string_view filter     = _filter.c_str();
        bool              bFirstFile = true;
        size_t            index      = 0;
        while ( index < _listFile.size() )
        {
            size_t end = index;
            while ( end < _listFile.size() && _listFile[end]._kindLabel == _listFile[index]._kindLabel )
            {
                ++end;
            }
            StringBuilder<constant::kMaxBuffer128> header;
            header.appendFormat( "%# (%#)###kind_%#", _listFile[index]._kindLabel, end - index, _listFile[index]._kindLabel );
            if ( ImGui::CollapsingHeader( header.c_str(), ImGuiTreeNodeFlags_DefaultOpen ) )
            {
                for ( size_t fileIndex = index; fileIndex < end; ++fileIndex )
                {
                    const FileRow& file = _listFile[fileIndex];
                    if ( filter.empty() == false && StringUtil::contains( file._resourceID, filter, true ) == false )
                        continue;
                    const bool bSelected = file._resourceID == _selectedID;
                    if ( ImGui::Selectable( file._resourceID.c_str(), bSelected ) && bSelected == false )
                    {
                        if ( _document.isDirty() )
                            _statusText = "Save or revert the open file first";
                        else
                            openFile( file._resourceID );
                    }
                    if ( bFirstFile )
                    {
                        EditorSelfTestMarks::note( "catalog.file.first" );
                        bFirstFile = false;
                    }
                }
            }
            index = end;
        }
    }

    void CatalogPanel::drawDocumentArea()
    {
        if ( _selectedID.empty() )
        {
            EditorWidgets::drawEmptyHint( "Pick a catalog file on the left" );
            return;
        }

        ImGui::Text( "%s", _selectedID.c_str() );
        ImGui::SameLine();
        ImGui::TextDisabled( "[%s]%s", _selectedKindLabel.c_str(), _document.isDirty() ? " *" : "" );
        drawValidation();
        ImGui::Separator();

        if ( _bRawMode || _document.isParsed() == false )
        {
            if ( _document.isParsed() == false )
                ImGui::TextColored( ImVec4{ 0.95f, 0.45f, 0.35f, 1.0f }, "The file is not valid XML: %s", _document.getError().c_str() );
            string       raw       = _document.getText();
            const uint32 lineCount = static_cast<uint32>( ImGui::GetContentRegionAvail().y / ImGui::GetTextLineHeightWithSpacing() ) - 1;
            if ( EditorWidgets::drawTextFieldMultiline( "##catalogRaw", raw, lineCount > 4 ? lineCount : 4 ) )
            {
                _document.replaceText( raw, false );
                syncDirty();
            }
            return;
        }

        if ( _tableEditor.draw( _document ) )
        {
            _statusText = _tableEditor.getLastActionText();
            syncDirty();
        }
    }

    void CatalogPanel::drawValidation()
    {
        if ( _statusText.empty() == false )
            ImGui::TextDisabled( "%s", _statusText.c_str() );
        if ( _validationState == ValidationState::Passed )
        {
            ImGui::TextColored( ImVec4{ 0.45f, 0.85f, 0.45f, 1.0f }, "Validation passed" );
        }
        else if ( _validationState == ValidationState::Failed )
        {
            ImGui::TextColored( ImVec4{ 0.95f, 0.45f, 0.35f, 1.0f }, "Validation failed (%d warnings)", static_cast<int32>( _listWarning.size() ) );
            ImGui::SameLine();
            if ( ImGui::SmallButton( "Restore previous file" ) )
                restorePreviousFile();
            EditorSelfTestMarks::note( "catalog.restore" );
            for ( const string& warning : _listWarning )
            {
                ImGui::BulletText( "%s", warning.c_str() );
            }
        }
    }

    void CatalogPanel::openFile( const string& resourceID )
    {
        const string absolutePath = ResourcePaths::findLooseFile( resourceID ).getDiskPath(); // 붙인 도메인과 게임 폴더를 거치므로 루트에 직접 붙이지 않는다(없으면 빈 문자열)
        string       text;
        // 읽지 못하면 고르지 않는다 — 앞 파일의 글을 가진 채 고르면 저장할 때 앞 파일의 내용을 이 파일에 쓴다.
        if ( FileUtil::readTextFile( absolutePath, text ) == false )
        {
            SW_LOG_ERROR( "Could not read catalog file '%#'", absolutePath.c_str() );
            _statusText = "Could not read the file";
            return;
        }
        _selectedID.clear();
        _selectedKindLabel.clear();
        vector<const ResourceDataKind*> listKind;
        ResourceDataKindUtil::collectMatches( resourceID, listKind );
        if ( listKind.size() == 1 )
            _selectedKindLabel = listKind[0]->_pLabel;
        _selectedID = resourceID;
        (void)_document.load( text ); // 깨진 원문은 글 상자로 고치게 열어 둔다 — 이유는 문서가 안다
        _tableEditor.reset();
        _listWarning.clear();
        _validationState  = ValidationState::None;
        _previousFileText = text;
        _statusText.clear();
        syncDirty();
        SW_LOG_INFO( "Catalog opened %#", resourceID.c_str() );
    }

    bool CatalogPanel::writeCurrentFile()
    {
        if ( _selectedID.empty() )
            return false;
        if ( _document.isParsed() == false )
        {
            _statusText = "Fix the XML before saving";
            return false;
        }
        const string absolutePath = ResourcePaths::resolveWritePath( _selectedID );
        if ( FileUtil::writeTextFile( absolutePath, _document.getText() ) == false )
        {
            SW_LOG_ERROR( "Could not write catalog file '%#'", absolutePath.c_str() );
            _statusText = "Could not write the file";
            return false;
        }
        _previousFileText = _document.getSavedText(); // 저장 직전에 디스크에 있던 글
        _document.markSaved();
        SW_LOG_INFO( "Catalog saved %#", _selectedID.c_str() );
        return true;
    }

    bool CatalogPanel::saveDocument()
    {
        if ( writeCurrentFile() == false )
            return false;
        validateCurrentFile();
        syncDirty();
        return true;
    }

    void CatalogPanel::revertDocument()
    {
        _document.revert();
        _tableEditor.reset();
        _statusText = "Reverted to the saved text";
        syncDirty();
        SW_LOG_INFO( "Catalog reverted %#", _selectedID.c_str() );
    }

    void CatalogPanel::validateCurrentFile()
    {
        _listWarning.clear();
        _validationState              = ValidationState::None;
        const ResourceDataKind* pKind = _selectedKindLabel.empty() ? nullptr : ResourceDataKindUtil::findByLabel( _selectedKindLabel );
        if ( pKind == nullptr )
        {
            _statusText = "No registered kind - nothing to validate with";
            return;
        }
        bool bLoaded = false;
        {
            CatalogWarningCollector collector;
            bLoaded      = pKind->_pLoad( _selectedID );
            _listWarning = collector.take();
        }
        const bool bPassed = bLoaded && _listWarning.empty();
        _statusText        = _document.isDirty() ? "Validated the saved file - unsaved edits are not included" : string();
        _validationState   = bPassed ? ValidationState::Passed : ValidationState::Failed;
        if ( bPassed )
            SW_LOG_INFO( "Catalog validation passed (%#)", _selectedKindLabel.c_str() );
        else
            SW_LOG_INFO( "Catalog validation failed (%#): %# warnings", _selectedKindLabel.c_str(), _listWarning.size() );
    }

    void CatalogPanel::restorePreviousFile()
    {
        if ( _selectedID.empty() )
            return;
        const string absolutePath = ResourcePaths::resolveWritePath( _selectedID );
        if ( FileUtil::writeTextFile( absolutePath, _previousFileText ) == false )
        {
            SW_LOG_ERROR( "Could not restore catalog file '%#'", absolutePath.c_str() );
            _statusText = "Could not restore the file";
            return;
        }
        (void)_document.load( _previousFileText ); // 되돌린 글이 디스크와 문서의 새 기준이다
        _tableEditor.reset();
        _statusText = "Restored the file as it was before saving";
        validateCurrentFile();
        syncDirty();
        SW_LOG_INFO( "Catalog restored %#", _selectedID.c_str() );
    }

    void CatalogPanel::syncDirty()
    {
        int32 rowCount = 0;
        for ( const CatalogDocument::Group& group : _document.getGroups() )
        {
            rowCount += static_cast<int32>( group._listRow.size() );
        }
        s_catalogRowCount.store( rowCount );
        s_catalogValidation.store( static_cast<int32>( _validationState ) );
        if ( _document.isDirty() )
            markDocumentDirty();
        else
            clearDocumentDirty();
    }

    SW_EDITOR_PANEL( CatalogPanel, CatalogPanel::kPanelID, EditorPanelCategory::Tool, 1710 );
} // namespace sw::editor

namespace sw
{
    SW_AUTOMATION_PROBE( catalogFileCount, "Editor.CatalogFileCount", "Catalog files the Catalog Editor lists (0 until its scan finishes)",
                         &editor::CatalogPanelInternal::readFileCount );
    SW_AUTOMATION_PROBE( catalogRowCount, "Editor.CatalogRowCount", "Rows (elements under the root) of the catalog file open in the Catalog Editor",
                         &editor::CatalogPanelInternal::readRowCount );
    SW_AUTOMATION_PROBE( catalogValidation, "Editor.CatalogValidation", "Last validation of the open catalog file: 0 not run, 1 passed, 2 failed",
                         &editor::CatalogPanelInternal::readValidation );
} // namespace sw
