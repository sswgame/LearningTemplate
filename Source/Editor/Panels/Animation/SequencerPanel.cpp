#include "pch.h"

#include "Editor/Panels/Animation/SequencerPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/Commands/Scene/EditorViewportPreview.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Inspector/Model/InspectorPropertyLayout.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayer.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <ImSequencer.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( SequencerPanel, SequencerPanel::kPanelID, EditorPanelCategory::Tool, 1000 );

    /**
     * @brief ImSequencer 가 보는 시퀀스입니다. 항목은 문서의 `SequenceTrackItem` 그대로이고, 더하기 · 지우기 · 사본은 문서의 명령입니다.
     * @details 위젯의 종류 번호는 만들 수 있는 종류의 순번입니다(`SequenceDocument::getAddableItemKind`) — 위젯은 그 번호를 더하기 목록에만 씁니다.
     */
    struct ClipSequence : ImSequencer::SequenceInterface
    {
        SequenceDocument& _document;
        mutable string    _labelScratch;

        explicit ClipSequence( SequenceDocument& document )
            : _document{ document }
            , _labelScratch{}
        {
        }

        int32 GetFrameMin() const override
        {
            return _document._asset._frameMin;
        }
        int32 GetFrameMax() const override
        {
            return _document._asset._frameMax;
        }
        int32 GetItemCount() const override
        {
            return static_cast<int32>( _document._asset._listItem.size() );
        }
        int32 GetItemTypeCount() const override
        {
            return static_cast<int32>( SequenceDocument::getAddableItemKindCount() );
        }
        const utf8* GetItemTypeName( int32 typeIndex ) const override
        {
            const utf8* pName = editor::getSubsystem<TypeRegistry>().enumToString( SequenceDocument::getAddableItemKind( static_cast<uint32>( typeIndex ) ) );
            return pName != nullptr ? pName : "?"; // 라이브러리가 const utf8* 를 요구해 사유 문자열을 줄 수 없다
        }
        const utf8* GetItemLabel( int32 itemIndex ) const override
        {
            const vector<SequenceTrackItem>& listItem     = _document._asset._listItem;
            const bool                       bHasItemName = 0 <= itemIndex && itemIndex < static_cast<int32>( listItem.size() ) && listItem[static_cast<size_t>( itemIndex )]._name.empty() == false;
            if ( bHasItemName )
                return listItem[static_cast<size_t>( itemIndex )]._name.c_str();
            _labelScratch = "Item ";
            _labelScratch += to_string( itemIndex );
            return _labelScratch.c_str();
        }

        void Get( int32 itemIndex, int32** ppStart, int32** ppEnd, int32* pType, uint32* pColor ) override
        {
            SequenceTrackItem& item = _document._asset._listItem[static_cast<size_t>( itemIndex )];
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
            (void)_document.addItem( SequenceDocument::getAddableItemKind( static_cast<uint32>( type ) ), GetItemTypeName( type ) );
        }

        void Del( int32 itemIndex ) override
        {
            (void)_document.removeItem( itemIndex );
        }

        void Duplicate( int32 itemIndex ) override
        {
            (void)_document.duplicateItem( itemIndex );
        }
    };

    namespace
    {
        struct SequencerPanelInternal
        {
            /** @brief 트랙 이름 필드의 폭(픽셀, DPI 전)입니다. 나머지 폭이 키 띠다. */
            static constexpr float32 kTrackLabelWidth = 180.0f;
            /** @brief 커브 편집기 구역의 높이(픽셀, DPI 전)입니다. */
            static constexpr float32 kCurveAreaLines = 12.0f; ///< 커브 영역 높이(글자 줄 수)
            /** @brief 키 마름모의 반지름(픽셀, DPI 전)입니다. */
            static constexpr float32 kKeyMarkerRadius = 4.0f;

            /**
             * @brief 에디터에서 고른 오브젝트의 이름입니다. 고른 것이 없으면 빈 문자열입니다.
             * @details 트랙은 이름으로 레벨의 오브젝트에 묶여 레벨에서 재생된다 — 메인 씬의 선택이다(언리얼 시퀀서도 레벨 선택의 액터를 트랙에 더한다).
             */
            static string findSelectedObjectName()
            {
                GameObject* pObject = editor::getMainSceneSelection().getPrimaryObject();
                return pObject != nullptr ? string{ pObject->getName().c_str() } : string{};
            }
        };
    } // namespace

    SequencerPanel::SequencerPanel()
        : EditorDocumentPanel{ EditorAssetType{ SequenceAsset::kAssetType }, false }
        , _cinematicNote{ "Cinematic notes (not a clip track)." }
        , _document{}
        , _sequence{ make_unique<ClipSequence>( _document ) }
        , _previewPlayer{ make_unique<sw::SequencePlayer>() }
        , _listTimingBefore{}
        , _listKeyFrame{}
        , _curveState{}
        , _currentFrame{ 0 }
        , _firstFrame{ 0 }
        , _bExpanded{ true }
    {
        (void)_document.addItem( SequenceItemKind::Clip, "Clip" );
        (void)_document.addItem( SequenceItemKind::Event, "Event" );
        vector<SequenceTrackItem>& listItem = _document._asset._listItem;
        listItem[0]._name                   = "Intro";
        listItem[1]._name                   = "Cut";
        listItem[1]._start                  = 20;
        listItem[1]._end                    = 40;
    }

    SequencerPanel::~SequencerPanel() = default;

    void SequencerPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        drawDocumentOpenBar( "sequencer" );

        if ( EditorChrome::beginToolbar( "##SequencerToolbar" ) )
        {
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Load" ) ) )
                reloadDocument();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "LoadTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Save" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) ) )
                saveToLoadedPath();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "SaveTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Play" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Play" ) ) )
            {
                syncPreviewPlayer();
                _previewPlayer->playFromFrame( _currentFrame );
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "PlayTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Pause" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "Pause" ) ) )
                _previewPlayer->pause();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "PauseTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Stop" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Stop" ) ) )
            {
                _previewPlayer->stop();
                _currentFrame = _document._asset._frameMin;
                previewCurrentFrame();
                SequenceTimelineUtil::releaseCameraCut( editor::getActiveObjectManager(), _previewPlayer->getAsset() );
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "StopTip" ) );
            ImGui::SameLine();
            if ( getLoadedAssetPath().empty() )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "NoSeqFileFocused" ) );
            else
                ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        }
        EditorChrome::endToolbar();

        updatePreview( ImGui::GetIO().DeltaTime );

        // 끄는 동안에도 적용한다 — 시간 막대를 옮기면 뷰포트가 따라온다(언리얼 Sequencer · 유니티 Timeline 의 스크럽).
        ImGui::SetNextItemWidth( -1.0f );
        if ( ImGui::SliderInt( "##ScrubFrame", &_currentFrame, _document._asset._frameMin, _document._asset._frameMax, "Frame %d" ) )
        {
            if ( _previewPlayer->isPlaying() )
                _previewPlayer->seekToFrame( _currentFrame );
            previewCurrentFrame();
        }
        EditorSelfTestMarks::note( "sequencer.scrub" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "ScrubTip" ) );

        // 키 트랙이 위, 구간 항목이 아래다 — 자주 고치는 쪽(키 · 커브)이 패널이 낮아도 보인다.
        drawKeyTracks();

        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "ClipsEventsAndCameraCuts" ) );
        drawItemFields();

        // ImSequencer 는 여러 항목을 그리는 위젯이라 IsItemEdited("마지막 항목")가 클립 끌기 · 더하기 · 지우기를 뜻하지 않는다 — 부르기 전후의 배치를 비교한다.
        const vector<SequenceTrackItem>& listItem = _document._asset._listItem;
        SequenceClipTiming::captureTiming( listItem, _listTimingBefore );
        const int32  frameBefore     = _currentFrame;
        const size_t itemCountBefore = listItem.size();
        int32        selectedItem    = _document._selectedItem;
        ImSequencer::Sequencer( _sequence.get(), &_currentFrame, &_bExpanded, &selectedItem, &_firstFrame,
                                ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_ADD | ImSequencer::SEQUENCER_DEL | ImSequencer::SEQUENCER_CHANGE_FRAME );
        // 위젯은 지운 항목 뒤의 선택을 당겨 주지 않는다(이름 칸이 옆 항목을 고친다) — 지웠으면 문서(`removeItem`)가 맞춘 번호를 쓴다.
        if ( listItem.size() >= itemCountBefore )
            _document._selectedItem = selectedItem;
        if ( SequenceClipTiming::hasTimingChanged( _listTimingBefore, listItem ) )
            notifyDocumentEdited( "Edit Sequence Timeline", "sequence-timeline" );
        if ( _currentFrame != frameBefore )
            previewCurrentFrame();

        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "CinematicNote" ) ); // 폭 전체 칸이라 이름표는 위에(오른쪽 이름표는 창 밖으로 잘린다)
        ImGui::InputTextMultiline( "##CinematicNote", _cinematicNote.data(), _cinematicNote.capacity(), ImVec2( -1.0f, 40.0f * EditorThemeUtil::getDPIScale() ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Sequence Note", "sequence-note" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "CinematicNoteTip" ) );
    }

    void SequencerPanel::drawItemFields()
    {
        constexpr float32 kClipFieldRowCount = 2.0f; // 이름 · 대상
        // 고른 항목의 편집 필드는 늘 같은 높이의 구역에 그린다 — 고를 때만 필드가 생기면 그만큼 타임라인이 밀려 내려간다.
        EditorSectionDesc clipDesc{};
        clipDesc._pID       = "##sequence_clip";
        clipDesc._kind      = EditorSectionKind::Child;
        clipDesc._childSize = float2{ 0.0f, ImGui::GetFrameHeightWithSpacing() * kClipFieldRowCount };
        EditorChrome::beginSection( clipDesc );
        if ( 0 <= _document._selectedItem && _document._selectedItem < static_cast<int32>( _document._asset._listItem.size() ) )
        {
            SequenceTrackItem& item = _document._asset._listItem[static_cast<size_t>( _document._selectedItem )];
            EditorWidgets::drawTextField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "ItemName" ), "Item Name" ), item._name );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sequence Item", "sequence-item" );
            EditorWidgets::drawTextField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "TargetObject" ), "Target Object" ), item._targetObject );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sequence Item", "sequence-item" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "ItemTargetTip" ) );
        }
        else
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSequencer", "SelectItemHint" ) );
        }
        EditorChrome::endSection();
    }

    void SequencerPanel::drawKeyTracks()
    {
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "KeyTracks" ) );
        if ( ImGui::Button( "+ Transform Track" ) )
            addKeyTrack( SequenceTrackKind::Transform );
        EditorSelfTestMarks::note( "sequencer.addTransformTrack" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "AddTransformTrackTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "+ Property Track" ) );
        if ( ImGui::Button( "+ Property Track" ) )
            addKeyTrack( SequenceTrackKind::Property );
        EditorSelfTestMarks::note( "sequencer.addPropertyTrack" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "AddPropertyTrackTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "+ Rig Slot Track" ) );
        if ( ImGui::Button( "+ Rig Slot Track" ) )
            addKeyTrack( SequenceTrackKind::RigSlot );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "AddRigSlotTrackTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "DeleteTrack" ) ) );
        ImGui::BeginDisabled( _document._selectedTrack < 0 );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "DeleteTrack" ) ) && _document.removeSelectedTrack() )
            notifyDocumentEdited( "Delete Sequence Track" );
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "DeleteTrackTip" ) );

        const vector<SequenceKeyTrack>& listTrack = _document._asset._listTrack;
        if ( listTrack.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSequencer", "NoKeyTracksHint" ) );
            return;
        }

        const float32 labelWidth = SequencerPanelInternal::kTrackLabelWidth * EditorThemeUtil::getDPIScale();
        for ( int32 trackIndex = 0; trackIndex < static_cast<int32>( listTrack.size() ); ++trackIndex )
        {
            const SequenceKeyTrack& track = listTrack[static_cast<size_t>( trackIndex )];
            ImGui::PushID( trackIndex );
            fixed_string<constant::buffer::kMax256> label;
            formatstring( label.data(), label.capacity(), "%# (%#)", track._name.empty() ? "Track" : track._name.c_str(),
                          track._targetObject.empty() ? "no target" : track._targetObject.c_str() );
            if ( ImGui::Selectable( label.c_str(), trackIndex == _document._selectedTrack, ImGuiSelectableFlags_None, ImVec2( labelWidth, 0.0f ) ) )
                _document._selectedTrack = trackIndex;
            fixed_string<constant::buffer::kMax64> mark;
            formatstring( mark.data(), mark.capacity(), "sequencer.track.%#", trackIndex );
            EditorSelfTestMarks::note( mark.c_str() );
            ImGui::SameLine();
            drawKeyStrip( track, trackIndex );
            ImGui::PopID();
        }

        SequenceKeyTrack* pSelectedTrack = _document.findSelectedTrack();
        if ( pSelectedTrack != nullptr )
            drawSelectedTrack( *pSelectedTrack );
        drawChannelCurve();
    }

    void SequencerPanel::drawKeyStrip( const SequenceKeyTrack& track, int32 trackIndex )
    {
        const float32 height = ImGui::GetTextLineHeight();
        const float32 width  = MathUtil::max( ImGui::GetContentRegionAvail().x, 1.0f );
        const ImVec2  origin = ImGui::GetCursorScreenPos();
        if ( ImGui::InvisibleButton( "##keyStrip", ImVec2( width, height ) ) )
        {
            _document._selectedTrack = trackIndex;
            _currentFrame            = _document.findFrameAtRatio( ( EditorInputGUI::readPointer()._position._x - origin.x ) / width );
            previewCurrentFrame();
        }

        ImDrawList*   pDrawList = ImGui::GetWindowDrawList();
        const float32 radius    = SequencerPanelInternal::kKeyMarkerRadius * EditorThemeUtil::getDPIScale();
        const float32 centerY   = origin.y + height * 0.5f;
        pDrawList->AddRectFilled( origin, ImVec2( origin.x + width, origin.y + height ), ImGui::GetColorU32( ImGuiCol_FrameBg ) );
        track.collectKeyFrames( _listKeyFrame );
        for ( const float32 keyFrame : _listKeyFrame )
        {
            const float32 x = origin.x + _document.computeRatioAtFrame( keyFrame ) * width;
            pDrawList->AddQuadFilled( ImVec2( x, centerY - radius ), ImVec2( x + radius, centerY ), ImVec2( x, centerY + radius ), ImVec2( x - radius, centerY ),
                                      ImGui::GetColorU32( ImGuiCol_PlotHistogram ) );
        }
        const float32 cursorX = origin.x + _document.computeRatioAtFrame( static_cast<float32>( _currentFrame ) ) * width;
        pDrawList->AddLine( ImVec2( cursorX, origin.y ), ImVec2( cursorX, origin.y + height ), ImGui::GetColorU32( ImGuiCol_CheckMark ) );
    }

    void SequencerPanel::drawTrackTextField( const utf8* pLabel, string& inoutText, const utf8* pTooltip, bool bPreviewOnEdit )
    {
        // 이름표는 왼쪽 열(인스펙터와 같은 `drawPropertyRowLabel`), 칸은 남은 폭 — ImGui 기본(오른쪽 이름표)은 칸이 창 폭을 따라 늘고 이름표가 줄 끝으로 밀렸다.
        ImGui::PushID( pLabel );
        EditorWidgets::drawPropertyRowLabel( pLabel );
        EditorWidgets::drawTextField( "##value", inoutText, -1.0f );
        ImGui::PopID();
        if ( pTooltip != nullptr )
            EditorWidgets::drawTooltip( pTooltip );
        if ( ImGui::IsItemDeactivatedAfterEdit() == false )
            return;
        notifyDocumentEdited( "Edit Sequence Track", "sequence-track" );
        if ( bPreviewOnEdit )
            previewCurrentFrame();
    }

    void SequencerPanel::drawSelectedTrack( SequenceKeyTrack& track )
    {
        ImGui::Separator();
        drawTrackTextField( SW_LOCKEY( "EditorSequencer", "TrackNameLabel" ), track._name, nullptr, false );
        drawTrackTextField( SW_LOCKEY( "EditorSequencer", "TrackTargetLabel" ), track._targetObject, nullptr, true );
        if ( track._kind == SequenceTrackKind::Property )
            drawTrackTextField( SW_LOCKEY( "EditorSequencer", "PropertyLabel" ), track._propertyPath, SW_LOCKEY( "EditorSequencer", "PropertyPathTip" ), true );
        else if ( track._kind == SequenceTrackKind::RigSlot )
            drawTrackTextField( SW_LOCKEY( "EditorSequencer", "RigSlotLabel" ), track._propertyPath, SW_LOCKEY( "EditorSequencer", "RigSlotTip" ), true );

        GameObjectManager* pManager = editor::getActiveObjectManager();
        const float32      frame    = static_cast<float32>( _currentFrame );
        float32            arrCurrent[kSequenceTransformChannelCount]{};
        const bool         bTargetFound = SequenceDocument::readTrackValues( pManager, track, arrCurrent, kSequenceTransformChannelCount );

        drawKeyButtons( track, frame, arrCurrent, bTargetFound );
        drawChannelRows( track, frame, arrCurrent );
    }

    void SequencerPanel::drawKeyButtons( SequenceKeyTrack& track, float32 frame, const float32* pArrCurrent, bool bTargetFound )
    {
        ImGui::BeginDisabled( bTargetFound == false );
        if ( ImGui::Button( SW_LOCKEY( "EditorSequencer", "KeyButton" ) ) )
        {
            track.setKey( frame, pArrCurrent, kSequenceTransformChannelCount );
            notifyDocumentEdited( "Add Sequence Key" );
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::note( "sequencer.addKey" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "AddKeyTip" ) );
        const utf8* const pDeleteKey = SW_LOCKEY( "EditorSequencer", "DeleteKeyButton" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pDeleteKey ) );
        ImGui::BeginDisabled( track.hasKeyAt( frame ) == false );
        if ( ImGui::Button( pDeleteKey ) && track.removeKeysAt( frame ) )
        {
            notifyDocumentEdited( "Delete Sequence Key" );
            previewCurrentFrame();
        }
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "DeleteKeyTip" ) );
        track.collectKeyFrames( _listKeyFrame );
        EditorChrome::sameLineIfFits( EditorChrome::measureArrowButton() );
        if ( ImGui::ArrowButton( "##previousKey", ImGuiDir_Left ) )
        {
            const std::optional<int32> previousFrame = _document.findPreviousKeyFrame( _listKeyFrame, _currentFrame );
            if ( previousFrame.has_value() )
            {
                _currentFrame = *previousFrame;
                previewCurrentFrame();
            }
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "PreviousKeyTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureArrowButton() );
        if ( ImGui::ArrowButton( "##nextKey", ImGuiDir_Right ) )
        {
            const std::optional<int32> nextFrame = _document.findNextKeyFrame( _listKeyFrame, _currentFrame );
            if ( nextFrame.has_value() )
            {
                _currentFrame = *nextFrame;
                previewCurrentFrame();
            }
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "NextKeyTip" ) );
        if ( bTargetFound == false )
        {
            ImGui::SameLine();
            const utf8* pMissing = SW_LOCKEY( "EditorSequencer", "TargetNotFound" );
            if ( track._kind == SequenceTrackKind::Property )
                pMissing = SW_LOCKEY( "EditorSequencer", "PropertyTargetNotFound" );
            else if ( track._kind == SequenceTrackKind::RigSlot )
                pMissing = SW_LOCKEY( "EditorSequencer", "RigSlotTargetNotFound" );
            ImGui::TextDisabled( "%s", pMissing );
        }
    }

    void SequencerPanel::drawChannelRows( SequenceKeyTrack& track, float32 frame, const float32* pArrCurrent )
    {
        const uint32 channelCount = static_cast<uint32>( track._listChannel.size() );
        for ( uint32 channelIndex = 0; channelIndex < channelCount; ++channelIndex )
        {
            ImGui::PushID( static_cast<int32>( channelIndex ) );
            const bool bKeyed    = track.findChannelKey( channelIndex, frame ) != constant::invalid_index::kUint32;
            const bool bRotation = SequenceDocument::isRotationChannel( track, channelIndex );
            // 키가 없는 채널은 대상의 지금 값을 보인다(트랜스폼 채널만 지금 값이 있다).
            const float32 currentValue = channelIndex < kSequenceTransformChannelCount ? pArrCurrent[channelIndex] : 0.0f;
            float32       value        = SequenceDocument::computeChannelDisplayValue( track, channelIndex, frame, currentValue );
            // 키가 있는 프레임이면 표식을 강조색으로 — 언리얼 Sequencer 채널 줄의 키 마름모에 해당한다.
            ImGui::TextColored( bKeyed ? ImGui::GetStyleColorVec4( ImGuiCol_CheckMark ) : ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ), bKeyed ? "<>" : "--" );
            ImGui::SameLine();
            // 이름표 열 · 값 · 커브 단추 순서(인스펙터 줄과 같다) — 값 칸은 단추 몫을 뺀 남은 폭이다.
            const utf8* const pCurve = SW_LOCKEY( "EditorSequencer", "CurveButton" );
            EditorWidgets::drawPropertyRowLabel( track.getChannelName( channelIndex ) );
            ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - EditorChrome::measureSmallButton( pCurve ) - ImGui::GetStyle().ItemSpacing.x );
            const float32 speed = bRotation ? InspectorPropertyLayout::kAngleDragSpeed : 0.05f;
            if ( ImGui::DragFloat( "##value", &value, speed, 0.0f, 0.0f, bRotation ? "%.2f deg" : "%.3f" ) )
            {
                (void)SequenceDocument::setChannelDisplayValue( track, channelIndex, frame, value );
                previewCurrentFrame();
            }
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sequence Key", "sequence-key" );
            fixed_string<constant::buffer::kMax64> mark;
            formatstring( mark.data(), mark.capacity(), "sequencer.channel.%#", channelIndex );
            EditorSelfTestMarks::note( mark.c_str() );
            ImGui::SameLine();
            if ( ImGui::SmallButton( pCurve ) )
            {
                _document._curveTrack    = _document._selectedTrack;
                _document._curveChannel  = static_cast<int32>( channelIndex );
                _curveState              = EditorCurveEditorState{};
                _curveState._working     = track._listChannel[channelIndex];
                _curveState._bFitPending = true;
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "OpenCurveTip" ) );
            formatstring( mark.data(), mark.capacity(), "sequencer.curve.%#", channelIndex );
            EditorSelfTestMarks::note( mark.c_str() );
            ImGui::PopID();
        }
    }

    void SequencerPanel::drawChannelCurve()
    {
        FloatCurve* pChannel = _document.findCurveChannel();
        if ( pChannel == nullptr )
            return;
        const SequenceKeyTrack& track   = *_document.findCurveTrack();
        FloatCurve&             channel = *pChannel;

        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Curve" ) );
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "Frames", TextArgumentList().addText( "name", track._name.c_str() ).addText( "curveChannel", track.getChannelName( static_cast<uint32>( _document._curveChannel ) ) ) ).c_str() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Close" ) ) );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "Close" ) ) )
        {
            _document._curveTrack = -1;
            return;
        }
        // 끄는 중 · 필드 입력 중이 아니면 채널에서 다시 뜬다(되돌리기 · 값 필드의 편집이 편집기에 보인다).
        if ( _curveState._drag == EditorCurveDrag::None && ImGui::IsAnyItemActive() == false )
            _curveState._working = channel;
        if ( ImGui::BeginChild( "##sequencerCurve", EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, SequencerPanelInternal::kCurveAreaLines ) ), ImGuiChildFlags_Borders ) )
        {
            if ( EditorCurveEditor::drawEditor( _curveState, "sequencer.curve" ) )
            {
                channel = _curveState._working;
                notifyDocumentEdited( "Edit Sequence Curve" );
                previewCurrentFrame();
            }
        }
        ImGui::EndChild();
    }

    void SequencerPanel::addKeyTrack( SequenceTrackKind kind )
    {
        _document.addTrack( kind, SequencerPanelInternal::findSelectedObjectName() );
        notifyDocumentEdited( "Add Sequence Track" );
    }

    bool SequencerPanel::findSelectedTrackKeyFrameCount( uint32& outCount ) const
    {
        const SequenceKeyTrack* pTrack = _document.findSelectedTrack();
        if ( pTrack == nullptr )
            return false;
        vector<float32> listFrame;
        pTrack->collectKeyFrames( listFrame );
        outCount = static_cast<uint32>( listFrame.size() );
        return true;
    }

    void SequencerPanel::previewCurrentFrame()
    {
        EditorViewportPreview::applySequenceFrame( captureAsset(), _currentFrame );
    }

    ToolAssetLoadResult SequencerPanel::loadDocument()
    {
        const string path = acceptDocumentToLoad();
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;

        // 읽지 못하면 그대로 둔다 — 결과를 돌려주므로 기반이 표시하고 저장을 막는다(표시 없이 돌아가면 프레임마다 다시 읽는다).
        SequenceAsset             asset;
        const ToolAssetLoadResult result = EditorToolAssetCommands::loadSequence( asset, path );
        if ( getLoadedAssetPath().empty() )
            acceptFocusedDocument();
        if ( result == ToolAssetLoadResult::Loaded )
        {
            applyAsset( asset );
            _document.resetSelection();
            _currentFrame = asset._frameMin;
        }
        return result;
    }

    void SequencerPanel::saveToLoadedPath()
    {
        if ( getLoadedAssetPath().empty() )
            return;
        // 다시 읽지 못할 내용으로 멀쩡한 파일을 덮지 않는다 — 문서는 고친 상태(dirty)로 남고, 이유는 읽기 검사의 로그에 있다.
        if ( _document.canReadBack() == false )
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
        SequenceAsset asset = _document._asset;
        asset._note         = _cinematicNote.c_str();
        return asset;
    }

    void SequencerPanel::applyAsset( const SequenceAsset& asset )
    {
        _document.restoreAsset( asset );
        _cinematicNote = asset._note.c_str();
    }

    string SequencerPanel::captureDocumentText() const
    {
        return captureAsset().toXML();
    }

    void SequencerPanel::applyDocumentText( string_view text )
    {
        SequenceAsset restored;
        // 읽지 못한 스냅숏으로 문서를 비우면 다음 저장이 내용을 잃는다 — 지금 문서를 그대로 둔다.
        if ( text.empty() == false && restored.parseXML( text ) == false )
        {
            SW_LOG_WARNING_ID( "SequencerPanel.SequenceUndoSnapshotCould", "Sequence undo snapshot could not be read - keeping the current sequence" );
            return;
        }
        applyAsset( restored );
    }

    void SequencerPanel::syncPreviewPlayer()
    {
        if ( _previewPlayer == nullptr )
            return;
        _previewPlayer->setAsset( captureAsset() );
        _previewPlayer->setLoop( true );
    }

    void SequencerPanel::updatePreview( float32 deltaSeconds )
    {
        if ( _previewPlayer == nullptr || _previewPlayer->isPlaying() == false )
            return;
        _previewPlayer->update( deltaSeconds );
        _currentFrame = _previewPlayer->getCurrentFrame();
        EditorViewportPreview::applySequenceFrame( _previewPlayer->getAsset(), _currentFrame );
    }
} // namespace sw::editor
