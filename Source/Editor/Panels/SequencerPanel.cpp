#include "pch.h"

#include "Editor/Panels/SequencerPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Commands/EditorViewportPreview.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyLayout.h"

#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayer.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <ImSequencer.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( SequencerPanel, "sequencer", EditorPanelCategory::Tool, 1000 );

    /**
     * @brief ImSequencer 가 보는 시퀀스입니다. 항목은 애셋의 `SequenceTrackItem` 그대로입니다.
     * @details 패널 전용 항목 타입을 따로 두지 않습니다 — 저장 · 복원 때마다 필드를 하나씩 옮기면, 애셋에 필드가 늘 때
     *          빠뜨린 필드만 조용히 저장되지 않습니다.
     */
    struct ClipSequence : ImSequencer::SequenceInterface
    {
        int32                     _frameMin{ 0 };
        int32                     _frameMax{ 100 };
        vector<SequenceTrackItem> _listItem;
        mutable string            _labelScratch;

        int32 GetFrameMin() const override
        {
            return _frameMin;
        }
        int32 GetFrameMax() const override
        {
            return _frameMax;
        }
        int32 GetItemCount() const override
        {
            return static_cast<int32>( _listItem.size() );
        }
        int32 GetItemTypeCount() const override
        {
            return static_cast<int32>( SequenceItemKind::Count );
        }
        const utf8* GetItemTypeName( int32 typeIndex ) const override
        {
            const TypeRegistry* pRegistry = editor::getService<TypeRegistry>();
            const utf8*         pName     = pRegistry != nullptr ? pRegistry->enumToString( static_cast<SequenceItemKind>( typeIndex ) ) : nullptr;
            return pName != nullptr ? pName : "?";
        }
        const utf8* GetItemLabel( int32 itemIndex ) const override
        {
            if ( 0 <= itemIndex && itemIndex < static_cast<int32>( _listItem.size() ) && _listItem[static_cast<size_t>( itemIndex )]._name.empty() == false )
                return _listItem[static_cast<size_t>( itemIndex )]._name.c_str();
            _labelScratch = "Item ";
            _labelScratch += to_string( itemIndex );
            return _labelScratch.c_str();
        }

        void Get( int32 itemIndex, int32** ppStart, int32** ppEnd, int32* pType, uint32* pColor ) override
        {
            SequenceTrackItem& item = _listItem[static_cast<size_t>( itemIndex )];
            if ( ppStart != nullptr )
                *ppStart = &item._start;
            if ( ppEnd != nullptr )
                *ppEnd = &item._end;
            if ( pType != nullptr )
                *pType = static_cast<int32>( item._kind );
            if ( pColor != nullptr )
                *pColor = item._color;
        }

        void Add( int32 type ) override
        {
            SequenceTrackItem item{};
            item._kind                        = static_cast<SequenceItemKind>( type );
            item._start                       = _frameMin;
            item._end                         = _frameMin + 10;
            const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( item._kind );
            if ( pInfo != nullptr )
                item._color = pInfo->_defaultColor;
            item._name = string( GetItemTypeName( type ) ) + " " + to_string( _listItem.size() );
            _listItem.push_back( item );
        }

        void Del( int32 itemIndex ) override
        {
            if ( 0 <= itemIndex && itemIndex < static_cast<int32>( _listItem.size() ) )
                _listItem.erase( _listItem.begin() + itemIndex );
        }

        void Duplicate( int32 itemIndex ) override
        {
            if ( 0 <= itemIndex && itemIndex < static_cast<int32>( _listItem.size() ) )
            {
                SequenceTrackItem copy = _listItem[static_cast<size_t>( itemIndex )];
                copy._name += " Copy";
                _listItem.push_back( std::move( copy ) );
            }
        }
    };

    SequencerPanel::SequencerPanel()
        : EditorDocumentPanel{ EditorAssetType::Sequence, false }
        , _cinematicNote{ "Cinematic notes (not a clip track)." }
        , _sequence{ make_unique<ClipSequence>() }
        , _previewPlayer{ make_unique<sw::SequencePlayer>() }
        , _listTimingBefore{}
        , _currentFrame{ 0 }
        , _selected{ -1 }
        , _firstFrame{ 0 }
        , _bExpanded{ true }
    {
        _sequence->Add( static_cast<int32>( SequenceItemKind::Clip ) );
        _sequence->Add( static_cast<int32>( SequenceItemKind::Event ) );
        _sequence->_listItem[0]._name  = "Intro";
        _sequence->_listItem[1]._name  = "Cut";
        _sequence->_listItem[1]._start = 20;
        _sequence->_listItem[1]._end   = 40;
    }

    SequencerPanel::~SequencerPanel() = default;

    void SequencerPanel::drawContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();

        if ( EditorChrome::beginToolbar( "##SequencerToolbar" ) )
        {
            if ( ImGui::Button( "Load" ) )
                reloadDocument();
            ImGui::SameLine();
            if ( ImGui::Button( "Save" ) )
                saveToLoadedPath();
            ImGui::SameLine();
            if ( ImGui::Button( "Play" ) )
            {
                syncPreviewPlayer();
                _previewPlayer->playFromFrame( _currentFrame );
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Pause" ) )
                _previewPlayer->pause();
            ImGui::SameLine();
            if ( ImGui::Button( "Stop" ) )
            {
                _previewPlayer->stop();
                _currentFrame = _sequence != nullptr ? _sequence->_frameMin : 0;
            }
            ImGui::SameLine();
            if ( getLoadedAssetPath().empty() )
                ImGui::TextDisabled( "No .seq file focused" );
            else
                ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        }
        EditorChrome::endToolbar();

        tickPreview( ImGui::GetIO().DeltaTime );

        ImGui::SliderInt( "Scrub Frame", &_currentFrame, _sequence->_frameMin, _sequence->_frameMax );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
        {
            if ( _previewPlayer->isPlaying() )
                _previewPlayer->seekToFrame( _currentFrame );
            EditorViewportPreview::applySequenceFrame( captureAsset(), _currentFrame );
        }
        ImGui::Text( "Current Frame: %d", _currentFrame );

        ImGui::InputTextMultiline( "Cinematic Note", _cinematicNote.data(), _cinematicNote.capacity(), ImVec2( -1.0f, 60.0f ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Sequence Note", "sequence-note" );

        constexpr float32 kClipFieldRowCount = 5.0f; // 이름 · 대상 · 이동 · 회전 · 크기
        // 고른 클립의 편집 칸은 늘 같은 높이의 구역에 그린다 — 고를 때만 칸이 생기면 그만큼 타임라인이 밀려 내려간다.
        EditorSectionDesc clipDesc{};
        clipDesc._pId       = "##sequence_clip";
        clipDesc._kind      = EditorSectionKind::Child;
        clipDesc._childSize = float2{ 0.0f, ImGui::GetFrameHeightWithSpacing() * kClipFieldRowCount };
        EditorChrome::beginSection( clipDesc );
        if ( 0 <= _selected && _selected < static_cast<int32>( _sequence->_listItem.size() ) )
        {
            SequenceTrackItem& item = _sequence->_listItem[static_cast<size_t>( _selected )];
            EditorWidgets::drawTextField( "Clip Name", item._name );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sequence Clip", "sequence-clip" );
            EditorWidgets::drawTextField( "Target Object", item._targetObject );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sequence Clip", "sequence-clip" );
            float32 arrTranslation[3] = { item._translation._x, item._translation._y, item._translation._z };
            if ( ImGui::DragFloat3( "Translation", arrTranslation, 0.1f ) )
            {
                item._translation._x = arrTranslation[0];
                item._translation._y = arrTranslation[1];
                item._translation._z = arrTranslation[2];
            }
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sequence Clip", "sequence-clip" );
            // 회전 델타는 라디안으로 저장하고(`setLocalRotation` 에 그대로 간다) 도로 보이고 고친다 — 인스펙터의 트랜스폼 섹션과 같은 규칙.
            float32 arrRotation[3] = { item._rotation._x * MathUtil::kRadianToDegree, item._rotation._y * MathUtil::kRadianToDegree,
                                       item._rotation._z * MathUtil::kRadianToDegree };
            if ( ImGui::DragFloat3( "Rotation", arrRotation, InspectorPropertyLayout::kAngleDragSpeed, 0.0f, 0.0f, "%.2f deg" ) )
            {
                item._rotation._x = arrRotation[0] * MathUtil::kDegreeToRadian;
                item._rotation._y = arrRotation[1] * MathUtil::kDegreeToRadian;
                item._rotation._z = arrRotation[2] * MathUtil::kDegreeToRadian;
            }
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sequence Clip", "sequence-clip" );
            float32 arrScale[3] = { item._scale._x, item._scale._y, item._scale._z };
            if ( ImGui::DragFloat3( "Scale", arrScale, 0.01f ) )
            {
                item._scale._x = arrScale[0];
                item._scale._y = arrScale[1];
                item._scale._z = arrScale[2];
            }
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sequence Clip", "sequence-clip" );
        }
        else
        {
            EditorWidgets::drawEmptyHint( "Select a clip on the timeline to edit it." );
        }
        EditorChrome::endSection();

        // ImSequencer 는 여러 항목을 그리는 위젯이라 IsItemEdited("마지막 항목")가 클립 끌기 · 더하기 · 지우기를 뜻하지 않는다 — 부르기 전후의 배치를 비교한다.
        SequenceTimingUtil::captureTiming( _sequence->_listItem, _listTimingBefore );
        ImSequencer::Sequencer( _sequence.get(), &_currentFrame, &_bExpanded, &_selected, &_firstFrame,
                                ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_ADD | ImSequencer::SEQUENCER_DEL | ImSequencer::SEQUENCER_CHANGE_FRAME );
        if ( SequenceTimingUtil::hasTimingChanged( _listTimingBefore, _sequence->_listItem ) )
            notifyDocumentEdited( "Edit Sequence Timeline", "sequence-timeline" );
    }

    ToolAssetLoadResult SequencerPanel::loadDocument()
    {
        if ( _sequence == nullptr )
            return ToolAssetLoadResult::Missing;
        string path = getLoadedAssetPath();
        if ( path.empty() )
            path = string{ getMatchingFocusedPath() };
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;

        // 읽지 못하면 그대로 둔다 — 결과를 돌려주므로 기반이 표시하고 저장을 막는다(표시 없이 돌아가면 프레임마다 다시 읽는다).
        SequenceAsset             asset;
        const ToolAssetLoadResult result = EditorToolAssetCommands::loadSequence( asset, path );
        if ( getLoadedAssetPath().empty() )
            acceptFocusedDocument();
        if ( result == ToolAssetLoadResult::Loaded )
            applyAsset( asset );
        return result;
    }

    void SequencerPanel::saveToLoadedPath()
    {
        if ( getLoadedAssetPath().empty() || _sequence == nullptr )
            return;
        if ( EditorToolAssetCommands::saveSequence( captureAsset(), getLoadedAssetPath() ) == false )
            return;
        clearDocumentDirty();
        syncDocumentUndoBaseline();
    }

    bool SequencerPanel::saveDocument()
    {
        if ( getLoadedAssetPath().empty() )
            return false;
        saveToLoadedPath();
        return isDocumentDirty() == false;
    }

    SequenceAsset SequencerPanel::captureAsset() const
    {
        SequenceAsset asset;
        if ( _sequence == nullptr )
            return asset;
        asset._frameMin = _sequence->_frameMin;
        asset._frameMax = _sequence->_frameMax;
        asset._note     = _cinematicNote.c_str();
        asset._listItem = _sequence->_listItem;
        return asset;
    }

    void SequencerPanel::applyAsset( const SequenceAsset& asset )
    {
        if ( _sequence == nullptr )
            return;
        _sequence->_frameMin = asset._frameMin;
        _sequence->_frameMax = asset._frameMax;
        _cinematicNote       = asset._note.c_str();
        _sequence->_listItem = asset._listItem;
    }

    string SequencerPanel::captureDocumentText() const
    {
        return captureAsset().toJson();
    }

    void SequencerPanel::applyDocumentText( string_view text )
    {
        SequenceAsset restored;
        if ( text.empty() == false && restored.parseJson( text ) == false )
            SW_LOG_WARNING( "Sequence undo snapshot could not be read - showing an empty sequence" );
        applyAsset( restored );
    }

    void SequencerPanel::syncPreviewPlayer()
    {
        if ( _previewPlayer == nullptr )
            return;
        _previewPlayer->setAsset( captureAsset() );
        _previewPlayer->setLoop( true );
    }

    void SequencerPanel::tickPreview( float32 deltaSeconds )
    {
        if ( _previewPlayer == nullptr || _previewPlayer->isPlaying() == false )
            return;
        _previewPlayer->update( deltaSeconds );
        _currentFrame = _previewPlayer->getCurrentFrame();
        EditorViewportPreview::applySequenceFrame( _previewPlayer->getAsset(), _currentFrame );
        if ( _previewPlayer->isPlaying() == false )
            return;
        vector<const SequenceTrackItem*> listActive;
        _previewPlayer->collectActiveItems( listActive );
        (void)listActive;
    }
} // namespace sw::editor
