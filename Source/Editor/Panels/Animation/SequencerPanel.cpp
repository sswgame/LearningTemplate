#include "pch.h"

#include "Editor/Panels/Animation/SequencerPanel.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/Commands/Scene/EditorViewportPreview.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Panels/Inspector/Model/InspectorPropertyLayout.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayer.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <ImSequencer.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( SequencerPanel, SequencerPanel::kPanelID, EditorPanelCategory::Tool, 1000 );

    /**
     * @brief ImSequencer 가 보는 시퀀스입니다. 항목은 문서의 `SequenceTrackItem` 그대로이고, 더하기 · 지우기 · 사본은 문서의 명령입니다.
     * @details 위젯의 종류 번호는 `SequenceItemKind` 값입니다.
     */
    struct ClipSequence : ImSequencer::SequenceInterface
    {
        SequenceModel&         _document;
        mutable string         _labelScratch;
        uint8                  _bDraftOpened : 1; ///< 이번 `Add` 가 항목 대신 초안을 열었다 — 패널이 위젯의 "끝 항목 고르기" 를 버릴지 본다
        [[maybe_unused]] uint8 _reserved     : 7;

        explicit ClipSequence( SequenceModel& document )
            : _document{ document }
            , _labelScratch{}
            , _bDraftOpened{ SW_FALSE }
            , _reserved{ 0 }
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
            return static_cast<int32>( SequenceItemKind::Count );
        }
        const utf8* GetItemTypeName( int32 typeIndex ) const override
        {
            const utf8* pName = editor::getSubsystem<TypeRegistry>().enumToString( static_cast<SequenceItemKind>( typeIndex ) );
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
            (void)_document.addItem( static_cast<SequenceItemKind>( type ), GetItemTypeName( type ) );
            _bDraftOpened = _document.isDraftingItem() ? SW_TRUE : SW_FALSE;
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
        /** @brief 항목의 글 칸 하나를 그리는 법입니다(종류 표가 허용한 칸만 그린다 — 칸을 더하면 이 표에 한 줄). 글은 `SequencerPanelInternal::findFieldLabel` · `findFieldTip`. */
        struct SequenceItemFieldView
        {
            SequenceItemField _field;
            const utf8*       _pStableID;
            const utf8*       _pDropExtension; ///< 끌어 놓기로 받을 확장자(없으면 nullptr)
            const utf8*       _pMark;
        };

        constexpr SequenceItemFieldView kArrItemFieldView[] = {
            {  SequenceItemField::Prefab,   "Prefab",   ".prefab.xml",   "sequencer.item.prefab"},
            {  SequenceItemField::Preset,   "Preset",         nullptr,   "sequencer.item.preset"},
            {SequenceItemField::Scenario, "Scenario", ".scenario.xml", "sequencer.item.scenario"},
            {    SequenceItemField::Clip,     "Clip",     ".animclip",     "sequencer.item.clip"},
            {   SequenceItemField::Sound,    "Sound",         nullptr,    "sequencer.item.sound"},
            {    SequenceItemField::Text,     "Text",         nullptr,     "sequencer.item.text"},
        };

        struct SequencerPanelInternal
        {
            /** @brief 트랙 이름 필드의 폭(픽셀, DPI 전)입니다. 나머지 폭이 키 띠다. */
            static constexpr float32 kTrackLabelWidth = 180.0f;
            /** @brief 커브 편집기 구역의 높이(픽셀, DPI 전)입니다. */
            static constexpr float32 kCurveAreaLines = 12.0f; ///< 커브 영역 높이(글자 줄 수)
            /** @brief 키 마름모의 반지름(픽셀, DPI 전)입니다. 누름 판정도 이 반지름 안이다. */
            static constexpr float32 kKeyMarkerRadius = 4.0f;
            /** @brief 컷 차선의 높이(글자 줄 수)입니다. */
            static constexpr float32 kCutLaneLines = 1.5f;

            /** @brief 에디터에서 고른 오브젝트입니다. 고른 것이 없으면 nullptr 입니다(메인 씬의 선택 — 언리얼 시퀀서도 레벨 선택의 액터를 바인딩에 더한다). */
            static GameObject* findSelectedObject()
            {
                return editor::getMainSceneSelection().getPrimaryObject();
            }

            /**
             * @brief 항목의 글 칸 하나입니다. 칸을 떠나며 고쳤거나 애셋을 끌어 놓았으면 true 입니다.
             * @param pExpectedExt 끌어 놓기로 받을 애셋의 확장자입니다. nullptr 이면 끌어 놓기를 받지 않습니다(이름 · 프리셋 id).
             * @param pMark 자체 시험 표시입니다.
             */
            [[nodiscard]] static bool drawItemField( const utf8* pLabel, string& text, const utf8* pTooltip, const utf8* pExpectedExt, const utf8* pMark )
            {
                (void)EditorWidgets::drawTextField( pLabel, text ); // 적용은 칸을 떠날 때다 — 치는 동안 비는 값은 사본에만 있다
                bool   bEdited = ImGui::IsItemDeactivatedAfterEdit();
                string droppedPath;
                if ( pExpectedExt != nullptr && EditorWidgets::acceptAssetDrop( droppedPath, pExpectedExt ) )
                {
                    text    = droppedPath;
                    bEdited = true;
                }
                EditorSelfTestMarks::record( pMark );
                if ( pTooltip != nullptr )
                    EditorWidgets::drawTooltip( pTooltip );
                return bEdited;
            }

            /** @brief 칸의 이름표입니다. */
            static const utf8* findFieldLabel( SequenceItemField field )
            {
                switch ( field )
                {
                    case SequenceItemField::Prefab:
                        return SW_LOCKEY( "EdWidget", "Prefab" );
                    case SequenceItemField::Preset:
                        return SW_LOCKEY( "EdWidget", "Preset" );
                    case SequenceItemField::Scenario:
                        return SW_LOCKEY( "EditorSequencer", "ScenarioLabel" );
                    case SequenceItemField::Clip:
                        return SW_LOCKEY( "EditorSequencer", "ClipLabel" );
                    case SequenceItemField::Sound:
                        return SW_LOCKEY( "EditorSequencer", "SoundLabel" );
                    case SequenceItemField::Text:
                        return SW_LOCKEY( "EditorSequencer", "TextLabel" );
                    case SequenceItemField::Binding:
                    case SequenceItemField::Count:
                        return "?";
                }
                return "?";
            }

            /** @brief 칸의 풀이입니다. */
            static const utf8* findFieldTip( SequenceItemField field )
            {
                switch ( field )
                {
                    case SequenceItemField::Prefab:
                        return SW_LOCKEY( "EditorSequencer", "PrefabTip" );
                    case SequenceItemField::Preset:
                        return SW_LOCKEY( "EditorSequencer", "PresetTip" );
                    case SequenceItemField::Scenario:
                        return SW_LOCKEY( "EditorSequencer", "ScenarioTip" );
                    case SequenceItemField::Clip:
                        return SW_LOCKEY( "EditorSequencer", "ClipTip" );
                    case SequenceItemField::Sound:
                        return SW_LOCKEY( "EditorSequencer", "SoundTip" );
                    case SequenceItemField::Text:
                        return SW_LOCKEY( "EditorSequencer", "TextTip" );
                    case SequenceItemField::Binding:
                    case SequenceItemField::Count:
                        return nullptr;
                }
                return nullptr;
            }

            /** @brief 키 띠에서 포인터 x 에 가장 가까운 키 프레임입니다(마름모 반지름 안). 없으면 비어 있습니다. */
            static optional<float32> findKeyUnderPointer( const SequenceModel& document, const vector<float32>& listKeyFrame, float32 originX, float32 width, float32 radius, float32 pointerX )
            {
                optional<float32> hit;
                float32           bestDistance = radius;
                for ( const float32 keyFrame : listKeyFrame )
                {
                    const float32 distance = MathUtil::abs( originX + document.computeRatioAtFrame( keyFrame ) * width - pointerX );
                    if ( distance <= bestDistance )
                    {
                        bestDistance = distance;
                        hit          = keyFrame;
                    }
                }
                return hit;
            }

            /** @brief 키 마름모 하나를 그립니다. */
            static void drawKeyDiamond( ImDrawList& drawList, float32 x, float32 centerY, float32 radius, uint32 color )
            {
                drawList.AddQuadFilled( ImVec2( x, centerY - radius ), ImVec2( x + radius, centerY ), ImVec2( x, centerY + radius ), ImVec2( x - radius, centerY ), color );
            }

            /** @brief 트랙 오류 배지의 풀이(이유와 고칠 일)입니다. */
            static const utf8* findTrackIssueText( SequenceTrackIssue issue )
            {
                switch ( issue )
                {
                    case SequenceTrackIssue::UnknownKind:
                        return SW_LOCKEY( "EditorSequencer", "TrackIssueUnknownKind" );
                    case SequenceTrackIssue::ChannelCount:
                        return SW_LOCKEY( "EditorSequencer", "TrackIssueChannelCount" );
                    case SequenceTrackIssue::MissingBinding:
                        return SW_LOCKEY( "EditorSequencer", "TrackIssueMissingBinding" );
                    case SequenceTrackIssue::MissingPropertyPath:
                        return SW_LOCKEY( "EditorSequencer", "TrackIssueMissingPropertyPath" );
                    case SequenceTrackIssue::KeysOutsideTimeline:
                        return SW_LOCKEY( "EditorSequencer", "TrackIssueKeysOutsideTimeline" );
                    case SequenceTrackIssue::None:
                        return "";
                }
                return "";
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
        , _listCutItem{}
        , _listPreviewSpan{}
        , _listPreviewSpawn{}
        , _curveState{}
        , _contextPlayer{}
        , _currentFrame{ 0 }
        , _firstFrame{ 0 }
        , _keyDragTrack{ -1 }
        , _keyDragDelta{ 0 }
        , _bExpanded{ true }
    {
        (void)_document.addItem( SequenceItemKind::Event, "Event" );
        _document._asset._listItem[0]._name = "Intro";
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
                releasePreview();
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
        EditorSelfTestMarks::record( "sequencer.scrub" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "ScrubTip" ) );
        drawViewControls();

        // 바인딩이 맨 위다 — 트랙 · 컷 · 항목이 모두 바인딩을 대상으로 고른다(언리얼 Sequencer 의 바인딩 줄 아래 트랙).
        drawBindings();
        drawCutLane();
        drawShotTools();
        // 키 트랙이 항목 위다 — 자주 고치는 쪽(키 · 커브)이 패널이 낮아도 보인다.
        drawKeyTracks();

        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "ClipsEventsAndCameraCuts" ) );
        // 고른 구간 항목을 지금 프레임에 붙인다(언리얼 Sequencer 의 섹션 복사 · 붙여넣기). 클립보드는 문서 안에 있어 다른 시퀀스로는 옮기지 않는다.
        const utf8* const pCopyItem = SW_LOCKEY( "EditorSequencer", "CopyItem" );
        ImGui::BeginDisabled( _document._selectedItem < 0 );
        if ( ImGui::Button( pCopyItem ) )
            (void)_document.copySelectedItem(); // 클립보드만 바뀐다 — 문서 편집이 아니다
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "sequencer.copyItem" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "CopyItemTip" ) );
        const utf8* const pPasteItem = SW_LOCKEY( "EditorSequencer", "PasteItem" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pPasteItem ) );
        ImGui::BeginDisabled( _document.hasCopiedItem() == false );
        if ( ImGui::Button( pPasteItem ) && _document.pasteItem( _currentFrame ) >= 0 )
        {
            notifyDocumentEdited( "Paste Sequence Item" );
            previewCurrentFrame();
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "sequencer.pasteItem" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "PasteItemTip" ) );
        drawItemFields();

        // ImSequencer 는 여러 항목을 그리는 위젯이라 IsItemEdited("마지막 항목")가 클립 끌기 · 더하기 · 지우기를 뜻하지 않는다 — 부르기 전후의 배치를 비교한다.
        const vector<SequenceTrackItem>& listItem = _document._asset._listItem;
        SequenceClipTiming::captureTiming( listItem, _listTimingBefore );
        const int32  frameBefore     = _currentFrame;
        const size_t itemCountBefore = listItem.size();
        int32        selectedItem    = _document._selectedItem;
        _sequence->_bDraftOpened     = SW_FALSE;
        ImSequencer::Sequencer( _sequence.get(), &_currentFrame, &_bExpanded, &selectedItem, &_firstFrame,
                                ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_ADD | ImSequencer::SEQUENCER_DEL | ImSequencer::SEQUENCER_CHANGE_FRAME );
        // 위젯은 지운 항목 뒤의 선택을 당겨 주지 않는다(이름 칸이 옆 항목을 고친다) — 지웠으면 문서(`removeItem`)가 맞춘 번호를 쓴다.
        // 더하기 뒤에는 끝 항목을 고르는데, 초안을 열었으면 새 항목이 아니라 앞 항목이다 — 그 선택도 버린다.
        if ( listItem.size() >= itemCountBefore && _sequence->_bDraftOpened == SW_FALSE )
            _document._selectedItem = selectedItem;
        if ( SequenceClipTiming::hasTimingChanged( _listTimingBefore, listItem ) )
            notifyDocumentEdited( "Edit Sequence Timeline", "sequence-timeline" );
        if ( _currentFrame != frameBefore )
            previewCurrentFrame();

        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "CinematicNote" ) );                 // 폭 전체 칸이라 이름표는 위에(오른쪽 이름표는 창 밖으로 잘린다)
        (void)EditorWidgets::drawTextFieldMultiline( "##CinematicNote", _cinematicNote, 2 ); // 바뀐 때는 아래 IsItemDeactivatedAfterEdit 가 본다
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Sequence Note", "sequence-note" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "CinematicNoteTip" ) );
    }

    SequencePlayerComponent* SequencerPanel::findBindingContext() const
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager == nullptr || getLoadedAssetPath().empty() )
            return nullptr;
        SequencePlayerComponent* pFirst = nullptr;
        for ( GameObject* pObject : pManager->getAllGameObjects() )
        {
            SequencePlayerComponent* pPlayer = pObject != nullptr ? pObject->getComponent<SequencePlayerComponent>() : nullptr;
            // 리소스 id 는 대소문자를 가리지 않는다.
            if ( pPlayer == nullptr || StringUtil::equals( pPlayer->getSequencePath(), getLoadedAssetPath(), true ) == false )
                continue;
            if ( pObject->getHandle() == _contextPlayer )
                return pPlayer;
            if ( pFirst == nullptr )
                pFirst = pPlayer;
        }
        return pFirst;
    }

    SequenceBindingView SequencerPanel::makePreviewBindingView( const SequenceAsset& asset ) const
    {
        static const vector<SequenceBindingSlot> s_listNoSlot{}; // 문맥이 없을 때 가리킬 빈 표(읽기만 한다)
        const SequencePlayerComponent*           pContext = findBindingContext();
        SequenceBindingView                      view;
        view._pAsset       = &asset;
        view._pListSlot    = pContext != nullptr ? &pContext->getBindings() : &s_listNoSlot;
        view._pListSpawned = &_listPreviewSpawn;
        return view;
    }

    const utf8* SequencerPanel::findBindingName( uint32 bindingID ) const
    {
        const SequenceBinding* pBinding = _document._asset.findBinding( bindingID );
        return pBinding != nullptr ? pBinding->_name.c_str() : "?";
    }

    uint32 SequencerPanel::countUnboundBindings() const
    {
        const SequenceBindingView view     = makePreviewBindingView( _document._asset );
        GameObjectManager*        pManager = editor::getActiveObjectManager();
        uint32                    unbound  = 0;
        for ( const SequenceBinding& binding : _document._asset._listBinding )
        {
            const bool bSpawned = _document._asset.findSpawnItem( binding._id ) != nullptr;
            if ( bSpawned == false && SequenceTimelineUtil::findTarget( pManager, view, binding._id ) == nullptr )
                ++unbound;
        }
        return unbound;
    }

    void SequencerPanel::bindSelectionTo( uint32 bindingID )
    {
        SequencePlayerComponent* pContext = findBindingContext();
        GameObject*              pTarget  = SequencerPanelInternal::findSelectedObject();
        GameObject*              pOwner   = pContext != nullptr ? pContext->getOwner() : nullptr;
        if ( pOwner == nullptr || pTarget == nullptr )
            return;
        // 묶음은 씬의 데이터(플레이어의 바인딩 표)다 — 오브젝트 수정으로 되돌리기에 남긴다.
        const ObjectSnapshot before = EditorTransaction::captureSnapshot( pOwner );
        pContext->setBinding( bindingID, pTarget->getHandle() );
        EditorTransaction::recordModify( pOwner, before, EditorTransaction::captureSnapshot( pOwner ), "Bind Sequence Object" );
        previewCurrentFrame();
    }

    void SequencerPanel::drawBindings()
    {
        ImGui::SeparatorText( SW_LOCKEY( "EditorSequencer", "BindingsSection" ) );
        SequencePlayerComponent* pContext  = findBindingContext();
        GameObject*              pSelected = SequencerPanelInternal::findSelectedObject();
        if ( pContext == nullptr )
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSequencer", "NoBindingContextHint" ) );
        else
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EditorSequencer", "BindingContextFormat", TextArgumentList().addText( "player", pContext->getOwner()->getName().c_str() ) ).c_str() );

        const SequenceBindingView view       = makePreviewBindingView( _document._asset );
        GameObjectManager*        pManager   = editor::getActiveObjectManager();
        const float32             labelWidth = SequencerPanelInternal::kTrackLabelWidth * EditorThemeUtil::getDPIScale();
        for ( size_t bindingIndex = 0; bindingIndex < _document._asset._listBinding.size(); ++bindingIndex )
        {
            const SequenceBinding& binding = _document._asset._listBinding[bindingIndex];
            ImGui::PushID( static_cast<int32>( binding._id ) );
            if ( ImGui::Selectable( binding._name.c_str(), binding._id == _document._selectedBinding, ImGuiSelectableFlags_None, ImVec2( labelWidth, 0.0f ) ) )
                _document._selectedBinding = binding._id;
            fixed_string<constant::buffer::kMax64> mark;
            formatstring( mark.data(), mark.capacity(), "sequencer.binding.%#", bindingIndex );
            EditorSelfTestMarks::record( mark.c_str() );
            ImGui::SameLine();
            // 묶인 오브젝트 · 스폰 · 묶이지 않음(오류 색 하나 — 고칠 일이 있는 줄)입니다.
            const GameObject* pBound   = SequenceTimelineUtil::findTarget( pManager, view, binding._id );
            const bool        bSpawned = _document._asset.findSpawnItem( binding._id ) != nullptr;
            if ( bSpawned )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorSequencer", "SpawnedBinding" ) );
            else if ( pBound != nullptr )
                ImGui::TextUnformatted( pBound->getName().c_str() );
            else
                ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", SW_LOCKEY( "EditorSequencer", "UnboundBinding" ) );
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EditorSequencer", "BindingUsesFormat", TextArgumentList().addInteger( "count", static_cast<int64>( _document._asset.countBindingUses( binding._id ) ) ) ).c_str() );
            ImGui::PopID();
        }
        if ( _document._asset._listBinding.empty() )
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSequencer", "NoBindingsHint" ) );

        const utf8* const pAdd = SW_LOCKEY( "EditorSequencer", "AddBindingFromSelection" );
        ImGui::BeginDisabled( pSelected == nullptr );
        if ( ImGui::Button( pAdd ) && pSelected != nullptr )
        {
            const uint32 bindingID = _document.addBinding( pSelected->getName().c_str() );
            notifyDocumentEdited( "Add Sequence Binding" );
            bindSelectionTo( bindingID );
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "sequencer.addBindingFromSelection" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "AddBindingFromSelectionTip" ) );

        const utf8* const pBind = SW_LOCKEY( "EditorSequencer", "BindSelection" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pBind ) );
        ImGui::BeginDisabled( pContext == nullptr || pSelected == nullptr || _document._asset.findBinding( _document._selectedBinding ) == nullptr );
        if ( ImGui::Button( pBind ) )
            bindSelectionTo( _document._selectedBinding );
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "sequencer.bindSelection" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "BindSelectionTip" ) );

        const utf8* const pDelete = SW_LOCKEY( "EditorSequencer", "DeleteBinding" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pDelete ) );
        const bool bUsed = _document._asset.countBindingUses( _document._selectedBinding ) > 0;
        ImGui::BeginDisabled( _document._asset.findBinding( _document._selectedBinding ) == nullptr || bUsed );
        if ( ImGui::Button( pDelete ) && _document.removeBinding( _document._selectedBinding ) )
            notifyDocumentEdited( "Delete Sequence Binding" );
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "DeleteBindingTip" ) );

        // 고른 바인딩의 이름(표시용 — 오브젝트를 찾는 데 쓰지 않는다)입니다.
        for ( SequenceBinding& binding : _document._asset._listBinding )
        {
            if ( binding._id != _document._selectedBinding )
                continue;
            string name = binding._name;
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorSequencer", "BindingNameLabel" ), SW_LOCKEY( "EditorSequencer", "BindingNameTip" ) );
            (void)EditorWidgets::drawTextField( "##bindingName", name, -1.0f );
            if ( ImGui::IsItemDeactivatedAfterEdit() && _document.renameBinding( binding._id, name ) )
                notifyDocumentEdited( "Rename Sequence Binding", "sequence-binding" );
            break;
        }
    }

    void SequencerPanel::drawCutLane()
    {
        ImGui::SeparatorText( SW_LOCKEY( "EditorSequencer", "CameraCutsSection" ) );
        const utf8* const pAddCut = SW_LOCKEY( "EditorSequencer", "AddCutButton" );
        ImGui::BeginDisabled( _document._asset.findBinding( _document._selectedBinding ) == nullptr );
        if ( ImGui::Button( pAddCut ) && _document.addCameraCut( _currentFrame ) >= 0 )
        {
            notifyDocumentEdited( "Add Camera Cut" );
            previewCurrentFrame();
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "sequencer.addCut" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "AddCutTip" ) );

        _document.collectCutLane( _listCutItem );
        const float32 height = ImGui::GetTextLineHeight() * SequencerPanelInternal::kCutLaneLines;
        const float32 width  = MathUtil::max( ImGui::GetContentRegionAvail().x, 1.0f );
        const ImVec2  origin = ImGui::GetCursorScreenPos();
        if ( ImGui::InvisibleButton( "##cutLane", ImVec2( width, height ) ) )
        {
            // 누른 프레임을 덮는 컷을 고르고 그 프레임으로 간다(언리얼 컷 트랙의 섹션 고르기).
            const int32 frame = _document.findFrameAtRatio( ( EditorInputGUI::readPointer()._position._x - origin.x ) / width );
            for ( const int32 cutIndex : _listCutItem )
            {
                const SequenceTrackItem& cut = _document._asset._listItem[static_cast<size_t>( cutIndex )];
                if ( cut._start <= frame && frame <= cut._end )
                    _document._selectedItem = cutIndex;
            }
            _currentFrame = frame;
            previewCurrentFrame();
        }
        EditorSelfTestMarks::record( "sequencer.cutLane" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "CutLaneTip" ) );
        handleTimelineWheel( origin.x, width );

        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        pDrawList->AddRectFilled( origin, ImVec2( origin.x + width, origin.y + height ), ImGui::GetColorU32( ImGuiCol_FrameBg ) );
        for ( const int32 cutIndex : _listCutItem )
        {
            const SequenceTrackItem& cut   = _document._asset._listItem[static_cast<size_t>( cutIndex )];
            const float32            left  = origin.x + MathUtil::saturate( _document.computeRatioAtFrame( static_cast<float32>( cut._start ) ) ) * width;
            const float32            right = origin.x + MathUtil::saturate( _document.computeRatioAtFrame( static_cast<float32>( cut._end + 1 ) ) ) * width;
            const bool               bPick = cutIndex == _document._selectedItem;
            const uint32             fill  = bPick ? ImGui::GetColorU32( ImGuiCol_HeaderActive ) : cut._color;
            pDrawList->AddRectFilled( ImVec2( left, origin.y ), ImVec2( MathUtil::max( right - 1.0f, left + 1.0f ), origin.y + height ), fill );
            // 컷 이름이 아니라 카메라(바인딩) 이름을 적는다 — 촬영자가 보는 것은 어느 카메라인지다. 칸보다 길면 잘린다.
            const ImVec4 clip( left, origin.y, right, origin.y + height );
            pDrawList->AddText( nullptr, 0.0f, ImVec2( left + ImGui::GetStyle().FramePadding.x, origin.y + ( height - ImGui::GetTextLineHeight() ) * 0.5f ), ImGui::GetColorU32( ImGuiCol_Text ),
                                findBindingName( cut._binding ), nullptr, 0.0f, &clip );
        }
        if ( _listCutItem.empty() )
            pDrawList->AddText( ImVec2( origin.x + ImGui::GetStyle().FramePadding.x, origin.y + ( height - ImGui::GetTextLineHeight() ) * 0.5f ), ImGui::GetColorU32( ImGuiCol_TextDisabled ),
                                SW_LOCKEY( "EditorSequencer", "NoCutsHint" ) );
        const float32 cursorRatio = _document.computeRatioAtFrame( static_cast<float32>( _currentFrame ) );
        if ( 0.0f <= cursorRatio && cursorRatio <= 1.0f ) // 확대해 지금 프레임이 보이는 범위 밖이면 그리지 않는다
            pDrawList->AddLine( ImVec2( origin.x + cursorRatio * width, origin.y ), ImVec2( origin.x + cursorRatio * width, origin.y + height ), ImGui::GetColorU32( ImGuiCol_CheckMark ) );
    }

    void SequencerPanel::drawShotTools()
    {
        // 컷 차선이 쇼트 목록이다(언리얼 Camera Cut 트랙 · Shot 트랙의 쇼트 하나 = 컷 하나). 하위 시퀀스(Shot 트랙)는 두지 않는다 — 쇼트를 따로 저작할 요구가 확인되기 전까지 한 시퀀스의 컷으로 짠다.
        const utf8* const pSplit = SW_LOCKEY( "EditorSequencer", "SplitCut" );
        if ( ImGui::Button( pSplit ) && _document.splitCameraCut( _currentFrame ) >= 0 )
        {
            notifyDocumentEdited( "Split Camera Cut" );
            previewCurrentFrame();
        }
        EditorSelfTestMarks::record( "sequencer.splitCut" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "SplitCutTip" ) );

        const SequenceTrackItem*    pSelected     = _document._selectedItem >= 0 && _document._selectedItem < static_cast<int32>( _document._asset._listItem.size() )
                                                      ? &_document._asset._listItem[static_cast<size_t>( _document._selectedItem )]
                                                      : nullptr;
        const SequenceItemKindInfo* pSelectedKind = pSelected != nullptr ? SequenceAsset::findItemKindInfo( pSelected->_kind ) : nullptr;
        const bool                  bCutSelected  = pSelectedKind != nullptr && pSelectedKind->_bCutsCamera;

        const utf8* const pDelete = SW_LOCKEY( "EditorSequencer", "DeleteCut" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pDelete ) );
        ImGui::BeginDisabled( bCutSelected == false );
        if ( ImGui::Button( pDelete ) && _document.removeCameraCut( _document._selectedItem ) )
        {
            notifyDocumentEdited( "Delete Camera Cut" );
            previewCurrentFrame();
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "sequencer.deleteCut" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "DeleteCutTip" ) );

        const utf8* const pCamera = SW_LOCKEY( "EditorSequencer", "CutCamera" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pCamera ) );
        ImGui::BeginDisabled( bCutSelected == false || pSelected->_binding == _document._selectedBinding || _document._asset.findBinding( _document._selectedBinding ) == nullptr );
        if ( ImGui::Button( pCamera ) && _document.setCutCamera( _document._selectedItem, _document._selectedBinding ) )
        {
            notifyDocumentEdited( "Set Camera Cut Camera" );
            previewCurrentFrame();
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "sequencer.cutCamera" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "CutCameraTip" ) );

        _document.collectCutLane( _listCutItem ); // 위 단추가 컷을 나누거나 지웠을 수 있다 — 옛 번호로 그리지 않는다
        if ( _listCutItem.empty() )
            return; // 빈 차선은 컷 차선이 이유와 할 일을 적는다
        constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH;
        if ( ImGui::BeginTable( "##shotList", 4, kFlags ) == false )
            return;
        ImGui::TableSetupColumn( SW_LOCKEY( "EditorSequencer", "ShotCameraColumn" ) );
        ImGui::TableSetupColumn( SW_LOCKEY( "EditorSequencer", "ShotStartColumn" ) );
        ImGui::TableSetupColumn( SW_LOCKEY( "EditorSequencer", "ShotEndColumn" ) );
        ImGui::TableSetupColumn( SW_LOCKEY( "EditorSequencer", "ShotLengthColumn" ) );
        ImGui::TableHeadersRow();
        for ( size_t shotIndex = 0; shotIndex < _listCutItem.size(); ++shotIndex )
        {
            const int32              cutIndex = _listCutItem[shotIndex];
            const SequenceTrackItem& cut      = _document._asset._listItem[static_cast<size_t>( cutIndex )];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID( cutIndex );
            // 줄을 누르면 그 컷을 고르고 첫 프레임으로 간다(쇼트 목록에서 쇼트로 가기).
            if ( ImGui::Selectable( findBindingName( cut._binding ), cutIndex == _document._selectedItem, ImGuiSelectableFlags_SpanAllColumns ) )
            {
                _document._selectedItem = cutIndex;
                _currentFrame           = cut._start;
                previewCurrentFrame();
            }
            fixed_string<constant::buffer::kMax64> mark;
            formatstring( mark.data(), mark.capacity(), "sequencer.shot.%#", shotIndex );
            EditorSelfTestMarks::record( mark.c_str() );
            ImGui::PopID();
            // 숫자는 오른쪽 맞춤(표는 규칙과 정렬).
            const int32 arrValue[] = { cut._start, cut._end, cut._end - cut._start + 1 };
            for ( const int32 value : arrValue )
            {
                ImGui::TableNextColumn();
                fixed_string<constant::buffer::kMax32> text;
                formatstring( text.data(), text.capacity(), "%#", value );
                ImGui::SetCursorPosX( ImGui::GetCursorPosX() + MathUtil::max( 0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( text.c_str() ).x ) );
                ImGui::TextUnformatted( text.c_str() );
            }
        }
        ImGui::EndTable();
    }

    void SequencerPanel::drawViewControls()
    {
        constexpr float32 kZoomStep = 0.5f; // 단추 한 번에 폭이 반(확대) · 두 배(축소)
        const utf8* const pZoomIn   = SW_LOCKEY( "EditorSequencer", "ZoomIn" );
        if ( ImGui::Button( pZoomIn ) )
            _document.zoomView( kZoomStep, _document.computeRatioAtFrame( static_cast<float32>( _currentFrame ) ) ); // 보기만 바뀐다 — 되돌리기에 남기지 않는다
        EditorSelfTestMarks::record( "sequencer.zoomIn" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "ZoomInTip" ) );
        const utf8* const pZoomOut = SW_LOCKEY( "EditorSequencer", "ZoomOut" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pZoomOut ) );
        ImGui::BeginDisabled( _document.isViewFitted() );
        if ( ImGui::Button( pZoomOut ) )
            _document.zoomView( 1.0f / kZoomStep, _document.computeRatioAtFrame( static_cast<float32>( _currentFrame ) ) );
        EditorSelfTestMarks::record( "sequencer.zoomOut" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "ZoomOutTip" ) );
        const utf8* const pFit = SW_LOCKEY( "EditorSequencer", "ZoomFit" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pFit ) );
        if ( ImGui::Button( pFit ) )
            _document.fitView();
        EditorSelfTestMarks::record( "sequencer.zoomFit" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "ZoomFitTip" ) );
        ImGui::EndDisabled();

        // 확대한 동안만 스크롤 막대가 뜻이 있다 — 막대 값은 보이는 첫 프레임이다.
        if ( _document.isViewFitted() )
            return;
        ImGui::SameLine();
        const int32 firstFrame = _document.getViewFirstFrame();
        const int32 lastFirst  = _document._asset._frameMax - _document.getViewFrameSpan();
        int32       scroll     = firstFrame;
        ImGui::SetNextItemWidth( -1.0f );
        if ( ImGui::SliderInt( "##viewScroll", &scroll, _document._asset._frameMin, MathUtil::max( lastFirst, _document._asset._frameMin ), SW_LOCKEY( "EditorSequencer", "ViewRangeFormat" ) ) )
            _document.setViewFirstFrame( scroll );
        EditorSelfTestMarks::record( "sequencer.viewScroll" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "ViewScrollTip" ) );
    }

    void SequencerPanel::handleTimelineWheel( float32 originX, float32 width )
    {
        if ( ImGui::IsItemHovered() == false )
            return;
        constexpr float32        kWheelZoom   = 0.8f; // 휠 한 칸에 폭이 0.8 배(확대) — 유니티 Timeline 과 비슷한 걸음
        constexpr float32        kWheelScroll = 0.1f; // 휠 한 칸에 보이는 폭의 1/10 만큼 스크롤
        const EditorPointerState pointer      = EditorInputGUI::readPointer();
        const float32            wheel        = pointer._wheel;
        if ( wheel != 0.0f && pointer.hasModifier( ModifierKey::Ctrl ) )
        {
            const float32 anchor = ( pointer._position._x - originX ) / MathUtil::max( width, 1.0f );
            _document.zoomView( MathUtil::pow( kWheelZoom, wheel ), anchor );
            return;
        }
        const float32 scrollWheel = pointer.hasModifier( ModifierKey::Shift ) ? wheel : pointer._wheelHorizontal;
        if ( scrollWheel != 0.0f )
            _document.scrollView( static_cast<int32>( MathUtil::round( -scrollWheel * kWheelScroll * static_cast<float32>( _document.getViewFrameSpan() ) ) ) );
    }

    bool SequencerPanel::drawItemBindingCombo( SequenceTrackItem& edit, const SequenceItemKindInfo& kindInfo )
    {
        bool        bEdited   = false;
        const bool  bRequired = kindInfo.requiresField( SequenceItemField::Binding );
        const utf8* pPreview  = edit._binding == kSequenceNoBinding ? SW_LOCKEY( "EditorSequencer", "NoBindingChoice" ) : findBindingName( edit._binding );
        if ( EditorWidgets::beginCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EditorSequencer", "BindingLabel" ), "Binding" ), pPreview ) )
        {
            if ( bRequired == false && ImGui::Selectable( SW_LOCKEY( "EditorSequencer", "NoBindingChoice" ), edit._binding == kSequenceNoBinding ) )
            {
                edit._binding = kSequenceNoBinding;
                bEdited       = true;
            }
            for ( const SequenceBinding& binding : _document._asset._listBinding )
            {
                ImGui::PushID( static_cast<int32>( binding._id ) );
                if ( ImGui::Selectable( binding._name.c_str(), binding._id == edit._binding ) )
                {
                    edit._binding = binding._id;
                    bEdited       = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::record( "sequencer.item.binding" );
        EditorWidgets::drawTooltip( kindInfo._bSpawns ? SW_LOCKEY( "EditorSequencer", "SpawnBindingTip" ) : SW_LOCKEY( "EditorSequencer", "ItemBindingTip" ) );
        return bEdited;
    }

    void SequencerPanel::drawItemFields()
    {
        constexpr float32 kClipFieldRowCount = 4.0f; // 이름 · 바인딩 · 종류의 칸 · 안내(초안이면 만들기 단추)
        // 고른 항목의 편집 필드는 늘 같은 높이의 구역에 그린다 — 고를 때만 필드가 생기면 그만큼 타임라인이 밀려 내려간다.
        EditorSectionDesc clipDesc{};
        clipDesc._pID       = "##sequence_clip";
        clipDesc._kind      = EditorSectionKind::Child;
        clipDesc._childSize = float2{ 0.0f, ImGui::GetFrameHeightWithSpacing() * kClipFieldRowCount };
        EditorChrome::beginSection( clipDesc );
        // 입력 중이 아니면 문서에서 다시 뜬다(되돌리기 · 선택 변경이 칸에 보인다) — 커브 편집기의 사본과 같은 규칙이다.
        if ( ImGui::IsAnyItemActive() == false )
            _document.syncItemEdit();
        const SequenceItemKindInfo* pKindInfo = _document.hasEditedItem() ? SequenceAsset::findItemKindInfo( _document._itemEdit._kind ) : nullptr;
        if ( pKindInfo == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSequencer", "SelectItemHint" ) );
        }
        else
        {
            SequenceTrackItem& edit    = _document._itemEdit;
            bool               bEdited = SequencerPanelInternal::drawItemField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "ItemName" ), "Item Name" ), edit._name, nullptr, nullptr, "sequencer.item.name" );
            if ( pKindInfo->allowsField( SequenceItemField::Binding ) )
                bEdited = drawItemBindingCombo( edit, *pKindInfo ) || bEdited;
            // 종류가 쓰는 칸만 그린다 — 칸 목록은 종류 표 한 곳이다(`SequenceItemKindInfo::_allowedFields`).
            for ( const SequenceItemFieldView& view : kArrItemFieldView )
            {
                string* pText = edit.accessTextField( view._field );
                if ( pText == nullptr || pKindInfo->allowsField( view._field ) == false )
                    continue;
                bEdited = SequencerPanelInternal::drawItemField( EditorThemeUtil::makeStableLabel( SequencerPanelInternal::findFieldLabel( view._field ), view._pStableID ), *pText,
                                                                 SequencerPanelInternal::findFieldTip( view._field ), view._pDropExtension, view._pMark ) ||
                          bEdited;
            }

            if ( _document.isDraftingItem() )
            {
                drawItemDraftButtons();
            }
            else
            {
                // 거절되면 칸이 항목의 값으로 돌아간다. 비운 채 치는 동안에는 아래 안내가 그 사실을 미리 알린다.
                if ( bEdited && _document.commitItemEdit() )
                {
                    notifyDocumentEdited( "Edit Sequence Item", "sequence-item" );
                    previewCurrentFrame();
                }
                if ( _document.isItemAcceptable( edit ) == false )
                    ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", SW_LOCKEY( "EditorSequencer", "RequiredFieldHint" ) );
            }
        }
        EditorChrome::endSection();
    }

    void SequencerPanel::drawItemDraftButtons()
    {
        const utf8* const pCreate = SW_LOCKEY( "EditorSequencer", "CreateItemButton" );
        ImGui::BeginDisabled( _document.isItemAcceptable( _document._itemEdit ) == false );
        if ( ImGui::Button( pCreate ) && _document.commitItemDraft() >= 0 )
            notifyDocumentEdited( "Add Sequence Item" );
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "sequencer.item.create" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "CreateItemTip" ) );
        const utf8* const pCancel = SW_LOCKEY( "EdVerb", "Cancel" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pCancel ) );
        if ( ImGui::Button( pCancel ) )
            _document.cancelItemDraft();
        EditorSelfTestMarks::record( "sequencer.item.cancel" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "CancelItemTip" ) );
        if ( _document.isDraftingItem() )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorSequencer", "DraftHint" ) );
        }
    }

    void SequencerPanel::drawKeyTracks()
    {
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "KeyTracks" ) );
        // 트랙은 고른 바인딩에 붙는다 — 고른 바인딩이 없으면 더할 대상이 없다.
        ImGui::BeginDisabled( _document._asset.findBinding( _document._selectedBinding ) == nullptr );
        // 종류 표를 돈다 — 새 트랙 종류는 엔진 표(`SequenceAsset::findTrackKindInfo`)의 한 줄이고 여기를 고치지 않는다.
        for ( int32 kindIndex = 0; kindIndex < static_cast<int32>( SequenceTrackKind::Count ); ++kindIndex )
        {
            const SequenceTrackKind      kind  = static_cast<SequenceTrackKind>( kindIndex );
            const SequenceTrackKindInfo* pInfo = SequenceAsset::findTrackKindInfo( kind );
            if ( pInfo == nullptr )
                continue;
            fixed_string<constant::buffer::kMax64> label;
            formatstring( label.data(), label.capacity(), "+ %# Track", pInfo->_pNamePrefix );
            if ( kindIndex > 0 )
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( label.c_str() ) );
            if ( ImGui::Button( label.c_str() ) )
                addKeyTrack( kind );
            const utf8*                            pKindName = editor::getSubsystem<TypeRegistry>().enumToString( kind );
            fixed_string<constant::buffer::kMax64> mark;
            formatstring( mark.data(), mark.capacity(), "sequencer.addTrack.%#", pKindName != nullptr ? pKindName : "?" );
            EditorSelfTestMarks::record( mark.c_str() );
            EditorWidgets::drawTooltip( SW_LOCKEYFORMAT( "EditorSequencer", "AddTrackTipFormat", TextArgumentList().addText( "kind", pInfo->_pNamePrefix ) ).c_str() );
        }
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "DeleteTrack" ) ) );
        ImGui::BeginDisabled( _document._selectedTrack < 0 );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "DeleteTrack" ) ) && _document.removeSelectedTrack() )
            notifyDocumentEdited( "Delete Sequence Track" );
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "DeleteTrackTip" ) );
        // 순서는 파일의 트랙 순서다(재생 결과는 같다 — 트랙은 서로 다른 값을 몬다). 언리얼 Sequencer 는 트리 줄을 끌어 바꾼다.
        const int32       trackCount = static_cast<int32>( _document._asset._listTrack.size() );
        const utf8* const pMoveUp    = SW_LOCKEY( "EditorSequencer", "MoveTrackUp" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pMoveUp ) );
        ImGui::BeginDisabled( _document._selectedTrack <= 0 || _document._selectedTrack >= trackCount );
        if ( ImGui::Button( pMoveUp ) && _document.moveTrack( _document._selectedTrack, _document._selectedTrack - 1 ) )
            notifyDocumentEdited( "Move Sequence Track" );
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "sequencer.moveTrackUp" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "MoveTrackUpTip" ) );
        const utf8* const pMoveDown = SW_LOCKEY( "EditorSequencer", "MoveTrackDown" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pMoveDown ) );
        ImGui::BeginDisabled( _document._selectedTrack < 0 || _document._selectedTrack + 1 >= trackCount );
        if ( ImGui::Button( pMoveDown ) && _document.moveTrack( _document._selectedTrack, _document._selectedTrack + 1 ) )
            notifyDocumentEdited( "Move Sequence Track" );
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "sequencer.moveTrackDown" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "MoveTrackDownTip" ) );
        const utf8* const pCopyKeys = SW_LOCKEY( "EditorSequencer", "CopyKeys" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pCopyKeys ) );
        ImGui::BeginDisabled( _document.getSelectedKeys().empty() );
        if ( ImGui::Button( pCopyKeys ) )
            (void)_document.copySelectedKeys(); // 클립보드만 바뀐다 — 문서 편집이 아니다
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "sequencer.copyKeys" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "CopyKeysTip" ) );
        const utf8* const pPasteKeys = SW_LOCKEY( "EditorSequencer", "PasteKeys" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pPasteKeys ) );
        ImGui::BeginDisabled( _document.hasCopiedKeys() == false || _document._selectedTrack < 0 );
        if ( ImGui::Button( pPasteKeys ) && _document.pasteKeys( _currentFrame ) > 0 )
        {
            notifyDocumentEdited( "Paste Sequence Keys" );
            previewCurrentFrame();
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "sequencer.pasteKeys" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "PasteKeysTip" ) );
        const utf8* const pDeleteKeys = SW_LOCKEY( "EditorSequencer", "DeleteSelectedKeys" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pDeleteKeys ) );
        ImGui::BeginDisabled( _document.getSelectedKeys().empty() );
        if ( ImGui::Button( pDeleteKeys ) && _document.removeSelectedKeys() > 0 )
        {
            notifyDocumentEdited( "Delete Sequence Keys" );
            previewCurrentFrame();
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "sequencer.deleteSelectedKeys" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "DeleteSelectedKeysTip" ) );

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
            // 오류 배지: 트랙마다 빨간 표식 하나와 이유 · 고칠 일(색만으로 알리지 않는다 — 풀이가 글로 있다). 배지가 없는 줄도 같은 폭을 비워 이름 열을 맞춘다.
            const SequenceTrackIssue issue = _document.findTrackIssue( trackIndex );
            const utf8* const        pMark = issue != SequenceTrackIssue::None ? "!" : " ";
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", pMark );
            if ( issue != SequenceTrackIssue::None )
            {
                EditorWidgets::drawTooltip( SequencerPanelInternal::findTrackIssueText( issue ) );
                fixed_string<constant::buffer::kMax64> issueMark;
                formatstring( issueMark.data(), issueMark.capacity(), "sequencer.trackIssue.%#", trackIndex );
                EditorSelfTestMarks::record( issueMark.c_str() );
            }
            ImGui::SameLine();
            fixed_string<constant::buffer::kMax256> label;
            formatstring( label.data(), label.capacity(), "%# (%#)", track._name.empty() ? "Track" : track._name.c_str(), findBindingName( track._binding ) );
            if ( ImGui::Selectable( label.c_str(), trackIndex == _document._selectedTrack, ImGuiSelectableFlags_None, ImVec2( labelWidth, 0.0f ) ) )
                _document._selectedTrack = trackIndex;
            fixed_string<constant::buffer::kMax64> mark;
            formatstring( mark.data(), mark.capacity(), "sequencer.track.%#", trackIndex );
            EditorSelfTestMarks::record( mark.c_str() );
            ImGui::SameLine();
            drawKeyStrip( track, trackIndex );
            formatstring( mark.data(), mark.capacity(), "sequencer.keyStrip.%#", trackIndex );
            EditorSelfTestMarks::record( mark.c_str() );
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
        const float32 radius = SequencerPanelInternal::kKeyMarkerRadius * EditorThemeUtil::getDPIScale();
        track.collectKeyFrames( _listKeyFrame );
        (void)ImGui::InvisibleButton( "##keyStrip", ImVec2( width, height ) );
        handleTimelineWheel( origin.x, width );
        const EditorPointerState pointer = EditorInputGUI::readPointer();
        if ( ImGui::IsItemActivated() )
        {
            // 누른 자리에 키가 있으면 고르고(Ctrl 은 더하기 · 빼기) 끌기를 시작한다. 없으면 그 트랙 · 그 프레임으로 간다(예전 띠 누름과 같다).
            _document._selectedTrack              = trackIndex;
            const optional<float32> keyUnderMouse = SequencerPanelInternal::findKeyUnderPointer( _document, _listKeyFrame, origin.x, width, radius, pointer._position._x );
            if ( keyUnderMouse.has_value() )
            {
                _document.selectKey( trackIndex, *keyUnderMouse, pointer.hasModifier( ModifierKey::Ctrl ) );
                _keyDragTrack = trackIndex;
                _keyDragDelta = 0;
            }
            else
            {
                if ( pointer.hasModifier( ModifierKey::Ctrl ) == false )
                    _document.clearKeySelection();
                _currentFrame = _document.findFrameAtRatio( ( pointer._position._x - origin.x ) / width );
                previewCurrentFrame();
            }
        }
        const bool bDraggingHere = _keyDragTrack == trackIndex && ImGui::IsItemActive();
        if ( bDraggingHere && pointer.isDragging( EditorMouseButton::Left ) )
            _keyDragDelta = _document.computeFrameDelta( pointer._arrDragDelta[static_cast<size_t>( EditorMouseButton::Left )]._x, width );
        if ( _keyDragTrack == trackIndex && ImGui::IsItemDeactivated() )
        {
            // 놓을 때 한 번 옮긴다 — 되돌리기 한 단계, 정수 프레임 스냅. 타임라인 밖으로 나가는 끌기는 옮기지 않는다.
            if ( _keyDragDelta != 0 && _document.moveSelectedKeys( _keyDragDelta ) )
            {
                notifyDocumentEdited( "Move Sequence Keys" );
                previewCurrentFrame();
            }
            _keyDragTrack = -1;
            _keyDragDelta = 0;
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "KeyStripTip" ) );

        ImDrawList*   pDrawList = ImGui::GetWindowDrawList();
        const float32 centerY   = origin.y + height * 0.5f;
        pDrawList->AddRectFilled( origin, ImVec2( origin.x + width, origin.y + height ), ImGui::GetColorU32( ImGuiCol_FrameBg ) );
        const float32 dragOffset = _keyDragTrack >= 0 ? static_cast<float32>( _keyDragDelta ) : 0.0f;
        for ( const float32 keyFrame : _listKeyFrame )
        {
            // 고른 키는 선택 색(강조는 선택에만), 끄는 중이면 놓을 자리에 그린다. 고른 키는 모든 트랙에서 함께 움직인다.
            const bool    bSelected = _document.isKeySelected( trackIndex, keyFrame );
            const float32 drawFrame = bSelected ? keyFrame + dragOffset : keyFrame;
            const float32 ratio     = _document.computeRatioAtFrame( drawFrame );
            if ( ratio < 0.0f || ratio > 1.0f )
                continue; // 확대해 보이는 범위 밖의 키 — 띠 밖에 그리지 않는다
            SequencerPanelInternal::drawKeyDiamond( *pDrawList, origin.x + ratio * width, centerY, bSelected ? radius * 1.25f : radius,
                                                    ImGui::GetColorU32( bSelected ? ImGuiCol_CheckMark : ImGuiCol_PlotHistogram ) );
        }
        const float32 cursorRatio = _document.computeRatioAtFrame( static_cast<float32>( _currentFrame ) );
        if ( 0.0f <= cursorRatio && cursorRatio <= 1.0f )
            pDrawList->AddLine( ImVec2( origin.x + cursorRatio * width, origin.y ), ImVec2( origin.x + cursorRatio * width, origin.y + height ), ImGui::GetColorU32( ImGuiCol_Text ) );
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
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorSequencer", "TrackTargetLabel" ) );
        ImGui::TextUnformatted( findBindingName( track._binding ) );
        if ( track._kind == SequenceTrackKind::Property )
            drawTrackTextField( SW_LOCKEY( "EditorSequencer", "PropertyLabel" ), track._propertyPath, SW_LOCKEY( "EditorSequencer", "PropertyPathTip" ), true );
        else if ( track._kind == SequenceTrackKind::RigSlot )
            drawTrackTextField( SW_LOCKEY( "EditorSequencer", "RigSlotLabel" ), track._propertyPath, SW_LOCKEY( "EditorSequencer", "RigSlotTip" ), true );

        GameObjectManager*   pManager = editor::getActiveObjectManager();
        const float32        frame    = static_cast<float32>( _currentFrame );
        const SequenceAsset& asset    = _document._asset;
        GameObject*          pTarget  = SequenceTimelineUtil::findTarget( pManager, makePreviewBindingView( asset ), track._binding );
        float32              arrCurrent[kSequenceMaxChannelCount]{};
        const bool           bTargetFound = SequenceTimelineUtil::readTrackValues( pTarget, track, arrCurrent, kSequenceMaxChannelCount );

        drawKeyButtons( track, frame, arrCurrent, bTargetFound );
        drawChannelRows( track, frame, arrCurrent );
    }

    void SequencerPanel::drawKeyButtons( SequenceKeyTrack& track, float32 frame, const float32* pArrCurrent, bool bTargetFound )
    {
        ImGui::BeginDisabled( bTargetFound == false );
        if ( ImGui::Button( SW_LOCKEY( "EditorSequencer", "KeyButton" ) ) )
        {
            track.setKey( frame, pArrCurrent, kSequenceMaxChannelCount );
            notifyDocumentEdited( "Add Sequence Key" );
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "sequencer.addKey" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "AddKeyTip" ) );
        const utf8* const pDeleteKey = SW_LOCKEY( "EditorSequencer", "DeleteKeyButton" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pDeleteKey ) );
        ImGui::BeginDisabled( track.hasKeyAt( frame ) == false );
        if ( ImGui::Button( pDeleteKey ) && track.removeKeysAt( frame ) )
        {
            _document.clearKeySelection(); // 지운 키를 고른 채 두지 않는다
            notifyDocumentEdited( "Delete Sequence Key" );
            previewCurrentFrame();
        }
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSequencer", "DeleteKeyTip" ) );
        track.collectKeyFrames( _listKeyFrame );
        EditorChrome::sameLineIfFits( EditorChrome::measureArrowButton() );
        if ( ImGui::ArrowButton( "##previousKey", ImGuiDir_Left ) )
        {
            const optional<int32> previousFrame = _document.findPreviousKeyFrame( _listKeyFrame, _currentFrame );
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
            const optional<int32> nextFrame = _document.findNextKeyFrame( _listKeyFrame, _currentFrame );
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
            else if ( track._kind == SequenceTrackKind::CameraLens )
                pMissing = SW_LOCKEY( "EditorSequencer", "LensTargetNotFound" );
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
            const bool bRotation = SequenceModel::isRotationChannel( track, channelIndex );
            // 키가 없는 채널은 대상의 지금 값을 보인다.
            const float32 currentValue = channelIndex < kSequenceMaxChannelCount ? pArrCurrent[channelIndex] : 0.0f;
            float32       value        = SequenceModel::computeChannelDisplayValue( track, channelIndex, frame, currentValue );
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
                (void)SequenceModel::setChannelDisplayValue( track, channelIndex, frame, value );
                previewCurrentFrame();
            }
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sequence Key", "sequence-key" );
            fixed_string<constant::buffer::kMax64> mark;
            formatstring( mark.data(), mark.capacity(), "sequencer.channel.%#", channelIndex );
            EditorSelfTestMarks::record( mark.c_str() );
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
            EditorSelfTestMarks::record( mark.c_str() );
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
            if ( EditorCurveEditorState::drawEditor( _curveState, "sequencer.curve" ) )
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
        if ( _document.addTrack( kind ) )
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
        const SequenceAsset asset = captureAsset();
        applyPreviewFrame( asset, _currentFrame );
    }

    void SequencerPanel::applyPreviewFrame( const SequenceAsset& asset, int32 frame )
    {
        const SequencePlayerComponent* pContext = findBindingContext();
        const float32                  fps      = pContext != nullptr ? pContext->getFramesPerSecond() : 30.0f;
        EditorViewportPreview::applySequenceFrame( asset, makePreviewBindingView( asset ), frame, fps, _listPreviewSpan );
    }

    void SequencerPanel::releasePreview()
    {
        const SequenceAsset asset    = captureAsset();
        GameObjectManager*  pManager = editor::getActiveObjectManager();
        SequenceTimelineUtil::releaseSpans( pManager, asset, makePreviewBindingView( asset ), 30.0f, _listPreviewSpan );
        SequenceTimelineUtil::releaseCameraCut( pManager, asset );
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
            releasePreview(); // 앞 문서의 구간 번호를 새 문서에 쓰지 않는다
            applyAsset( asset );
            _document.resetSelection();
            _currentFrame                           = asset._frameMin;
            _contextPlayer                          = GameObjectHandle{};
            const SequencePlayerComponent* pContext = findBindingContext();
            if ( pContext != nullptr && pContext->getOwner() != nullptr )
                _contextPlayer = pContext->getOwner()->getHandle();
        }
        return result;
    }

    void SequencerPanel::saveToLoadedPath()
    {
        if ( getLoadedAssetPath().empty() )
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
        asset._note         = _cinematicNote;
        return asset;
    }

    void SequencerPanel::applyAsset( const SequenceAsset& asset )
    {
        _document.restoreAsset( asset );
        _cinematicNote = asset._note;
    }

    string SequencerPanel::captureDocumentText() const
    {
        return captureAsset().toXML();
    }

    void SequencerPanel::applyDocumentText( string_view text )
    {
        SequenceAsset restored;
        // 읽지 못한 스냅숏으로 문서를 비우면 다음 저장이 내용을 잃는다 — 지금 문서를 그대로 둔다.
        if ( text.empty() == false && restored.loadFromXMLText( text ) == false )
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
        applyPreviewFrame( _previewPlayer->getAsset(), _currentFrame );
    }
} // namespace sw::editor
