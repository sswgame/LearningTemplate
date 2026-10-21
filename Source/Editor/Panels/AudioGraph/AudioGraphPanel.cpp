#include "pch.h"

#include "Editor/Panels/AudioGraph/AudioGraphPanel.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/Graph/AudioGraph.h"
#include "Engine/Audio/IAudioService.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 창을 엽니다(명령 표의 실행 함수). */
        void openAudioGraphPanel()
        {
            EditorContext* pContext = EditorContext::get();
            if ( pContext != nullptr )
                (void)pContext->getSubsystem<EditorPanelManager>().setPanelOpen<AudioGraphPanel>( true ); // 패널은 이 파일에 등록되어 있다
        }
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    AudioGraphPanel::AudioGraphPanel()
        : IEditorPanel( false )
        , _editGraph{}
        , _grid{}
        , _listOrder{}
        , _editPath{}
        , _previewID{ 0 }
        , _pathInput{}
        , _bEditDirty{ false }
    {
    }

    void AudioGraphPanel::loadGraph()
    {
        const string_view path{ _pathInput.c_str() };
        if ( _editGraph.loadFromResource( path ) == false )
        {
            _editGraph = AudioGraphDef{};
            _editPath  = hashed_string{};
            return;
        }
        _editPath   = hashed_string( path );
        _bEditDirty = false;
    }

    void AudioGraphPanel::onGraphEdited( const PropertyInfo& /*prop*/ )
    {
        _bEditDirty = true;
    }

    void AudioGraphPanel::drawToolbar()
    {
        ImGui::BeginDisabled( _bEditDirty == false );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "Save" ) ) )
        {
            // 검사를 통과하지 못하면 쓰지 않고 오류를 남긴다(사본은 남아 계속 고칠 수 있다).
            if ( _editGraph.saveToResource( _editPath.view() ) )
                _bEditDirty = false;
        }
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAudioGraph", "SaveTip" ) );

        IAudioService* pAudio      = editor::findSubsystem<IAudioService>();
        const bool     bAudioReady = pAudio != nullptr && pAudio->isInitialized();
        ImGui::SameLine();
        ImGui::BeginDisabled( bAudioReady == false || _bEditDirty );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "Preview" ) ) )
        {
            AudioEngine& engine = pAudio->getEngine();
            if ( _previewID != 0 )
                engine.stop( _previewID, 0.0f );
            _previewID = engine.playClip( _editPath, hashed_string( AudioBusNames::kUI ), AudioClipPlayParams{} );
        }
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAudioGraph", "PreviewTip" ) );
        ImGui::SameLine();
        ImGui::BeginDisabled( bAudioReady == false || _previewID == 0 );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "StopPreview" ) ) )
        {
            pAudio->getEngine().stop( _previewID, 0.05f );
            _previewID = 0;
        }
        ImGui::EndDisabled();
        if ( _bEditDirty )
        {
            ImGui::SameLine();
            EditorThemeUtil::textMuted( SW_LOCKEY( "EditorAudioGraph", "Unsaved" ) );
        }
    }

    void AudioGraphPanel::drawProcessingOrder()
    {
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EditorAudioGraph", "ProcessingOrder" ), ImGuiTreeNodeFlags_DefaultOpen ) == false )
            return;
        if ( _editGraph.makeProcessingOrder( _listOrder ) == false )
        {
            EditorThemeUtil::textMuted( SW_LOCKEY( "EditorAudioGraph", "BrokenLinks" ) );
            return;
        }
        for ( const uint32 nodeIndex : _listOrder )
        {
            const AudioGraphNodeDesc& node    = _editGraph._listNode[nodeIndex];
            const bool                bOutput = node._name == _editGraph._output;
            const utf8*               pType   = AudioGraphRegistry::getNodeType( node._type )._pName;
            string                    inputs;
            for ( const hashed_string& input : node._listInput )
            {
                if ( inputs.empty() == false )
                    inputs.append( ", " );
                inputs.append( input.view() );
            }
            ImGui::Text( "%s%s (%s)%s%s", bOutput ? "> " : "  ", node._name.c_str(), pType, inputs.empty() ? "" : " <- ", inputs.c_str() );
        }
    }

    void AudioGraphPanel::drawContent()
    {
        ImGui::SetNextItemWidth( -120.0f * EditorThemeUtil::getDpiScale() );
        (void)ImGui::InputTextWithHint( "##AudioGraphPath", SW_LOCKEY( "EditorAudioGraph", "PathHint" ), _pathInput.data(), _pathInput.capacity() );
        ImGui::SameLine();
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "Open" ) ) )
            loadGraph();
        if ( _editPath.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAudioGraph", "NoGraph" ) );
            return;
        }
        drawToolbar();
        drawProcessingOrder();

        EditorPropertyGridTarget target{};
        target._pInstance = &_editGraph;
        target._pType     = AudioGraphDef::StaticType();
        target._onEdited  = SW_DELEGATE_METHOD( Delegate<void( const PropertyInfo& )>, &AudioGraphPanel::onGraphEdited, this );
        _grid.drawProperties( target, nullptr, {} );
    }

    SW_EDITOR_PANEL( AudioGraphPanel, AudioGraphPanel::kPanelID, EditorPanelCategory::Tool, 2102 );
    SW_EDITOR_COMMAND( AudioGraphOpen, "window.audioGraph", 2232, "Audio Graph", editoricon::kSettings, "Audio", "소리 그래프 창을 엽니다(노드 · 연결 · 값 편집, 저장, 미리 듣기)",
                       "Open the audio graph editor", {}, &openAudioGraphPanel, nullptr, "MainMenu/Window" );
} // namespace sw::editor
