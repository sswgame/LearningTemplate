#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/Color.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/PropertyGrid/EditorPropertyGrid.h"
#include "Editor/Panels/AudioEvents/Model/AudioEventBrowserUtil.h"
#include "Editor/Panels/AudioEvents/Model/AudioEventEditModel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Audio/AudioEngineState.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/TypeInfo.h"

#include <imgui.h>

namespace sw
{
    struct PropertyInfo;
} // namespace sw

namespace sw::editor
{
    /**
     * @class AudioEventBrowserPanel
     * @brief 언리얼 Sound Cue 에디터의 속성 보기 · 오디오 디버거, FMOD Studio 이벤트 속성 · 프로파일러에 해당합니다.
     * @details 이벤트 속성은 기본이 읽기 보기다. "고치기" 를 누르면 그 이벤트가 있는 라이브러리 파일을 사본(`AudioEventEditModel`)으로 다시 읽어 속성 그리드로 고치고
     *          (이벤트 복제 · 지우기, 라이브러리의 파라미터 · 감쇠 프리셋 포함 — FMOD Studio 의 이벤트 · 파라미터 편집에 해당),
     *          "저장" 은 검사를 통과할 때만 파일에 쓴다(`AudioEventLibrary::saveToAsset`). 엔진에 올라간 라이브러리는 저장한 파일을 에셋 핫 리로드
     *          (`AudioSubsystem` 의 라이브러리 다시 읽기)가 바꾼다 — 창은 엔진 사본을 직접 고치지 않는다(오디오 스레드가 설명자를 참조한다).
     *          재생 중 인스턴스와 보이스 수는 오디오 스레드가 블록마다 게시한 사본이다.
     *          고치는 라이브러리 파일이 이 창의 문서다(`EditorDocumentPanel`, Data 종류 가운데 `*.audioevents.xml`) — "고치기" · 콘텐츠 브라우저 · `asset.openScratch` 가 모두 워크스페이스 포커스로 연다.
     *          예전에는 "고치기" 만 여는 자체 길이라 시나리오가 사본(`scratch/…`)을 가리킬 수 없었다.
     */
    class AudioEventBrowserPanel final : public EditorDocumentPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "audioevents";
        /** @brief 처음 패널 크기를 정하는 글 칸 수입니다(폭 · 높이). */
        static constexpr float32 kInitialTextWidth  = 109.0f;
        static constexpr float32 kInitialTextHeight = 27.0f;

        AudioEventBrowserPanel();

        const utf8* getPanelTitle() const override { return "Audio Events"; }

        EditorDockSlot   getDefaultDockSlot() const override { return EditorDockSlot::Floating; } // 도구 창 — 문서 기반의 기본(가운데 탭)이 아니라 옮기기 전처럼 떠 있는 창
        void             drawDocumentContent() override;
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( kInitialTextWidth, kInitialTextHeight ); }
        /** @brief 고치는 라이브러리 사본을 파일에 씁니다(닫기 · 종료 확인의 Save). 검사를 통과하지 못하면 쓰지 않고 false 입니다. */
        [[nodiscard]] bool saveDocument( AssetWriter& writer ) override;
        /** @brief 읽은 라이브러리를 고치는 중일 때만 저장합니다("고치기 그만" 뒤에는 쓸 사본이 없다). */
        bool canSaveDocument() const override { return EditorDocumentPanel::canSaveDocument() && _edit.isEditingAny(); }
        /** @brief 엔진에서 모은 이벤트 이름 수입니다(감시 값 `Editor.AudioEventCount`). */
        uint32 getEventCount() const { return static_cast<uint32>( _listEventName.size() ); }
        /** @brief 목록에서 이벤트를 골랐습니다(감시 값 `Editor.AudioEventSelected`). */
        bool hasSelectedEvent() const { return _selectedEvent.empty() == false; }
        /** @brief 고치는 라이브러리 사본의 이벤트 수입니다. 고치는 중이 아니면 0 입니다(감시 값 `Editor.AudioEventEditCount`). */
        uint32 getEditEventCount() const { return _edit.isEditingAny() ? static_cast<uint32>( _edit.getLibrary()._listEvent.size() ) : 0u; }

    protected:
        /** @brief 이벤트 라이브러리 파일(`*.audioevents.xml`)만 받습니다. */
        bool canAcceptDocumentPath( string_view assetPath ) const override;
        /** @brief 받아들인 라이브러리 파일을 사본으로 읽어 고치기를 시작합니다. 같은 라이브러리를 다시 읽으면 고치던 이벤트를 그대로 고릅니다. 읽지 못하면 `Malformed` 입니다. */
        ToolAssetLoadResult loadDocument() override;

    private:
        /** @brief 사본 라이브러리의 저장 글입니다(문서 되돌리기 — 감사 E-13). */
        string captureDocumentText() const override;
        /** @brief 되돌리기가 준 글로 사본을 다시 읽습니다. */
        void applyDocumentText( string_view text ) override;
        void drawEventList();
        void drawEventDetail();
        /**
         * @brief 고치는 라이브러리 사본을 그립니다 — 저장 · 그만두기, 고칠 이벤트 고르기 · 복제 · 지우기, 이벤트 속성 그리드, 라이브러리의 파라미터 · 감쇠 프리셋 그리드.
         * @details 고칠 이벤트(`AudioEventEditModel::getEventName`)는 사본에서 고른다. 복제 · 지운 이벤트는 저장 뒤 핫 리로드가 엔진 목록에 올리기 전까지 사본에만 있다.
         */
        void drawLibraryEditor();
        /** @brief 속성 그리드가 사본을 고쳤다는 알림입니다(이벤트 · 라이브러리 그리드 둘 다). 저장 전 상태로 표시합니다. */
        void onEventEdited( const PropertyInfo& prop );
        void drawPlayingInstances();
        void drawVoiceProfiler( const AudioEngineStats& stats );

    private:
        AudioEventEditModel                     _edit;             ///< 고치기 상태
        EditorPropertyGrid                      _grid;             ///< 사본 이벤트의 속성 그리드
        EditorPropertyGrid                      _libraryGrid;      ///< 사본 라이브러리의 파라미터 · 감쇠 프리셋 그리드
        vector<hashed_string>                   _listEventName;    ///< 엔진이 준 이벤트 이름(순서 없음)
        vector<hashed_string>                   _listVisibleEvent; ///< 필터를 거친 사전순 이름
        vector<AudioEventFieldRow>              _listFieldRow;
        vector<AudioInstanceStatus>             _listInstanceStatus;
        vector<AudioInstanceStatus>             _listInstanceRow;
        hashed_string                           _selectedEvent;
        hashed_string                           _selectedLibrary; ///< 고른 이벤트가 들어 있는 라이브러리(리소스 경로)
        fixed_string<constant::buffer::kMax128> _filter;
        bool                                    _bOnlySelectedEvent;     ///< 재생 중 목록을 고른 이벤트로 거른다. NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
        uint8                                   _bVisibleEventStale : 1; ///< 이름 목록이 바뀌어 다시 걸러야 한다
        const void*                             _pEventNameEngine;       ///< `_listEventName` 을 모은 엔진의 표지(비교만 — 엔진이 새로 뜨면 세대가 다시 1 부터라 함께 본다)
        uint32                                  _eventNameGeneration;    ///< `_listEventName` 을 모은 때의 `AudioEngine::getEventLibraryGeneration`
        string                                  _visibleEventFilter;     ///< `_listVisibleEvent` 를 거른 검색 글
    };

    namespace
    {
        struct AudioEventBrowserPanelInternal
        {
            /** @brief 사용률 막대 하나를 그립니다. 상한에 닿으면 경고색입니다. */
            static void drawUsageBar( const utf8* pLabel, uint32 count, uint32 limit, float32 usage, bool bLimitReached )
            {
                ImGui::TextUnformatted( pLabel );
                ImGui::SameLine( 160.0f * EditorThemeUtil::getDPIScale() );
                fixed_string<constant::buffer::kMax64> overlay;
                if ( limit == 0 )
                    formatstring( overlay.data(), overlay.capacity(), "%# / -", count );
                else
                    formatstring( overlay.data(), overlay.capacity(), "%# / %#", count, limit );
                ImGui::PushStyleColor( ImGuiCol_PlotHistogram, EditorColorUtil::toImVec4( bLimitReached ? SW_THEME_COLOR( "Signal", "Warning" ) : SW_THEME_COLOR( "Signal", "Success" ) ) );
                ImGui::ProgressBar( usage, ImVec2{ -1.0f, 0.0f }, overlay.c_str() );
                ImGui::PopStyleColor();
            }

            /** @brief 열린 오디오 이벤트 창입니다. 닫혔으면 nullptr 입니다(감시 값이 읽는다). */
            [[nodiscard]] static const AudioEventBrowserPanel* findOpenPanel()
            {
                return EditorPanelManager::findOpenPanelInEditorContext<AudioEventBrowserPanel>();
            }

            [[nodiscard]] static bool readEventCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AudioEventBrowserPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getEventCount() );
                return true;
            }

            [[nodiscard]] static bool readSelected( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AudioEventBrowserPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = pPanel->hasSelectedEvent() ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readEditCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AudioEventBrowserPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getEditEventCount() );
                return true;
            }

            /** @brief 오디오 스레드가 마지막으로 게시한 받아들인 이벤트 수입니다. 오디오 서비스가 없거나 시작하지 않았으면 값을 내지 않는다. */
            [[nodiscard]] static bool readPlayedCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                AudioEngineStats stats;
                if ( AudioEventBrowserUtil::readStats( stats ) == false )
                    return false;
                outValue = static_cast<float64>( stats._playedEventCount );
                return true;
            }

            /** @brief `Editor.AudioEventsPlayed` 의 활동 수 — 오디오 스레드가 지금까지 렌더한 프레임 수입니다(게시가 멈추면 값도 멈춘다). */
            [[nodiscard]] static uint64 readAudioActivity( const GameObjectManager* /*pManager*/ )
            {
                AudioEngineStats stats;
                return AudioEventBrowserUtil::readStats( stats ) ? stats._renderedFrameCount : 0u;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    AudioEventBrowserPanel::AudioEventBrowserPanel()
        : EditorDocumentPanel( EditorAssetType{ EditorAssetTypeIDs::kData }, false )
        , _edit{ getDocumentEditRecorder() }
        , _grid{}
        , _libraryGrid{}
        , _listEventName{}
        , _listVisibleEvent{}
        , _listFieldRow{}
        , _listInstanceStatus{}
        , _listInstanceRow{}
        , _selectedEvent{}
        , _selectedLibrary{}
        , _filter{}
        , _bOnlySelectedEvent{ false }
        , _bVisibleEventStale{ SW_TRUE }
        , _pEventNameEngine{ nullptr }
        , _eventNameGeneration{ 0 }
        , _visibleEventFilter{}
    {
    }

    void AudioEventBrowserPanel::onEventEdited( const PropertyInfo& /*prop*/ )
    {
        _edit.markPropertyEdited();
    }

    string AudioEventBrowserPanel::captureDocumentText() const
    {
        return _edit.captureText();
    }

    void AudioEventBrowserPanel::applyDocumentText( string_view text )
    {
        (void)_edit.applyText( text ); // 스스로 만든 글이다 — 읽지 못하면 사본은 그대로이고 읽기가 이유를 남겼다
    }

    bool AudioEventBrowserPanel::canAcceptDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, AudioEventLibrary::kExtension, true );
    }

    ToolAssetLoadResult AudioEventBrowserPanel::loadDocument()
    {
        const string libraryPath = acceptDocumentToLoad();
        if ( libraryPath.empty() )
            return ToolAssetLoadResult::Missing; // 아직 고치는 라이브러리가 없다
        const hashed_string library{ libraryPath.c_str() };
        const hashed_string eventName = _edit.isEditing( library ) ? _edit.getEventName() : _selectedEvent; // 다시 읽기는 고치던 이벤트를, 새로 열기는 목록에서 고른 이벤트를 고친다
        return _edit.begin( library, eventName ) ? ToolAssetLoadResult::Loaded : ToolAssetLoadResult::Malformed;
    }

    bool AudioEventBrowserPanel::saveDocument( AssetWriter& writer )
    {
        return _edit.saveDocument( writer );
    }

    void AudioEventBrowserPanel::drawLibraryEditor()
    {
        ImGui::BeginDisabled( isDocumentDirty() == false );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioEvents", "SaveEdits" ) ) )
            (void)saveDocumentAndClearDirty(); // 저장 경로는 하나다 — 실패는 saveToAsset 가 오류로 남기고 사본은 그대로다
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "audioevents.save" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAudioEvents", "SaveEditsTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorAudioEvents", "StopEditing" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioEvents", "StopEditing" ) ) )
        {
            _edit.end();
            clearDocumentDirty();   // 사본을 버렸다 — 쓸 편집이 없다
            resetDocumentHistory(); // 남은 단계는 버린 사본의 글이다
            return;
        }
        EditorSelfTestMarks::record( "audioevents.stopEditing" );
        if ( isDocumentDirty() )
        {
            ImGui::SameLine();
            EditorThemeUtil::textMuted( SW_LOCKEY( "EditorAudioEvents", "Unsaved" ) );
        }

        // 고칠 이벤트 — 사본의 이벤트에서 고른다(복제한 새 이벤트는 저장 전에는 엔진 목록에 없다).
        if ( EditorWidgets::beginCombo( SW_LOCKEY( "EditorAudioEvents", "EditedEvent" ), _edit.getEventName().c_str() ) )
        {
            for ( const AudioEventDef& candidate : _edit.getLibrary()._listEvent )
            {
                if ( ImGui::Selectable( candidate._name.c_str(), candidate._name == _edit.getEventName() ) )
                    _edit.selectEvent( candidate._name );
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::record( "audioevents.editedEvent" );
        AudioEventDef* pEvent = _edit.findEvent();
        ImGui::BeginDisabled( pEvent == nullptr );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioEvents", "DuplicateEvent" ) ) )
        {
            _edit.duplicateEvent();
            pEvent = _edit.findEvent(); // 목록이 다시 잡혔을 수 있다
        }
        EditorSelfTestMarks::record( "audioevents.duplicate" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorAudioEvents", "DeleteEvent" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioEvents", "DeleteEvent" ) ) && _edit.removeEvent() )
            pEvent = _edit.findEvent();
        EditorSelfTestMarks::record( "audioevents.delete" );
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAudioEvents", "DeleteEventTip" ) );

        const Delegate<void( const PropertyInfo& )> onEdited = SW_DELEGATE_METHOD( Delegate<void( const PropertyInfo& )>, &AudioEventBrowserPanel::onEventEdited, this );
        if ( pEvent != nullptr )
        {
            EditorPropertyGridTarget target{};
            target._pInstance = pEvent;
            target._pType     = AudioEventDef::StaticType();
            target._onEdited  = onEdited;
            _grid.drawProperties( target, nullptr, {} );
        }

        // 라이브러리의 파라미터 · 감쇠 프리셋 — 이벤트 목록은 위에서 고르고 고치므로 이 그리드에서 뺀다.
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EditorAudioEvents", "LibrarySettings" ) ) )
        {
            EditorPropertyGridTarget target{};
            target._pInstance = &_edit.editLibrary();
            target._pType     = AudioEventLibrary::StaticType();
            target._onEdited  = onEdited;
            ImGui::PushID( "##AudioLibraryGrid" );
            _libraryGrid.drawProperties( target, nullptr, { hashed_string( "_listEvent" ) } );
            ImGui::PopID();
        }
        EditorSelfTestMarks::record( "audioevents.librarySettings" );
    }

    void AudioEventBrowserPanel::drawEventList()
    {
        (void)EditorWidgets::drawSearchField( "##AudioEventFilter", _filter, nullptr, -1.0f );
        // 이름 목록이나 검색 글이 바뀐 프레임에만 거르고 정렬한다.
        if ( _bVisibleEventStale || _visibleEventFilter != _filter.c_str() )
        {
            AudioEventBrowserUtil::filterEventNames( _listEventName, string_view{ _filter.c_str() }, _listVisibleEvent );
            _visibleEventFilter = _filter.c_str();
            _bVisibleEventStale = SW_FALSE;
        }
        if ( _listEventName.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAudioEvents", "NoLibrary" ) );
            return;
        }
        if ( _listVisibleEvent.empty() )
        {
            EditorWidgets::drawNoSearchResultHint( string_view{ _filter.c_str() } );
            return;
        }
        for ( const hashed_string& name : _listVisibleEvent )
        {
            if ( ImGui::Selectable( name.c_str(), name == _selectedEvent ) )
                _selectedEvent = name;
            EditorSelfTestMarks::recordFormat( "audioevents.event.%#", name.c_str() ); // 시나리오가 이벤트를 고른다
        }
        EditorSelfTestMarks::record( "audioevents.list" );
    }

    void AudioEventBrowserPanel::drawEventDetail()
    {
        if ( _selectedEvent.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAudioEvents", "SelectEvent" ) );
            return;
        }
        AudioEventDef       eventDesc;
        AudioAttenuationDef attenuation;
        if ( AudioEventBrowserUtil::findEventDesc( _selectedEvent, eventDesc, _selectedLibrary, attenuation ) == false )
        {
            // 라이브러리가 언로드됐거나 다시 읽혀 이벤트가 사라졌다.
            _selectedEvent   = hashed_string{};
            _selectedLibrary = hashed_string{};
            return;
        }

        ImGui::TextUnformatted( _selectedEvent.c_str() );
        ImGui::TextDisabled( "%s", _selectedLibrary.c_str() );

        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Play" ), SW_LOCKEY( "EditorAudioEvents", "PostEvent" ) ) ) )
            AudioEventBrowserUtil::postEvent( _selectedEvent );
        EditorSelfTestMarks::record( "audioevents.postEvent" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorAudioEvents", "OpenDataFile" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioEvents", "OpenDataFile" ) ) )
        {
            // 데이터 파일을 직접 연다(고치기 창 밖의 편집 — 예: 새 클립 경로를 여러 개 붙여 넣기). 여는 도구가 없으면 경로를 클립보드에 둔다.
            if ( AudioEventBrowserUtil::openDataFile( _selectedLibrary.view() ) == false )
                ImGui::SetClipboardText( _selectedLibrary.c_str() );
        }
        EditorSelfTestMarks::record( "audioevents.openDataFile" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAudioEvents", "OpenDataFileTip" ) );

        // 고치기 — 같은 파일의 사본이 있으면 그 사본을 고친다(고칠 이벤트는 사본에서 고른다).
        if ( _edit.isEditing( _selectedLibrary ) )
        {
            drawLibraryEditor();
            return;
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorAudioEvents", "EditEvent" ) ) );
        ImGui::BeginDisabled( isDocumentDirty() ); // 다른 파일을 고치는 중에 저장하지 않은 것이 있으면 바꾸지 않는다
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioEvents", "EditEvent" ) ) )
        {
            // 콘텐츠 브라우저와 같은 문서 열기 길이다. "고치기 그만" 뒤의 같은 라이브러리는 이미 열린 문서라 포커스가 바뀌지 않으므로 다시 읽는다.
            if ( getLoadedAssetPath() == _selectedLibrary.view() )
                reloadDocument();
            else
                requestOpenDocument( _selectedLibrary.view() );
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "audioevents.edit" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAudioEvents", "EditEventTip" ) );

        AudioEventBrowserUtil::makeEventFieldRows( eventDesc, attenuation, _listFieldRow );
        if ( ImGui::BeginTable( "##AudioEventParams", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV ) )
        {
            ImGui::TableSetupColumn( SW_LOCKEY( "EditorAudioEvents", "ColumnField" ), ImGuiTableColumnFlags_WidthFixed, 140.0f * EditorThemeUtil::getDPIScale() );
            ImGui::TableSetupColumn( SW_LOCKEY( "EditorAudioEvents", "ColumnValue" ), ImGuiTableColumnFlags_WidthStretch );
            for ( const AudioEventFieldRow& row : _listFieldRow )
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                ImGui::TextUnformatted( row._pLabel );
                ImGui::TableSetColumnIndex( 1 );
                ImGui::TextUnformatted( row._value.c_str() );
            }
            ImGui::EndTable();
        }
        if ( eventDesc._listClip.empty() == false && ImGui::TreeNode( SW_LOCKEY( "EditorAudioEvents", "Clips" ) ) )
        {
            for ( const AudioClipEntry& clip : eventDesc._listClip )
            {
                ImGui::BulletText( "%s  (w %.2f, %.1f dB)", clip._path.c_str(), static_cast<float64>( clip._weight ), static_cast<float64>( clip._volumeDb ) );
            }
            ImGui::TreePop();
        }
    }

    void AudioEventBrowserPanel::drawPlayingInstances()
    {
        (void)ImGui::Checkbox( SW_LOCKEY( "EditorAudioEvents", "OnlySelected" ), &_bOnlySelectedEvent );
        const hashed_string eventFilter = _bOnlySelectedEvent ? _selectedEvent : hashed_string{};
        AudioEventBrowserUtil::makeInstanceRows( _listInstanceStatus, eventFilter, _listInstanceRow );
        if ( _listInstanceRow.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAudioEvents", "NothingPlaying" ) );
            return;
        }
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;
        if ( ImGui::BeginTable( "##AudioEventInstances", 5, flags ) )
        {
            ImGui::TableSetupScrollFreeze( 0, 1 );
            ImGui::TableSetupColumn( SW_LOCKEY( "EditorAudioEvents", "ColumnEvent" ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EditorAudioEvents", "ColumnBus" ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EditorAudioEvents", "ColumnElapsed" ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EditorAudioEvents", "ColumnRemaining" ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EditorAudioEvents", "ColumnVoices" ) );
            ImGui::TableHeadersRow();
            for ( const AudioInstanceStatus& row : _listInstanceRow )
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                if ( row._bStopping )
                    ImGui::TextDisabled( "%s", row._event.c_str() );
                else
                    ImGui::TextUnformatted( row._event.c_str() );
                ImGui::TableSetColumnIndex( 1 );
                ImGui::TextUnformatted( row._bus.c_str() );
                ImGui::TableSetColumnIndex( 2 );
                ImGui::Text( "%.2f s", static_cast<float64>( row._elapsedSeconds ) );
                ImGui::TableSetColumnIndex( 3 );
                ImGui::TextUnformatted( AudioEventBrowserUtil::formatRemainingSeconds( row._remainingSeconds ).c_str() );
                ImGui::TableSetColumnIndex( 4 );
                ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "Virtual", TextArgumentList().addInteger( "voiceCount", static_cast<int64>( row._voiceCount ) ).addInteger( "virtualVoiceCount", static_cast<int64>( row._virtualVoiceCount ) ) ).c_str() );
            }
            ImGui::EndTable();
        }
    }

    void AudioEventBrowserPanel::drawVoiceProfiler( const AudioEngineStats& stats )
    {
        using Internal                  = AudioEventBrowserPanelInternal;
        const AudioVoiceProfile profile = AudioEventBrowserUtil::makeVoiceProfile( stats );
        Internal::drawUsageBar( SW_LOCKEY( "EditorAudioEvents", "RealVoices" ), profile._realVoiceCount, profile._maxRealVoiceCount, profile._realUsage, profile._bRealLimitReached );
        Internal::drawUsageBar( SW_LOCKEY( "EditorAudioEvents", "AllVoices" ), profile._voiceCount, profile._maxVoiceCount, profile._voiceUsage, profile._bVoiceLimitReached );
        ImGui::Text( "%s: %u", SW_LOCKEY( "EditorAudioEvents", "VirtualVoices" ), profile._virtualVoiceCount );
        ImGui::Text( "%s: %u", SW_LOCKEY( "EditorAudioEvents", "PeakRealVoices" ), profile._peakRealVoiceCount );
        ImGui::Text( "%s: %u", SW_LOCKEY( "EditorAudioEvents", "Instances" ), profile._instanceCount );
        ImGui::Text( "%s: %llu / %llu", SW_LOCKEY( "EditorAudioEvents", "PlayedDropped" ), static_cast<unsigned long long>( profile._playedEventCount ),
                     static_cast<unsigned long long>( profile._droppedEventCount ) ); // %llu 는 unsigned long long 이다 — uint64 는 Linux 에서 unsigned long 이다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAudioEvents", "PlayedDroppedTip" ) );
        // 이 창이 읽는 사본(인스턴스 × 보이스)을 오디오 스레드가 블록마다 만드는 비용이다(E6 측정).
        const float64 nanosecondsPerMicrosecond = static_cast<float64>( constant::time::kNanosecondsPerMicrosecond );
        ImGui::Text( "%s: %.1f / %.1f us", SW_LOCKEY( "EditorAudioEvents", "PublishCost" ), static_cast<float64>( stats._publishNanoseconds ) / nanosecondsPerMicrosecond,
                     static_cast<float64>( stats._peakPublishNanoseconds ) / nanosecondsPerMicrosecond );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAudioEvents", "PublishCostTip" ) );
    }

    void AudioEventBrowserPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        // 서비스 · 엔진은 Model(`AudioEventBrowserUtil`)이 찾는다 — 이 창은 값과 명령만 본다.
        AudioEngineStats stats;
        if ( AudioEventBrowserUtil::readStats( stats ) == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAudioEvents", "NoService" ) );
            return;
        }
        if ( AudioEventBrowserUtil::collectEventNamesIfChanged( _pEventNameEngine, _eventNameGeneration, _listEventName ) )
            _bVisibleEventStale = SW_TRUE;
        AudioEventBrowserUtil::collectInstanceStatuses( _listInstanceStatus );

        ImGui::BeginChild( "##AudioEventList", EditorChrome::measureTextExtent( EditorTextExtent::ofText( 29.0f, 0.0f ) ), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX );
        drawEventList();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild( "##AudioEventRight", ImVec2{ 0.0f, 0.0f } );
        if ( ImGui::BeginTabBar( "##AudioEventTabs" ) )
        {
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EditorAudioEvents", "TabProperties" ) ) )
            {
                drawEventDetail();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EditorAudioEvents", "TabPlaying" ) ) )
            {
                drawPlayingInstances();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EditorAudioEvents", "TabVoices" ) ) )
            {
                drawVoiceProfiler( stats );
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
    }

    SW_EDITOR_PANEL( AudioEventBrowserPanel, AudioEventBrowserPanel::kPanelID, EditorPanelCategory::Tool, 2101 );
    SW_AUTOMATION_WATCH( editorAudioEventCount, "Editor.AudioEventCount", "Audio events the Audio Events window lists (fails while it is closed)",
                         &AudioEventBrowserPanelInternal::readEventCount );
    SW_AUTOMATION_WATCH( editorAudioEventSelected, "Editor.AudioEventSelected", "1 while the Audio Events window has an event picked", &AudioEventBrowserPanelInternal::readSelected );
    SW_AUTOMATION_WATCH( editorAudioEventEditCount, "Editor.AudioEventEditCount", "Events in the library copy the Audio Events window edits (0 while not editing)",
                         &AudioEventBrowserPanelInternal::readEditCount );
    SW_AUTOMATION_WALL_WATCH( editorAudioEventsPlayed, "Editor.AudioEventsPlayed", "Audio events the audio thread admitted, as it last published (follows the audio thread)",
                              &AudioEventBrowserPanelInternal::readPlayedCount, &AudioEventBrowserPanelInternal::readAudioActivity );
    SW_EDITOR_PANEL_OPEN_COMMAND( AudioEventBrowserOpen, "window.audioEvents", AudioEventBrowserPanel );
} // namespace sw::editor
