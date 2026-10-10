#include "pch.h"

#include "Editor/Panels/SequencerPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Commands/EditorViewportPreview.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyLayout.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayer.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

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

    namespace
    {
        struct SequencerPanelInternal
        {
            /** @brief 트랙 이름 칸의 폭(픽셀, DPI 전)입니다. 나머지 폭이 키 띠다. */
            static constexpr float32 kTrackLabelWidth = 180.0f;
            /** @brief 커브 편집기 구역의 높이(픽셀, DPI 전)입니다. */
            static constexpr float32 kCurveAreaHeight = 220.0f;
            /** @brief 키 마름모의 반지름(픽셀, DPI 전)입니다. */
            static constexpr float32 kKeyMarkerRadius = 4.0f;

            /** @brief 트랜스폼 트랙의 회전 채널이면 true 입니다 — 값 칸은 도로 보이고 고친다(저장은 라디안, 인스펙터와 같은 규칙). */
            static bool isRotationChannel( const SequenceKeyTrack& track, uint32 channelIndex )
            {
                return track._kind == SequenceTrackKind::Transform && channelIndex >= 3 && channelIndex < 6;
            }

            /** @brief 에디터에서 고른 오브젝트의 이름입니다. 고른 것이 없으면 빈 문자열입니다. */
            static string findSelectedObjectName()
            {
                EditorContext* pContext = EditorContext::get();
                GameObject*    pObject  = pContext != nullptr ? pContext->getEditorSelection().getPrimaryObject() : nullptr;
                return pObject != nullptr ? string{ pObject->getName().c_str() } : string{};
            }
        };
    } // namespace

    SequencerPanel::SequencerPanel()
        : EditorDocumentPanel{ EditorAssetType::Sequence, false }
        , _cinematicNote{ "Cinematic notes (not a clip track)." }
        , _sequence{ make_unique<ClipSequence>() }
        , _previewPlayer{ make_unique<sw::SequencePlayer>() }
        , _listTimingBefore{}
        , _listTrack{}
        , _listKeyFrame{}
        , _curveState{}
        , _currentFrame{ 0 }
        , _selected{ -1 }
        , _firstFrame{ 0 }
        , _selectedTrack{ -1 }
        , _curveTrack{ -1 }
        , _curveChannel{ 0 }
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
        drawDocumentOpenBar( "sequencer" );

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
                previewCurrentFrame();
                SequenceTimelineUtil::releaseCameraCut( editor::getActiveObjectManager(), _previewPlayer->getAsset() );
            }
            ImGui::SameLine();
            if ( getLoadedAssetPath().empty() )
                ImGui::TextDisabled( "No .seq file focused" );
            else
                ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        }
        EditorChrome::endToolbar();

        tickPreview( ImGui::GetIO().DeltaTime );

        // 끄는 동안에도 적용한다 — 시간 막대를 옮기면 뷰포트가 따라온다(언리얼 Sequencer · 유니티 Timeline 의 스크럽).
        ImGui::SetNextItemWidth( -1.0f );
        if ( ImGui::SliderInt( "##ScrubFrame", &_currentFrame, _sequence->_frameMin, _sequence->_frameMax, "Frame %d" ) )
        {
            if ( _previewPlayer->isPlaying() )
                _previewPlayer->seekToFrame( _currentFrame );
            previewCurrentFrame();
        }
        EditorSelfTestMarks::note( "sequencer.scrub" );

        // 키 트랙이 위, 구간 항목이 아래다 — 자주 고치는 쪽(키 · 커브)이 패널이 낮아도 보인다.
        drawKeyTracks();

        ImGui::SeparatorText( "Clips, Events and Camera Cuts" );
        drawItemFields();

        // ImSequencer 는 여러 항목을 그리는 위젯이라 IsItemEdited("마지막 항목")가 클립 끌기 · 더하기 · 지우기를 뜻하지 않는다 — 부르기 전후의 배치를 비교한다.
        SequenceTimingUtil::captureTiming( _sequence->_listItem, _listTimingBefore );
        const int32 frameBefore = _currentFrame;
        ImSequencer::Sequencer( _sequence.get(), &_currentFrame, &_bExpanded, &_selected, &_firstFrame,
                                ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_ADD | ImSequencer::SEQUENCER_DEL | ImSequencer::SEQUENCER_CHANGE_FRAME );
        if ( SequenceTimingUtil::hasTimingChanged( _listTimingBefore, _sequence->_listItem ) )
            notifyDocumentEdited( "Edit Sequence Timeline", "sequence-timeline" );
        if ( _currentFrame != frameBefore )
            previewCurrentFrame();

        ImGui::InputTextMultiline( "Cinematic Note", _cinematicNote.data(), _cinematicNote.capacity(), ImVec2( -1.0f, 40.0f * EditorThemeUtil::getDpiScale() ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Sequence Note", "sequence-note" );
    }

    void SequencerPanel::drawItemFields()
    {
        constexpr float32 kClipFieldRowCount = 2.0f; // 이름 · 대상
        // 고른 항목의 편집 칸은 늘 같은 높이의 구역에 그린다 — 고를 때만 칸이 생기면 그만큼 타임라인이 밀려 내려간다.
        EditorSectionDesc clipDesc{};
        clipDesc._pID       = "##sequence_clip";
        clipDesc._kind      = EditorSectionKind::Child;
        clipDesc._childSize = float2{ 0.0f, ImGui::GetFrameHeightWithSpacing() * kClipFieldRowCount };
        EditorChrome::beginSection( clipDesc );
        if ( 0 <= _selected && _selected < static_cast<int32>( _sequence->_listItem.size() ) )
        {
            SequenceTrackItem& item = _sequence->_listItem[static_cast<size_t>( _selected )];
            EditorWidgets::drawTextField( "Item Name", item._name );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sequence Item", "sequence-item" );
            EditorWidgets::drawTextField( "Target Object", item._targetObject );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sequence Item", "sequence-item" );
        }
        else
        {
            EditorWidgets::drawEmptyHint( "Select a clip, event or camera cut on the timeline to edit it." );
        }
        EditorChrome::endSection();
    }

    void SequencerPanel::drawKeyTracks()
    {
        ImGui::SeparatorText( "Key Tracks" );
        if ( ImGui::Button( "+ Transform Track" ) )
            addKeyTrack( SequenceTrackKind::Transform );
        EditorSelfTestMarks::note( "sequencer.addTransformTrack" );
        EditorWidgets::drawTooltip( "고른 오브젝트의 이동 · 회전 · 크기를 키로 모는 트랙을 더합니다" );
        ImGui::SameLine();
        if ( ImGui::Button( "+ Property Track" ) )
            addKeyTrack( SequenceTrackKind::Property );
        EditorSelfTestMarks::note( "sequencer.addPropertyTrack" );
        EditorWidgets::drawTooltip( "고른 오브젝트 컴포넌트의 숫자 프로퍼티 하나를 키로 모는 트랙을 더합니다" );
        ImGui::SameLine();
        ImGui::BeginDisabled( _selectedTrack < 0 );
        if ( ImGui::Button( "Delete Track" ) && 0 <= _selectedTrack && _selectedTrack < static_cast<int32>( _listTrack.size() ) )
        {
            _listTrack.erase( _listTrack.begin() + _selectedTrack );
            _selectedTrack = -1;
            _curveTrack    = -1;
            notifyDocumentEdited( "Delete Sequence Track" );
        }
        ImGui::EndDisabled();

        if ( _listTrack.empty() )
        {
            EditorWidgets::drawEmptyHint( "No key tracks. Select an object and add a transform or property track." );
            return;
        }

        const float32 labelWidth = SequencerPanelInternal::kTrackLabelWidth * EditorThemeUtil::getDpiScale();
        for ( int32 trackIndex = 0; trackIndex < static_cast<int32>( _listTrack.size() ); ++trackIndex )
        {
            const SequenceKeyTrack& track = _listTrack[static_cast<size_t>( trackIndex )];
            ImGui::PushID( trackIndex );
            fixed_string<constant::kMaxBuffer256> label;
            formatstring( label.data(), label.capacity(), "%# (%#)", track._name.empty() ? "Track" : track._name.c_str(),
                          track._targetObject.empty() ? "no target" : track._targetObject.c_str() );
            if ( ImGui::Selectable( label.c_str(), trackIndex == _selectedTrack, ImGuiSelectableFlags_None, ImVec2( labelWidth, 0.0f ) ) )
                _selectedTrack = trackIndex;
            fixed_string<constant::kMaxBuffer64> mark;
            formatstring( mark.data(), mark.capacity(), "sequencer.track.%#", trackIndex );
            EditorSelfTestMarks::note( mark.c_str() );
            ImGui::SameLine();
            drawKeyStrip( track, trackIndex );
            ImGui::PopID();
        }

        if ( 0 <= _selectedTrack && _selectedTrack < static_cast<int32>( _listTrack.size() ) )
            drawSelectedTrack( _listTrack[static_cast<size_t>( _selectedTrack )] );
        drawChannelCurve();
    }

    void SequencerPanel::drawKeyStrip( const SequenceKeyTrack& track, int32 trackIndex )
    {
        const float32 height = ImGui::GetTextLineHeight();
        const float32 width  = MathUtil::max( ImGui::GetContentRegionAvail().x, 1.0f );
        const ImVec2  origin = ImGui::GetCursorScreenPos();
        const int32   span   = MathUtil::max( _sequence->_frameMax - _sequence->_frameMin, 1 );
        if ( ImGui::InvisibleButton( "##keyStrip", ImVec2( width, height ) ) )
        {
            const float32 ratio = MathUtil::clamp( ( ImGui::GetIO().MousePos.x - origin.x ) / width, 0.0f, 1.0f );
            _selectedTrack      = trackIndex;
            _currentFrame       = _sequence->_frameMin + static_cast<int32>( MathUtil::round( ratio * static_cast<float32>( span ) ) );
            previewCurrentFrame();
        }

        ImDrawList*   pDrawList = ImGui::GetWindowDrawList();
        const float32 radius    = SequencerPanelInternal::kKeyMarkerRadius * EditorThemeUtil::getDpiScale();
        const float32 centerY   = origin.y + height * 0.5f;
        pDrawList->AddRectFilled( origin, ImVec2( origin.x + width, origin.y + height ), ImGui::GetColorU32( ImGuiCol_FrameBg ) );
        track.collectKeyFrames( _listKeyFrame );
        for ( const float32 keyFrame : _listKeyFrame )
        {
            const float32 x = origin.x + ( keyFrame - static_cast<float32>( _sequence->_frameMin ) ) / static_cast<float32>( span ) * width;
            pDrawList->AddQuadFilled( ImVec2( x, centerY - radius ), ImVec2( x + radius, centerY ), ImVec2( x, centerY + radius ), ImVec2( x - radius, centerY ),
                                      ImGui::GetColorU32( ImGuiCol_PlotHistogram ) );
        }
        const float32 cursorX = origin.x + static_cast<float32>( _currentFrame - _sequence->_frameMin ) / static_cast<float32>( span ) * width;
        pDrawList->AddLine( ImVec2( cursorX, origin.y ), ImVec2( cursorX, origin.y + height ), ImGui::GetColorU32( ImGuiCol_CheckMark ) );
    }

    void SequencerPanel::drawSelectedTrack( SequenceKeyTrack& track )
    {
        ImGui::Separator();
        EditorWidgets::drawTextField( "Track Name", track._name );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Sequence Track", "sequence-track" );
        EditorWidgets::drawTextField( "Track Target", track._targetObject );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
        {
            notifyDocumentEdited( "Edit Sequence Track", "sequence-track" );
            previewCurrentFrame();
        }
        if ( track._kind == SequenceTrackKind::Property )
        {
            EditorWidgets::drawTextField( "Property", track._propertyPath );
            EditorWidgets::drawTooltip( "<컴포넌트 타입>.<프로퍼티> — float32 · int32 프로퍼티(예: CameraComponent._fov)" );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
            {
                notifyDocumentEdited( "Edit Sequence Track", "sequence-track" );
                previewCurrentFrame();
            }
        }

        GameObjectManager* pManager = editor::getActiveObjectManager();
        const float32      frame    = static_cast<float32>( _currentFrame );
        float32            arrCurrent[kSequenceTransformChannelCount]{};
        const bool         bTargetFound = SequenceTimelineUtil::readTrackValues( pManager, track, arrCurrent, kSequenceTransformChannelCount );

        ImGui::BeginDisabled( bTargetFound == false );
        if ( ImGui::Button( "Key" ) )
        {
            track.setKey( frame, arrCurrent, kSequenceTransformChannelCount );
            notifyDocumentEdited( "Add Sequence Key" );
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::note( "sequencer.addKey" );
        EditorWidgets::drawTooltip( "대상의 지금 값을 이 프레임의 키로 모든 채널에 찍습니다" );
        ImGui::SameLine();
        ImGui::BeginDisabled( track.hasKeyAt( frame ) == false );
        if ( ImGui::Button( "Delete Key" ) && track.removeKeysAt( frame ) )
        {
            notifyDocumentEdited( "Delete Sequence Key" );
            previewCurrentFrame();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        track.collectKeyFrames( _listKeyFrame );
        if ( ImGui::ArrowButton( "##previousKey", ImGuiDir_Left ) )
        {
            for ( auto it = _listKeyFrame.rbegin(); it != _listKeyFrame.rend(); ++it )
            {
                if ( *it < frame - kSequenceKeyFrameTolerance )
                {
                    _currentFrame = static_cast<int32>( MathUtil::round( *it ) );
                    previewCurrentFrame();
                    break;
                }
            }
        }
        EditorWidgets::drawTooltip( "앞 키로" );
        ImGui::SameLine();
        if ( ImGui::ArrowButton( "##nextKey", ImGuiDir_Right ) )
        {
            for ( const float32 keyFrame : _listKeyFrame )
            {
                if ( keyFrame > frame + kSequenceKeyFrameTolerance )
                {
                    _currentFrame = static_cast<int32>( MathUtil::round( keyFrame ) );
                    previewCurrentFrame();
                    break;
                }
            }
        }
        EditorWidgets::drawTooltip( "다음 키로" );
        if ( bTargetFound == false )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( track._kind == SequenceTrackKind::Property ? "Target or numeric property not found" : "Target object not found" );
        }

        const uint32 channelCount = static_cast<uint32>( track._listChannel.size() );
        for ( uint32 channelIndex = 0; channelIndex < channelCount; ++channelIndex )
        {
            ImGui::PushID( static_cast<int32>( channelIndex ) );
            const bool    bKeyed     = track.findChannelKey( channelIndex, frame ) != invalid_index::kUint32;
            const bool    bRotation  = SequencerPanelInternal::isRotationChannel( track, channelIndex );
            const float32 fallback   = channelIndex < kSequenceTransformChannelCount ? arrCurrent[channelIndex] : 0.0f;
            const float32 displayMul = bRotation ? MathUtil::kRadianToDegree : 1.0f;
            float32       value      = track.evaluateChannel( channelIndex, frame, fallback ) * displayMul;
            // 키가 있는 프레임이면 표식을 강조색으로 — 언리얼 Sequencer 채널 줄의 키 마름모 자리.
            ImGui::TextColored( bKeyed ? ImGui::GetStyleColorVec4( ImGuiCol_CheckMark ) : ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ), bKeyed ? "<>" : "--" );
            ImGui::SameLine();
            ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x * 0.6f );
            const float32 speed = bRotation ? InspectorPropertyLayout::kAngleDragSpeed : 0.05f;
            if ( ImGui::DragFloat( track.getChannelName( channelIndex ), &value, speed, 0.0f, 0.0f, bRotation ? "%.2f deg" : "%.3f" ) )
            {
                (void)track.setChannelKey( channelIndex, frame, value / displayMul );
                previewCurrentFrame();
            }
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sequence Key", "sequence-key" );
            fixed_string<constant::kMaxBuffer64> mark;
            formatstring( mark.data(), mark.capacity(), "sequencer.channel.%#", channelIndex );
            EditorSelfTestMarks::note( mark.c_str() );
            ImGui::SameLine();
            if ( ImGui::SmallButton( "Curve" ) )
            {
                _curveTrack              = _selectedTrack;
                _curveChannel            = static_cast<int32>( channelIndex );
                _curveState              = EditorCurveEditorState{};
                _curveState._working     = track._listChannel[channelIndex];
                _curveState._bFitPending = true;
            }
            formatstring( mark.data(), mark.capacity(), "sequencer.curve.%#", channelIndex );
            EditorSelfTestMarks::note( mark.c_str() );
            ImGui::PopID();
        }
    }

    void SequencerPanel::drawChannelCurve()
    {
        if ( _curveTrack < 0 || _curveTrack >= static_cast<int32>( _listTrack.size() ) )
            return;
        SequenceKeyTrack& track = _listTrack[static_cast<size_t>( _curveTrack )];
        if ( _curveChannel < 0 || _curveChannel >= static_cast<int32>( track._listChannel.size() ) )
            return;
        FloatCurve& channel = track._listChannel[static_cast<size_t>( _curveChannel )];

        ImGui::SeparatorText( "Curve" );
        ImGui::Text( "%s / %s (frames)", track._name.c_str(), track.getChannelName( static_cast<uint32>( _curveChannel ) ) );
        ImGui::SameLine();
        if ( ImGui::SmallButton( "Close" ) )
        {
            _curveTrack = -1;
            return;
        }
        // 끄는 중 · 칸 입력 중이 아니면 채널에서 다시 뜬다(되돌리기 · 값 칸의 편집이 편집기에 보인다).
        if ( _curveState._drag == EditorCurveDrag::None && ImGui::IsAnyItemActive() == false )
            _curveState._working = channel;
        if ( ImGui::BeginChild( "##sequencerCurve", ImVec2( 0.0f, SequencerPanelInternal::kCurveAreaHeight * EditorThemeUtil::getDpiScale() ), ImGuiChildFlags_Borders ) )
        {
            if ( EditorCurveEditor::drawEditor( _curveState ) )
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
        SequenceKeyTrack track{};
        track._kind         = kind;
        track._targetObject = SequencerPanelInternal::findSelectedObjectName();
        track._name         = string{ kind == SequenceTrackKind::Transform ? "Transform " : "Property " } + to_string( _listTrack.size() );
        track.fitChannelsToKind();
        _listTrack.push_back( std::move( track ) );
        _selectedTrack = static_cast<int32>( _listTrack.size() ) - 1;
        notifyDocumentEdited( "Add Sequence Track" );
    }

    bool SequencerPanel::findSelectedTrackKeyFrameCount( uint32& outCount ) const
    {
        if ( _selectedTrack < 0 || _selectedTrack >= static_cast<int32>( _listTrack.size() ) )
            return false;
        vector<float32> listFrame;
        _listTrack[static_cast<size_t>( _selectedTrack )].collectKeyFrames( listFrame );
        outCount = static_cast<uint32>( listFrame.size() );
        return true;
    }

    void SequencerPanel::previewCurrentFrame()
    {
        EditorViewportPreview::applySequenceFrame( captureAsset(), _currentFrame );
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
        {
            applyAsset( asset );
            _selected      = -1;
            _selectedTrack = _listTrack.empty() ? -1 : 0;
            _curveTrack    = -1;
            _currentFrame  = asset._frameMin;
        }
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
        asset._frameMin  = _sequence->_frameMin;
        asset._frameMax  = _sequence->_frameMax;
        asset._note      = _cinematicNote.c_str();
        asset._listItem  = _sequence->_listItem;
        asset._listTrack = _listTrack;
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
        _listTrack           = asset._listTrack;
        // 되돌리기가 트랙을 줄였으면 고른 자리를 맞춘다(지운 트랙을 가리키지 않게).
        if ( _selectedTrack >= static_cast<int32>( _listTrack.size() ) )
            _selectedTrack = static_cast<int32>( _listTrack.size() ) - 1;
        if ( _curveTrack >= static_cast<int32>( _listTrack.size() ) )
            _curveTrack = -1;
    }

    string SequencerPanel::captureDocumentText() const
    {
        return captureAsset().toJSON();
    }

    void SequencerPanel::applyDocumentText( string_view text )
    {
        SequenceAsset restored;
        if ( text.empty() == false && restored.parseJSON( text ) == false )
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
    }
} // namespace sw::editor
