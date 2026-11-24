/**
 * @file CollisionResponsePanel.cpp
 * @brief 충돌 응답 창(물리 설정 `physicssettings.xml`)입니다 — 프로필 × 채널 응답 격자를 보고 칸을 눌러 막음 · 겹침 · 무시를 돌리며, 그 결과인 프로필 쌍 응답 표를 봅니다.
 * @details 언리얼 Project Settings > Collision(프리셋 · 채널 표) 자리다. 상태와 명령은 `CollisionResponseEditModel`(ImGui 없음)이 들고, 되돌리기는 문서 패널의 원문 단계다.
 *          연 문서가 없으면 프로젝트의 물리 설정(`PhysicsSystem::kSettingsPath`)을 연다 — 이 표는 프로젝트에 하나다. 채널 · 프로필을 더하고 지우는 일은 카탈로그 편집기다.
 */
#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputActionNames.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeID.h"
#include "Editor/Panels/Physics/Model/CollisionResponseEditModel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Physics/PhysicsSystem.h"

#include <imgui.h>

namespace sw::editor
{
    /** @class CollisionResponsePanel @brief 파일 머리말 참고. */
    class CollisionResponsePanel : public EditorDocumentPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "collision_responses";

        CollisionResponsePanel();

        const utf8*        getPanelTitle() const override { return "Collision Responses"; }
        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override;
        bool               canAcceptDocumentPath( string_view assetPath ) const override;

        const CollisionResponseEditModel& getModel() const { return _model; }

    private:
        ToolAssetLoadResult loadDocument() override;
        string              captureDocumentText() const override { return _model.captureText(); }
        void                applyDocumentText( string_view text ) override { _model.applyText( text ); }

        /** @brief 프로필 × 채널 격자입니다. 칸을 누르면 다음 응답으로 돌린다. 채널 기본을 쓰는 칸은 흐리게 보인다. */
        void drawResponseGrid();
        /** @brief 프로필 쌍의 결과 응답(약한 쪽)입니다 — 읽기만 한다. */
        void drawPairTable();

    private:
        CollisionResponseEditModel _model;
        uint8                      _bOpenRequested : 1; ///< 연 문서가 없어 프로젝트 물리 설정을 한 번 열었다
        [[maybe_unused]] uint8     _reserved       : 7;
    };

    namespace
    {
        struct CollisionResponsePanelInternal
        {
            static constexpr int32 kMaxTableColumn  = 64; ///< ImGui 표 열의 상한 — 채널 · 프로필은 32 이하라 넘지 않는다
            static constexpr int32 kNameColumnCount = 1;
            static constexpr int32 kPanelOrder      = 1345; ///< Tool 메뉴 차례 — Ragdoll Preview(1340) 다음  ///< 표 맨 앞의 프로필 이름 열

            static const utf8* getResponseLabel( CollisionResponse response )
            {
                switch ( response )
                {
                    case CollisionResponse::Ignore:
                        return SW_LOCTEXT( "EditorCollision", "Ignore", "Ignore" );
                    case CollisionResponse::Overlap:
                        return SW_LOCTEXT( "EditorCollision", "Overlap", "Overlap" );
                    case CollisionResponse::Block:
                        return SW_LOCTEXT( "EditorCollision", "Block", "Block" );
                }
                return "";
            }
        };
    } // namespace

    SW_EDITOR_PANEL( CollisionResponsePanel, CollisionResponsePanel::kPanelID, EditorPanelCategory::Tool, CollisionResponsePanelInternal::kPanelOrder );

    CollisionResponsePanel::CollisionResponsePanel()
        : EditorDocumentPanel( EditorAssetType{ EditorAssetTypeIDs::kData }, false )
        , _model{}
        , _bOpenRequested{ SW_FALSE }
        , _reserved{ 0 }
    {
        _model.bindDocumentEditRecorder( getDocumentEditRecorder() ); // 문서 편집 알림은 모델이 낸다
    }

    bool CollisionResponsePanel::canAcceptDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, "physicssettings.xml", true );
    }

    ToolAssetLoadResult CollisionResponsePanel::loadDocument()
    {
        if ( acceptDocumentToLoad().empty() )
            return ToolAssetLoadResult::Missing; // 아직 고르지 않았다
        const optional<AbsolutePath> diskPath = getDocumentDiskPath();
        return diskPath.has_value() ? _model.loadDocument( *diskPath ) : ToolAssetLoadResult::Missing;
    }

    bool CollisionResponsePanel::saveDocument()
    {
        const optional<AbsolutePath> diskPath = getDocumentDiskPath();
        if ( diskPath.has_value() == false || _model.saveDocument( *diskPath ) == false )
            return false; // 까닭은 상태 줄에 있다
        syncDocumentUndoBaseline();
        return true;
    }

    void CollisionResponsePanel::drawDocumentContent()
    {
        updateFocusedDocument();
        // 이 표는 프로젝트에 하나다 — 고른 문서가 없으면 프로젝트의 물리 설정을 연다(다른 물리 설정 파일은 열기 막대로).
        if ( getLoadedAssetPath().empty() && getMatchingFocusedPath().empty() && _bOpenRequested == SW_FALSE )
        {
            _bOpenRequested = SW_TRUE;
            requestOpenDocument( PhysicsSystem::kSettingsPath );
        }
        ensureDocumentLoaded();
        drawDocumentOpenBar( "collisionResponses" );

        ImGui::BeginDisabled( isDocumentDirty() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) ) )
            (void)saveDocumentAndClearDirty(); // 실패하면 상태 줄에 까닭이 있다
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "collisionResponses.save" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Reload" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Reload" ) ) )
            reloadDocument();
        EditorSelfTestMarks::record( "collisionResponses.reload" );

        if ( isDocumentLoaded() )
        {
            drawResponseGrid();
            ImGui::Spacing();
            drawPairTable();
        }
        EditorWidgets::drawPanelStatus( _model.getStatus().c_str() );
    }

    void CollisionResponsePanel::drawResponseGrid()
    {
        using Internal                     = CollisionResponsePanelInternal;
        const PhysicsSettings& settings    = _model.getSettings();
        const int32            columnCount = static_cast<int32>( settings._listChannel.size() ) + Internal::kNameColumnCount;
        ImGui::TextUnformatted( SW_LOCTEXT( "EditorCollision", "ResponsesTitle", "Responses by profile and channel" ) );
        EditorWidgets::drawTooltip( SW_LOCTEXT( "EditorCollision", "ResponsesTip",
                                                "Click a cell to cycle Block, Overlap and Ignore. Two bodies use the weaker of the responses they give each other. Dim cells use the channel default." ) );
        constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX | ImGuiTableFlags_BordersInnerH;
        if ( columnCount > Internal::kMaxTableColumn || ImGui::BeginTable( "##CollisionResponses", columnCount, kFlags ) == false )
            return;
        ImGui::TableSetupColumn( SW_LOCTEXT( "EditorCollision", "Profile", "Profile" ) );
        for ( const CollisionChannelDef& channel : settings._listChannel )
        {
            ImGui::TableSetupColumn( channel._name.c_str() );
        }
        ImGui::TableHeadersRow();
        const ImVec4 mutedColor = EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "TextMuted" ) );
        for ( uint32 profileIndex = 0; profileIndex < settings._listProfile.size(); ++profileIndex )
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( settings._listProfile[profileIndex]._name.c_str() );
            for ( uint32 channelIndex = 0; channelIndex < settings._listChannel.size(); ++channelIndex )
            {
                ImGui::TableNextColumn();
                ImGui::PushID( static_cast<int32>( profileIndex * static_cast<uint32>( Internal::kMaxTableColumn ) + channelIndex ) );
                const bool bDefault = _model.isDefaultResponse( profileIndex, channelIndex ); // 채널 기본은 흐리게 — 적힌 줄만 진하다
                if ( bDefault )
                    ImGui::PushStyleColor( ImGuiCol_Text, mutedColor );
                const bool bClicked = ImGui::Selectable( Internal::getResponseLabel( _model.getResponse( profileIndex, channelIndex ) ) );
                const bool bReset   = bDefault == false && EditorInputGUI::wasItemResetRequested(); // R10 — 휠클릭은 채널 기본으로
                const bool bHovered = ImGui::IsItemHovered();
                EditorSelfTestMarks::recordFormat( "collisionResponses.cell.%#.%#", profileIndex, channelIndex );
                if ( bDefault )
                    ImGui::PopStyleColor();
                ImGui::PopID();
                if ( bClicked )
                    (void)_model.cycleResponse( profileIndex, channelIndex ); // 바꿨으면 모델이 되돌리기 이름으로 알린다
                else if ( bReset )
                    (void)_model.resetResponse( profileIndex, channelIndex );
                // R13 — 칸 위 Ctrl+C · Ctrl+V 는 응답 이름을 복사 · 붙인다. 응답으로 읽히지 않는 클립보드는 붙이지 않는다.
                if ( bHovered && EditorInputGUI::wasActionTriggered( editorinput::kPropertyCopyValue ) )
                    ImGui::SetClipboardText( CollisionResponseEditModel::formatResponse( _model.getResponse( profileIndex, channelIndex ) ) );
                CollisionResponse pasted     = CollisionResponse::Block;
                const utf8* const pClipboard = bHovered && EditorInputGUI::wasActionTriggered( editorinput::kPropertyPasteValue ) ? ImGui::GetClipboardText() : nullptr;
                if ( pClipboard != nullptr && CollisionResponseEditModel::parseResponse( pClipboard, pasted ) )
                    (void)_model.setResponse( profileIndex, channelIndex, pasted );
            }
        }
        ImGui::EndTable();
        EditorSelfTestMarks::record( "collisionResponses.grid" );
    }

    void CollisionResponsePanel::drawPairTable()
    {
        using Internal                  = CollisionResponsePanelInternal;
        const PhysicsSettings& settings = _model.getSettings();
        const CollisionLayers  layers   = settings.makeCollisionLayers();
        const int32            count    = static_cast<int32>( settings._listProfile.size() );
        const int32            columns  = count + Internal::kNameColumnCount;
        ImGui::TextUnformatted( SW_LOCTEXT( "EditorCollision", "PairsTitle", "Resulting response between profiles" ) );
        constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX | ImGuiTableFlags_BordersInnerH;
        if ( settings._listProfile.empty() || columns > Internal::kMaxTableColumn || ImGui::BeginTable( "##CollisionPairs", columns, kFlags ) == false )
            return;
        ImGui::TableSetupColumn( SW_LOCTEXT( "EditorCollision", "Profile", "Profile" ) );
        for ( const CollisionProfileDef& profile : settings._listProfile )
        {
            ImGui::TableSetupColumn( profile._name.c_str() );
        }
        ImGui::TableHeadersRow();
        for ( int32 row = 0; row < count; ++row )
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( settings._listProfile[static_cast<size_t>( row )]._name.c_str() );
            for ( int32 column = 0; column < count; ++column )
            {
                ImGui::TableNextColumn();
                const CollisionResponse response = layers.getResponse( static_cast<uint8>( row ), static_cast<uint8>( column ) );
                if ( response == CollisionResponse::Ignore )
                    EditorThemeUtil::textMuted( Internal::getResponseLabel( response ) );
                else
                    ImGui::TextUnformatted( Internal::getResponseLabel( response ) );
            }
        }
        ImGui::EndTable();
    }
} // namespace sw::editor
