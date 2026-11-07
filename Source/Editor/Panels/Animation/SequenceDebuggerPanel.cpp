#include "pch.h"

#include "Editor/Panels/Animation/SequenceDebuggerPanel.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequenceDiagnostics.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct SequenceDebuggerPanelInternal
        {
            /** @brief 활성 씬의 시퀀스 플레이어를 모두 채웁니다(플레이 중이면 플레이 월드). */
            static void collectPlayers( vector<SequencePlayerComponent*>& outListPlayer )
            {
                outListPlayer.clear();
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( pManager == nullptr )
                    return;
                for ( GameObject* pObject : pManager->getAllGameObjects() )
                {
                    SequencePlayerComponent* pPlayer = pObject != nullptr ? pObject->getComponent<SequencePlayerComponent>() : nullptr;
                    if ( pPlayer != nullptr )
                        outListPlayer.push_back( pPlayer );
                }
            }

            static const utf8* getHistoryKindLabel( SequenceHistoryKind kind )
            {
                switch ( kind )
                {
                    case SequenceHistoryKind::Started:
                        return "Started";
                    case SequenceHistoryKind::Finished:
                        return "Finished";
                    case SequenceHistoryKind::Stopped:
                        return "Stopped";
                    case SequenceHistoryKind::Event:
                        return "Event";
                    case SequenceHistoryKind::CameraSignal:
                        return "Camera";
                    case SequenceHistoryKind::UnboundBinding:
                        return "Unbound";
                }
                return "?";
            }

            static const utf8* getSignalLabel( uint8 detail )
            {
                switch ( static_cast<SequenceCameraSignalKind>( detail ) )
                {
                    case SequenceCameraSignalKind::CutBegin:
                        return "cut";
                    case SequenceCameraSignalKind::CinematicBegin:
                        return "blend in";
                    case SequenceCameraSignalKind::CinematicEnd:
                        return "blend out";
                    case SequenceCameraSignalKind::CutPrewarm:
                        return "standby view";
                    case SequenceCameraSignalKind::Shake:
                        return "shake";
                }
                return "?";
            }
        };
    } // namespace

    SW_EDITOR_PANEL( SequenceDebuggerPanel, SequenceDebuggerPanel::kPanelID, EditorPanelCategory::Tool, 1005 );
    SequenceDebuggerPanel::SequenceDebuggerPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _listProvider{}
        , _providerText{}
        , _watched{}
    {
    }

    SequencePlayerComponent* SequenceDebuggerPanel::findWatchedPlayer() const
    {
        vector<SequencePlayerComponent*> listPlayer;
        SequenceDebuggerPanelInternal::collectPlayers( listPlayer );
        for ( SequencePlayerComponent* pPlayer : listPlayer )
        {
            if ( pPlayer->getOwner() != nullptr && pPlayer->getOwner()->getHandle() == _watched )
                return pPlayer;
        }
        return listPlayer.empty() ? nullptr : listPlayer[0];
    }

    void SequenceDebuggerPanel::drawContent()
    {
        vector<SequencePlayerComponent*> listPlayer;
        SequenceDebuggerPanelInternal::collectPlayers( listPlayer );
        if ( listPlayer.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSequenceDebugger", "NoPlayersHint" ) );
            return;
        }
        SequencePlayerComponent* pWatched = findWatchedPlayer();
        drawPlayerPicker( listPlayer, pWatched );
        if ( pWatched == nullptr )
            return;

        // 플레이헤드: 프레임 · 범위 · 상태(재생 · 정지)입니다. 읽기 전용이다 — 고치려면 시퀀서 패널을 쓴다.
        const SequenceAsset& asset = pWatched->getAsset();
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EditorSequenceDebugger", "PlayheadFormat",
                                                 TextArgumentList()
                                                     .addInteger( "frame", pWatched->getCurrentFrame() )
                                                     .addInteger( "frameMin", asset._frameMin )
                                                     .addInteger( "frameMax", asset._frameMax )
                                                     .addText( "state", pWatched->isPlaying() ? "playing" : "stopped" ) )
                                    .c_str() );
        const float32 span = static_cast<float32>( MathUtil::max( asset._frameMax - asset._frameMin, 1 ) );
        ImGui::ProgressBar( static_cast<float32>( pWatched->getCurrentFrame() - asset._frameMin ) / span, ImVec2( -1.0f, 0.0f ), "" );

        ImGui::SeparatorText( SW_LOCKEY( "EditorSequenceDebugger", "ActiveSection" ) );
        vector<const SequenceTrackItem*> listActive;
        asset.collectActiveItems( pWatched->getCurrentFrame(), listActive );
        if ( listActive.empty() )
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSequenceDebugger", "NoActiveItemsHint" ) );
        for ( const SequenceTrackItem* pItem : listActive )
        {
            const SequenceBinding* pBinding = asset.findBinding( pItem->_binding );
            ImGui::BulletText( "%s", formatToString( "%# (%#) %#..%#", pItem->_name, pBinding != nullptr ? pBinding->_name.c_str() : "-", pItem->_start, pItem->_end ).c_str() );
        }

        drawBindingTable( *pWatched );
        drawHistoryTable( *pWatched );
        drawProviders( *pWatched );
    }

    void SequenceDebuggerPanel::drawPlayerPicker( const vector<SequencePlayerComponent*>& listPlayer, const SequencePlayerComponent* pWatched )
    {
        const utf8* pPreview = pWatched != nullptr && pWatched->getOwner() != nullptr ? pWatched->getOwner()->getName().c_str() : "-";
        ImGui::SetNextItemWidth( -1.0f );
        if ( EditorWidgets::beginCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EditorSequenceDebugger", "PlayerLabel" ), "Player" ), pPreview ) )
        {
            for ( const SequencePlayerComponent* pPlayer : listPlayer )
            {
                const GameObject* pOwner = pPlayer->getOwner();
                if ( pOwner == nullptr )
                    continue;
                ImGui::PushID( static_cast<int32>( pOwner->getObjectID() ) );
                if ( ImGui::Selectable( pOwner->getName().c_str(), pPlayer == pWatched ) )
                    _watched = pOwner->getHandle();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::record( "sequencedebugger.player" );
    }

    void SequenceDebuggerPanel::drawBindingTable( const SequencePlayerComponent& player )
    {
        ImGui::SeparatorText( SW_LOCKEY( "EditorSequenceDebugger", "BindingsSection" ) );
        const SequenceAsset& asset = player.getAsset();
        if ( asset._listBinding.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSequenceDebugger", "NoBindingsHint" ) );
            return;
        }
        if ( ImGui::BeginTable( "##sequence_bindings", 3, ImGuiTableFlags_SizingStretchProp ) == false )
            return;
        ImGui::TableSetupColumn( "Id" );
        ImGui::TableSetupColumn( "Binding" );
        ImGui::TableSetupColumn( "Object" );
        ImGui::TableHeadersRow();
        for ( const SequenceBinding& binding : asset._listBinding )
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text( "%u", binding._id );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( binding._name.c_str() );
            ImGui::TableNextColumn();
            const GameObject* pBound = player.findBoundObject( binding._id );
            if ( pBound != nullptr )
                ImGui::TextUnformatted( pBound->getName().c_str() );
            else if ( asset.findSpawnItem( binding._id ) != nullptr )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorSequenceDebugger", "NotSpawnedYet" ) );
            else
                ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", SW_LOCKEY( "EditorSequenceDebugger", "Unbound" ) );
        }
        ImGui::EndTable();
    }

    void SequenceDebuggerPanel::drawHistoryTable( const SequencePlayerComponent& player )
    {
        ImGui::SeparatorText( SW_LOCKEY( "EditorSequenceDebugger", "HistorySection" ) );
        const SequencePlaybackHistory& history = player.getHistory();
        if ( history.getCount() == 0 )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSequenceDebugger", "EmptyHistoryHint" ) );
            return;
        }
        if ( ImGui::BeginTable( "##sequence_history", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                                EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, 10.0f ) ) ) == false )
            return;
        ImGui::TableSetupColumn( "Frame" );
        ImGui::TableSetupColumn( "Kind" );
        ImGui::TableSetupColumn( "What" );
        ImGui::TableHeadersRow();
        // 최근 줄이 위다.
        for ( uint32 index = history.getCount(); index > 0; --index )
        {
            const SequenceHistoryEntry& entry = history.getEntry( index - 1 );
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text( "%d", entry._frame );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( SequenceDebuggerPanelInternal::getHistoryKindLabel( entry._kind ) );
            ImGui::TableNextColumn();
            if ( entry._kind == SequenceHistoryKind::CameraSignal )
                ImGui::Text( "%s - %s", entry._name.c_str(), SequenceDebuggerPanelInternal::getSignalLabel( entry._detail ) );
            else
                ImGui::TextUnformatted( entry._name.c_str() );
        }
        ImGui::EndTable();
        EditorSelfTestMarks::record( "sequencedebugger.history" );
    }

    void SequenceDebuggerPanel::drawProviders( const SequencePlayerComponent& player )
    {
        // 키트의 해석 — 그 키트 모듈이 올라와 있을 때만 제공자가 목록에 있다.
        SequenceDiagnosticRegistry::collectProviders( _listProvider );
        for ( const SequenceDiagnosticRegistration* pProvider : _listProvider )
        {
            _providerText.clear();
            if ( pProvider->_pDescribe( player, _providerText ) == false )
                continue;
            ImGui::SeparatorText( pProvider->_pName );
            ImGui::TextWrapped( "%s", _providerText.c_str() );
        }
    }
} // namespace sw::editor
