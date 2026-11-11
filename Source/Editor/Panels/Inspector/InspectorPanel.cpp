#include "pch.h"

#include "Editor/Panels/Inspector/InspectorPanel.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/EditorCommandTarget.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorComponentIconRow.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorComponentMenu.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/TypeRegistry.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct InspectorPanelInternal
        {
            static constexpr Color kMultiSelectionText{ 0.4f, 0.7f, 1.0f, 1.0f };

            static InspectorPanel* findPanel()
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                return pPanels != nullptr ? pPanels->findPanel<InspectorPanel>() : nullptr;
            }

            /** @brief 프리셋 하위 메뉴는 인스펙터가 프리셋 목록 · 이름 칸을 들고 있어 그 창이 있을 때 켜진다. */
            static bool canShowPresets( const EditorCommandTarget& target )
            {
                return findPanel() != nullptr && InspectorViewModel::hasTargetComponent( target );
            }

            static void drawPresetsSubmenu( const EditorCommandTarget& target )
            {
                InspectorPanel* pPanel = findPanel();
                if ( pPanel != nullptr )
                    pPanel->drawComponentPresetItems( target );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( InspectorPanel, InspectorPanel::kPanelID, EditorPanelCategory::Core, 200 );
    // 컴포넌트 메뉴의 Presets 하위 메뉴(나머지 항목은 `EditorSceneObjectCommands.cpp`).
    SW_EDITOR_CONTEXT_COMMAND( ComponentPresets, "inspector.component.presets", nullptr, &InspectorPanelInternal::canShowPresets, NoScene, &InspectorPanelInternal::drawPresetsSubmenu );

    InspectorPanel::InspectorPanel()
        : _propertyGrid{}
        , _viewModel{}
        , _state{}
        , _nameEditBuffer{}
        , _listPreset{}
        , _nameEditObjectID{ 0 }
        , _addComponentSearch{}
        , _presetNameBuffer{}
    {
    }

    void InspectorPanel::drawContent()
    {
        _viewModel.gather( _state );
        _propertyGrid.setFavorites( &_viewModel.editFavorites() );
        EditorWidgets::pushInspectorStyle();
        drawSelectionSection();
        _propertyGrid.endFrame(); // 모두 펼치기 · 접기 요청은 이번 프레임의 머리에만 적용한다
        InspectorPropertyUndo::commitFinishedEdits();

        // Undo 와 Redo 단축키는 여기서 처리하지 않는다. edit.undo 와 edit.redo 커맨드가 유일한 처리자다.
        // ImGui 의 IsKeyPressed 는 소비되지 않으므로 여기서도 받으면 전역 처리기(EditorCommandGUI)와 함께
        // 두 번 되돌리게 된다.

        EditorWidgets::popInspectorStyle();
    }

    void InspectorPanel::drawSelectionSection()
    {
        // 머리 글과 자물쇠를 한 줄에 두고 그 아래에 구분선 — drawSectionHeader 는 구분선까지 그려 자물쇠가 다음 줄로 밀렸다(uiaudit 00).
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Selection" ) );
        // 잠그면 선택이 바뀌어도 현재 오브젝트를 계속 보여 준다. 유니티 Inspector 의 자물쇠에 해당하며, 언리얼에서는 Details 창을 여러 개 여는 것과 같은 용도다.
        ImGui::SameLine( ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() );
        const bool bLocked = _state._lockedObjectID != 0;
        if ( EditorWidgets::drawToggleIconButton( "##inspectorLock", bLocked, SW_EDITOR_ICON( "Lock" ), SW_EDITOR_ICON( "Unlock" ), SW_LOCKEY( "EditorInspector", "LockedToObject" ),
                                                  SW_LOCKEY( "EditorInspector", "FollowingSelection" ) ) )
            _viewModel.toggleLock();
        EditorSelfTestMarks::record( "inspector.lock" );
        ImGui::Separator();

        switch ( _state._content )
        {
            case InspectorContent::Preview:
            case InspectorContent::PreviewEmpty:
            {
                drawPreviewSelection();
                return;
            }
            case InspectorContent::Multi:
            {
                drawMultiSelection();
                return;
            }
            case InspectorContent::NothingSelected:
            {
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorInspector", "NothingSelected" ) );
                return;
            }
            case InspectorContent::NoActiveScene:
            {
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorInspector", "NoActiveScene" ) );
                return;
            }
            case InspectorContent::SelectionGone:
            {
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorInspector", "SelectionGone" ) );
                return;
            }
            case InspectorContent::Single:
            {
                break;
            }
        }

        drawPlayModeNotice();
        drawGameObjectHeader();

        ImGui::Spacing();
        ImGui::Separator();

        if ( _state._prefabPath.empty() == false )
            drawPrefabLinkSection();
        drawSceneAssemblySection();

        _propertyGrid.drawSearchBarWithOptions( SW_LOCKEY( "EditorInspector", "SearchTooltip" ) );
        ImGui::Spacing();
        drawFavoritesSection();

        if ( _state._bHasObjectType == SW_TRUE )
        {
            const EditorPropertyGridTarget objectTarget = EditorPropertyGridTarget::makeSceneTarget( EditorPropertyGridScope::MainScene, _state._objectID, 0 );
            _propertyGrid.drawProperties( objectTarget, "Reflected Properties", {} );
            ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Methods" ) );
            _propertyGrid.drawMethodsAndEvents( objectTarget, false );
        }

        drawComponentList();
    }

    void InspectorPanel::drawPlayModeNotice() const
    {
        // 플레이 중에도 값은 고칠 수 있다(유니티의 플레이 모드 편집). Stop 이 스냅샷으로 되돌리며, 남기려면 Keep Simulation Changes 를 쓴다.
        // 구조 편집(컴포넌트 추가, 프리팹)은 씬 명령이 플레이 중에 스스로 거절한다.
        if ( _state._bSceneEditsAllowed == SW_TRUE )
            return;
        EditorWidgets::drawChip( SW_LOCKEY( "EdWidget", "PlayMode" ), SW_THEME_COLOR( "Signal", "Warning" ) );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "EditsRevertOnStopPlayKeepSimulationChanges" ) );
    }

    void InspectorPanel::drawMultiSelection()
    {
        ImGui::TextColored( EditorColorUtil::toImVec4( InspectorPanelInternal::kMultiSelectionText ), "%s", SW_LOCKEYFORMAT( "EdFormat", "ObjectsSelected", TextArgumentList().addInteger( "objectCount", static_cast<int64>( _state._selectedCount ) ) ).c_str() );
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "Primary", TextArgumentList().addText( "name", _state._objectName.c_str() ) ).c_str() );

        drawPlayModeNotice();

        // 활성은 모든 선택에 적용한다. 값이 섞여 있으면 혼합으로 표시하고, 체크는 주 선택의 값을 따른다.
        bool bActive = _state._bActive == SW_TRUE;
        if ( _state._bActiveMixed == SW_TRUE )
        {
            ImGui::TextDisabled( "\xe2\x80\x94" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "MultipleValues" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "Active" ) ) );
        }
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Active" ), &bActive ) )
            _viewModel.setMultiActive( bActive );

        _propertyGrid.drawSearchBarWithOptions( SW_LOCKEY( "EditorInspector", "SearchTooltip" ) );
        drawFavoritesSection(); // 다중 선택의 즐겨찾기는 주 선택 오브젝트 기준이다
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Components" ) );
        for ( uint32 index = 0; index < static_cast<uint32>( _state._listComponent.size() ); ++index )
        {
            const InspectorComponentCard& card = _state._listComponent[index];
            ImGui::PushID( card._typeName.c_str() );
            if ( _propertyGrid.getOpenAllRequest() != OpenAllRequest::None )
                ImGui::SetNextItemOpen( _propertyGrid.getOpenAllRequest() == OpenAllRequest::ExpandAll );
            if ( ImGui::CollapsingHeader( card._typeDisplayName.c_str(), ImGuiTreeNodeFlags_DefaultOpen ) )
            {
                _viewModel.setCurrentMultiComponent( index );
                EditorPropertyGridTarget target = EditorPropertyGridTarget::makeSceneTarget( EditorPropertyGridScope::MainScene, _state._objectID, card._componentID );
                target._onEdited                = SW_DELEGATE_METHOD( Delegate<void( const PropertyInfo& )>, &InspectorViewModel::copyEditToOthers, &_viewModel );
                target._isMixed                 = SW_DELEGATE_METHOD( Delegate<bool( const PropertyInfo& )>, &InspectorViewModel::isMultiEditMixed, &_viewModel );
                _propertyGrid.drawProperties( target, nullptr, {} );
                _viewModel.setCurrentMultiComponent( InspectorViewModel::kNoMultiComponent );
            }
            ImGui::PopID();
        }
        if ( _state._unsharedCount > 0 )
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "ComponentSAreNot", TextArgumentList().addInteger( "componentCount", static_cast<int64>( _state._unsharedCount ) ) ).c_str() );
    }

    void InspectorPanel::drawFavoritesSection()
    {
        if ( _state._listFavorite.empty() )
            return;
        ImGui::SeparatorText( SW_LOCKEY( "EditorInspector", "FavoritesSection" ) );
        EditorSelfTestMarks::record( "inspector.favorites" );
        for ( const EditorPropertyGridTarget& target : _state._listFavorite )
        {
            // 같은 타입의 컴포넌트가 둘이면 id 가 겹치므로 컴포넌트 id 로 나눈다(오브젝트 자신은 0).
            ImGui::PushID( static_cast<int32>( target._componentID ) );
            _propertyGrid.drawFavoriteProperties( target );
            ImGui::PopID();
        }
        ImGui::Spacing();
    }

    void InspectorPanel::drawPreviewSelection()
    {
        EditorWidgets::drawChip( SW_LOCKEY( "EdWidget", "Preview" ), SW_THEME_COLOR( "Base", "Accent" ) );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", _state._previewSceneName.c_str() );
        if ( _state._content == InspectorContent::PreviewEmpty )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorInspector", "NothingSelected" ) );
            return;
        }
        ImGui::TextUnformatted( _state._objectName.c_str() );
        ImGui::Separator();
        _propertyGrid.drawSearchBarWithOptions( SW_LOCKEY( "EditorInspector", "SearchTooltip" ) );
        // 구조 편집(추가 · 제거)도 메인과 같은 명령(`EditorSceneCommands`)이다 — 기록은 오브젝트가 사는 씬이 고른 창의 스택에 간다(`EditorTransaction`).
        // 카드는 gather 의 사본이라 아래 메뉴가 컴포넌트를 제거해도 순회가 깨지지 않는다.
        for ( const InspectorComponentCard& card : _state._listComponent )
        {
            if ( card._typeName.empty() )
                continue;
            ImGui::PushID( static_cast<int32>( card._componentID ) );
            const bool bOpen = ImGui::CollapsingHeader( card._typeDisplayName.c_str(), ImGuiTreeNodeFlags_DefaultOpen );
            drawComponentContextMenu( card._componentID ); // 메인 카드와 같은 컴포넌트 메뉴 — 고른 명령은 다음 프레임 첫머리에 돈다
            if ( bOpen )
            {
                const EditorPropertyGridTarget target = EditorPropertyGridTarget::makeSceneTarget( EditorPropertyGridScope::FocusedPreview, _state._objectID, card._componentID );
                _propertyGrid.drawProperties( target, nullptr, {} );
            }
            ImGui::PopID();
        }

        ImGui::Spacing();
        drawAddComponentButton( "PreviewAddComponent", "inspector.previewAddComponent", false );
    }

    void InspectorPanel::drawSceneAssemblySection()
    {
        const bool bInstanceRelated = _state._bInstanceRelated == SW_TRUE;
        if ( bInstanceRelated == false && _state._listDataLayer.empty() )
            return;

        if ( bInstanceRelated )
        {
            EditorWidgets::drawChip( SW_LOCKEY( "EdWidget", "SceneInstance" ), SW_THEME_COLOR( "Base", "Accent" ) );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "SceneInstance" ) );
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", _state._instanceLabel.c_str() );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "ApplyInstanceOverrides" ) ) )
                _viewModel.applyInstanceOverrides();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ApplyInstanceOverrides" ) );
        }

        // 멤버의 레이어는 내부 씬의 엔티티와 앵커가 정한다 — gather 가 줄을 비운다.
        if ( _state._listDataLayer.empty() == false )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "DataLayers" ) );
            for ( const InspectorDataLayerRow& layer : _state._listDataLayer )
            {
                ImGui::PushID( layer._name.c_str() );
                bool bOnLayer = layer._bOn == SW_TRUE;
                if ( ImGui::Checkbox( layer._name.c_str(), &bOnLayer ) )
                    _viewModel.setDataLayer( _state._objectID, layer._name, bOnLayer );
                ImGui::PopID();
            }
        }
        ImGui::Separator();
    }

    void InspectorPanel::drawPrefabLinkSection()
    {
        EditorWidgets::drawChip( SW_LOCKEY( "EdWidget", "Prefab" ), SW_THEME_COLOR( "Base", "Accent" ) );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "PrefabInstance" ) );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", _state._prefabPath.c_str() );

        if ( ImGui::Button( SW_LOCKEY( "EdText", "ApplyToPrefab" ) ) )
            _viewModel.applyToPrefab( _state._objectID );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ApplyToPrefab" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "RevertToPrefab" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "RevertToPrefab" ) ) )
            _viewModel.revertToPrefab( _state._objectID );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "RevertToPrefab" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Unlink" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Unlink" ) ) )
            _viewModel.unlinkPrefab( _state._objectID );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "UnlinkPrefab" ) );

        // 중첩 부품이면 바깥 인스턴스의 프리팹에도 적용할 수 있다 — 부품의 수정이 바깥 프리팹의 노드 오버라이드(`<Part>` 포함)가 된다.
        if ( _state._bHasOuter == SW_TRUE )
        {
            if ( ImGui::Button( SW_LOCKEY( "EdText", "ApplyToOuterPrefab" ) ) )
                _viewModel.applyToOuterPrefab( _state._objectID );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ApplyToPrefab" ) );
            ImGui::SameLine();
            ImGui::TextDisabled( "%s: %s", _state._outerName.c_str(), _state._outerPrefabPath.c_str() );
        }

        ImGui::Separator();
    }

    void InspectorPanel::drawComponentList()
    {
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Components" ) );
        const TypeRegistry& registry = PropertyGridModel::getTypeRegistry();
        // 카드는 gather 의 사본이라 아래에서 컴포넌트를 제거해도 순회가 깨지지 않는다. 제거하면 이번 프레임은 거기서 멈춘다.
        for ( const InspectorComponentCard& card : _state._listComponent )
        {
            // 카드 헤더 앞에 컴포넌트 아이콘을 둔다. 유니티 Inspector 와 언리얼 Details 의 컴포넌트 헤더에 해당한다
            fixed_string<constant::buffer::kMax128> cardTitle;
            formatstring( cardTitle.data(), cardTitle.capacity(), "%#  %#", EditorComponentIconRow::findRow( registry.findType( card._typeName ) )._pGlyph, card._title.c_str() );
            // 체크박스는 컴포넌트 자기 비트다. 실효값(소유 오브젝트의 계층 활성까지 포함)을 읽어 그대로 다시 쓰면
            // 꺼진 부모 아래의 컴포넌트는 인스펙터에 보이기만 해도 자기 비트가 꺼지므로 주의한다. 쓰는 것도 값이 바뀐 때 한 번이다(아래).
            const bool bWasActive = card._bSelfActive == SW_TRUE;
            bool       bActive    = bWasActive;
            bool       bRemove{ false };
            const bool bScrollTo = card._bScrollTo == SW_TRUE;

            if ( bScrollTo )
            {
                ImGui::SetNextItemOpen( true );
                _viewModel.consumeScrollTo();
            }
            else if ( _propertyGrid.getOpenAllRequest() != OpenAllRequest::None )
            {
                ImGui::SetNextItemOpen( _propertyGrid.getOpenAllRequest() == OpenAllRequest::ExpandAll ); // 보기 옵션의 모두 펼치기 · 접기는 컴포넌트 카드에도 적용한다
            }

            if ( EditorWidgets::beginComponentCard( cardTitle.c_str(), card._componentID, &bActive, &bRemove, card._bAccent == SW_TRUE ) )
            {
                if ( bScrollTo )
                    ImGui::SetScrollHereY( 0.25f );

                drawComponentContextMenu( card._componentID );

                drawComponentSection( card );
                EditorWidgets::endComponentCard();
            }
            if ( bActive != bWasActive )
                _viewModel.setComponentActive( _state._objectID, card._componentID, bActive );

            if ( bRemove )
            {
                _viewModel.removeComponent( _state._objectID, card._componentID );
                break;
            }
        }

        // 컴포넌트 목록 끝의 Add Component 다. 유니티 Inspector 맨 아래, 언리얼 Details 의 Add 에 해당하며 Hierarchy 메뉴와 같은 목록이다.
        ImGui::Spacing();
        drawAddComponentButton( "InspectorAddComponent", "inspector.addComponent", true );
    }

    void InspectorPanel::drawAddComponentButton( const utf8* pPopupID, const utf8* pMarkPrefix, bool bCentered )
    {
        const EditorIconLabel label = EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Plus" ), SW_LOCKEY( "EdButton", "AddComponent" ), "Add Component" ); // 자기 버퍼를 든 값이라 그리는 동안 붙잡아 둔다
        bool                  bOpen = false;
        if ( bCentered )
        {
            const float32 buttonWidth = MathUtil::min( ImGui::GetContentRegionAvail().x, 220.0f * EditorThemeUtil::getDPIScale() );
            ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( ImGui::GetContentRegionAvail().x - buttonWidth ) * 0.5f );
            bOpen = ImGui::Button( label, ImVec2{ buttonWidth, 0.0f } );
            EditorSelfTestMarks::record( pMarkPrefix );
        }
        else
            bOpen = ImGui::Button( label );
        if ( bOpen )
        {
            _addComponentSearch.clear();
            EditorInputGUI::openPopup( pPopupID );
        }
        if ( ImGui::BeginPopup( pPopupID ) )
        {
            // 공용 목록 위젯이 오브젝트 포인터를 받는다 — 그 창구만 ViewModel 이 준다.
            GameObject* pObj    = _viewModel.findShownObject();
            bool        bFailed = false;
            if ( pObj == nullptr || EditorComponentMenu::drawAddComponentList( pObj, _addComponentSearch, pMarkPrefix, ImGui::IsWindowAppearing(), bFailed ) )
                ImGui::CloseCurrentPopup();
            if ( bFailed )
                SW_LOG_WARNING_ID( "InspectorPanel.CouldNotAddComponent", "Could not add the component to '%#'", _state._objectName.c_str() );
            ImGui::EndPopup();
        }
    }

    void InspectorPanel::drawComponentContextMenu( uint64 componentID )
    {
        // 계층 창의 컴포넌트 행 · 미리보기 카드와 같은 메뉴다(`scene.component.*` 와 프리셋 하위 메뉴). 선택은 오브젝트 단위라 컴포넌트 선택을 바꾸지 않는다.
        EditorContextMenuSubject subject{};
        subject._pPopupID  = "CompCardCtx";
        subject._kind      = EditorCommandTargetKind::Component;
        subject._hoveredID = componentID;
        (void)EditorInputGUI::drawContextMenu( subject ); // 열렸는지는 쓰지 않는다
    }

    void InspectorPanel::drawComponentPresetItems( const EditorCommandTarget& target )
    {
        ImGui::InputTextWithHint( "##presetName", SW_LOCKEY( "EditorInspector", "PresetNameHint" ), _presetNameBuffer.data(), _presetNameBuffer.capacity() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Save" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) ) && _presetNameBuffer.empty() == false && _viewModel.saveComponentPreset( target, _presetNameBuffer.c_str() ) )
            _presetNameBuffer.clear();
        ImGui::Separator();

        _viewModel.gatherPresets( target, _listPreset );
        for ( const InspectorPresetRow& preset : _listPreset )
        {
            if ( ImGui::MenuItem( preset._displayName.c_str() ) )
                _viewModel.loadComponentPreset( target, preset._file );
        }
        if ( _listPreset.empty() )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "NoSavedPresets" ) );
    }

    void InspectorPanel::drawGameObjectHeader()
    {
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "GameObjectID", TextArgumentList().addInteger( "objectID", static_cast<int64>( _state._objectID ) ) ).c_str() );

        // 편집 중이 아니면 오브젝트 이름으로 채운다. 편집 중인 텍스트를 멤버로 보관해야 입력란을 떠난 프레임(ImGui 가 버퍼에 쓰지 않는다)에도 적용할 수 있다.
        // Enter 를 누르거나 다른 곳을 눌러 떠나도 적용한다(유니티와 언리얼). Esc 는 ImGui 가 텍스트를 되돌려 편집 없음으로 끝난다.
        const bool bEditingName = ImGui::GetActiveID() == ImGui::GetID( "##Name" ) && _nameEditObjectID == _state._objectID;
        if ( bEditingName == false )
        {
            _nameEditBuffer   = _state._objectName.c_str();
            _nameEditObjectID = _state._objectID;
        }
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "NameAlt" ) );
        const bool bEnter         = ImGui::InputText( "##Name", _nameEditBuffer.data(), _nameEditBuffer.capacity(), ImGuiInputTextFlags_EnterReturnsTrue );
        const bool bLeftAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
        if ( bEnter || bLeftAfterEdit )
            _viewModel.renameObject( _nameEditObjectID, _nameEditBuffer.c_str() ); // 빈 이름 · 같은 이름은 ViewModel 이 거른다
        EditorSelfTestMarks::record( "inspector.name" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ObjectName" ) );

        bool bActive = _state._bActive == SW_TRUE;
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Active" ), &bActive ) )
            _viewModel.setObjectActive( _state._objectID, bActive );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ObjectActive" ) );

        if ( _state._bHasParent == SW_TRUE )
        {
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "Parent", TextArgumentList().addText( "parent", _state._parentName.c_str() ) ).c_str() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Unparent" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "Unparent" ) ) )
                _viewModel.detachObjectFromParent( _state._objectID );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "Unparent" ) );
        }
        else
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "ParentRoot" ) );

        if ( _state._listTag.empty() == false )
        {
            ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Tags" ) );
            for ( const string& tag : _state._listTag )
            {
                ImGui::SameLine();
                EditorWidgets::drawChip( tag.c_str(), SW_THEME_COLOR( "Signal", "Success" ) );
            }
        }
    }

    void InspectorPanel::drawComponentSection( const InspectorComponentCard& card )
    {
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "ID", TextArgumentList().addInteger( "componentID", static_cast<int64>( card._componentID ) ) ).c_str() );

        if ( card._typeName.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorInspector", "NoTypeInfo" ) );
            return;
        }

        // 확장은 이 타입과 그 기반 타입들에 등록된 것 전부다(기반에서 파생 순서). 각자 자기 영역을 그리고 직접 그린 리플렉션 프로퍼티만 감춘다. 나머지 리플렉션
        // 프로퍼티는 상속한 것까지 아래에서 그린다(언리얼 Details 의 `IDetailCustomization` 과 `HideProperty`). 확장 API 는 컴포넌트 포인터를 받는다 — 그 창구만 ViewModel 이 준다.
        const vector<IInspectorComponent*>& listInspector = InspectorViewModel::getInspectorsForType( card._typeName );
        Component*                          pComp         = listInspector.empty() ? nullptr : _viewModel.findShownComponent( card._componentID );
        vector<hashed_string>               listDrawnName;
        if ( pComp != nullptr )
        {
            for ( IInspectorComponent* pInspector : listInspector )
            {
                pInspector->drawHeader( pComp );
            }
            for ( IInspectorComponent* pInspector : listInspector )
            {
                // 확장 영역도 프로퍼티 위젯과 같은 규칙으로 되돌리기와 dirty 에 남긴다. 영역을 하나로 묶어 위젯 하나처럼 추적하므로
                // 영역이 위젯마다 `trackRowEdit` 를 호출하지 않아도 된다.
                ImGui::BeginGroup();
                pInspector->drawSection( pComp );
                ImGui::EndGroup();
                InspectorPropertyUndo::trackLastItem( card._typeDisplayName.c_str() );
                pInspector->collectDrawnProperties( listDrawnName );
            }
        }

        const EditorPropertyGridTarget componentTarget = EditorPropertyGridTarget::makeSceneTarget( EditorPropertyGridScope::MainScene, _state._objectID, card._componentID );
        _propertyGrid.drawProperties( componentTarget, "Properties", listDrawnName );
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Methods" ) );
        _propertyGrid.drawMethodsAndEvents( componentTarget, true );

        if ( pComp != nullptr )
        {
            for ( IInspectorComponent* pInspector : listInspector )
            {
                pInspector->drawFooter( pComp );
            }
        }
    }
} // namespace sw::editor
