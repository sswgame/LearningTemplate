#include "pch.h"

#include "Editor/Panels/Inspector/EditorPropertyGrid.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorCompactControls.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"
#include "Editor/Panels/Inspector/Model/InspectorPropertyLayout.h"
#include "Editor/Panels/Inspector/Model/PropertyEditConditionExpr.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionEditorMetadata.h"
#include "Engine/Reflection/ReflectionInvoke.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct EditorPropertyGridInternal
        {
            static constexpr Color kNestedHeader{ 0.15f, 0.15f, 0.15f, 1.0f };

            /** @brief 비트 플래그 열거형을 콤보로 그립니다. 전체 선택 · 해제 · 항목 체크는 @p inoutEditedValue 만 바꿉니다. */
            static void drawBitFlagEnum( const EnumInfo& enumInfo, int64 currentValue, bool bReadOnly, int64& inoutEditedValue )
            {
                const utf8* pLabel = "##value";
                string      previewStr;
                for ( const auto& [val, nameHashed] : enumInfo._mapValueToName )
                {
                    if ( val != 0 && ( currentValue & val ) == val )
                    {
                        if ( previewStr.empty() == false )
                            previewStr += " | ";
                        previewStr += nameHashed.c_str();
                    }
                }
                if ( previewStr.empty() )
                    previewStr = ( currentValue == 0 ) ? "None" : "<Unknown>";

                if ( bReadOnly )
                {
                    ImGui::TextDisabled( "%s", pLabel );
                    ImGui::SameLine();
                    ImGui::TextUnformatted( previewStr.c_str() );
                    return;
                }

                if ( ImGui::BeginCombo( pLabel, previewStr.c_str() ) == false )
                    return;
                if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "SelectAll" ) ) )
                {
                    for ( const auto& [val, _] : enumInfo._mapValueToName )
                    {
                        inoutEditedValue |= val;
                    }
                }
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "ClearAll" ) ) );
                if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "ClearAll" ) ) )
                    inoutEditedValue = 0;
                ImGui::Separator();

                for ( const auto& [val, nameHashed] : enumInfo._mapValueToName )
                {
                    if ( val == 0 )
                        continue;
                    bool bChecked = ( ( inoutEditedValue & val ) == val );
                    if ( ImGui::Checkbox( nameHashed.c_str(), &bChecked ) )
                    {
                        if ( bChecked )
                            inoutEditedValue |= val;
                        else
                            inoutEditedValue &= ~val;
                    }
                }
                ImGui::EndCombo();
            }

            /** @brief 하나만 고르는 열거형을 콤보로 그립니다. 고른 값은 @p outEditedValue 에 둡니다. */
            static void drawPlainEnum( const TypeRegistry& registry, const EnumInfo& enumInfo, const PropertyInfo& prop, int64 currentValue, bool bReadOnly, int64& outEditedValue )
            {
                const utf8* pLabel   = "##value";
                const utf8* pName    = registry.enumToString( prop._typeName, currentValue );
                const utf8* pPreview = ( pName != nullptr ) ? pName : "<Unknown>";
                if ( bReadOnly )
                {
                    ImGui::TextDisabled( "%s", pLabel );
                    ImGui::SameLine();
                    ImGui::TextUnformatted( pPreview );
                    return;
                }

                if ( ImGui::BeginCombo( pLabel, pPreview ) == false )
                    return;
                for ( const auto& [val, nameHashed] : enumInfo._mapValueToName )
                {
                    const bool bSelected = ( val == currentValue );
                    if ( ImGui::Selectable( nameHashed.c_str(), bSelected ) )
                        outEditedValue = val;
                    if ( EditorSelfTestMarks::isEnabled() )
                    {
                        // 시나리오가 콤보 항목을 누를 수 있게 한다(`inspector.enum.<프로퍼티>.<열거자>`) — 콤보를 연 프레임에만 그려진다.
                        fixed_string<constant::buffer::kMax128> itemMark;
                        formatstring( itemMark.data(), itemMark.capacity(), "inspector.enum.%#.%#", prop._name.c_str(), nameHashed.c_str() );
                        EditorSelfTestMarks::record( itemMark.c_str() );
                    }
                    if ( bSelected )
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorPropertyGrid::EditorPropertyGrid()
        : _propertyFilter{}
        , _mapMethodArgSlot{}
        , _mapContainerAddText{}
        , _listMethodArgChoice{}
        , _model{}
        , _propertyDrawDepth{ 0 }
        , _openAllRequest{ 0 }
        , _bFavoritesOnly{ SW_FALSE }
        , _bShowAllMethods{ SW_FALSE }
        , _bShowModifiedOnly{ false }
        , _layoutUseDepth{ 0 }
        , _methodCountFrame{ -1 }
        , _methodRowCount{ 0 }
        , _hiddenMethodCount{ 0 }
    {
    }

    void EditorPropertyGrid::drawSearchBarWithOptions( const utf8* pFieldTooltip )
    {
        const float32 buttonWidth = ImGui::GetFrameHeight();
        const float32 spacing     = ImGui::GetStyle().ItemSpacing.x;
        // drawSearchField 는 width 뒤에 지우기 단추(22 * dpi)를 더 그린다 — 둘을 빼야 보기 옵션 단추가 패널 오른쪽 밖으로 밀려나지 않는다.
        const float32 clearWidth = 22.0f * EditorThemeUtil::getDPIScale();
        const float32 fieldWidth = MathUtil::max( ImGui::GetContentRegionAvail().x - buttonWidth - clearWidth - spacing * 2.0f, 1.0f );
        EditorWidgets::drawSearchField( "##propFilter", _propertyFilter, SW_LOCKEY( "EditorInspector", "SearchPropertiesHint" ), fieldWidth );
        if ( pFieldTooltip != nullptr )
            EditorWidgets::drawTooltip( pFieldTooltip );
        ImGui::SameLine();
        // 거르는 중이면 단추가 켜진 바탕으로 그려진다 — 꺼진 줄 모르고 프로퍼티가 사라졌다고 여기지 않게 한다(예전 아이콘 토글은 켜도 모양이 같았다).
        if ( EditorCompactControls::beginViewOptions( "##InspectorViewOptions", _bShowModifiedOnly, SW_LOCKEY( "EditorInspector", "ViewOptions" ), "inspector.viewOptions" ) )
        {
            ImGui::MenuItem( SW_LOCKEY( "EdLabel", "ShowOnlyModifiedProperties" ), nullptr, &_bShowModifiedOnly );
            EditorSelfTestMarks::record( "inspector.viewOptions.modifiedOnly" );
            ImGui::Separator();
            if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "ExpandAll" ) ) )
                _openAllRequest = 1;
            EditorSelfTestMarks::record( "inspector.viewOptions.expandAll" );
            if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "CollapseAll" ) ) )
                _openAllRequest = -1;
            EditorSelfTestMarks::record( "inspector.viewOptions.collapseAll" );
            ImGui::Separator();
            // 진단 보기 — CallInEditor 가 없는 FUNCTION 도 호출할 수 있게 한다. 실행 안전성을 선언하지 않은 함수라 기본 목록에서 뺀다.
            if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "ShowAllFunctions" ), nullptr, _bShowAllMethods == SW_TRUE ) )
                _bShowAllMethods = _bShowAllMethods == SW_TRUE ? SW_FALSE : SW_TRUE;
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ShowAllFunctionsTooltip" ) );
            EditorSelfTestMarks::record( "inspector.viewOptions.allFunctions" );
            EditorCompactControls::endViewOptions();
        }
    }

    void EditorPropertyGrid::drawSearchBar( const utf8* pMarkKey )
    {
        if ( pMarkKey != nullptr )
        {
            EditorWidgets::drawSearchField( "##propFilter", _propertyFilter, SW_LOCKEY( "EditorInspector", "SearchPropertiesHint" ), 0.0f, false, pMarkKey );
            return;
        }
        EditorWidgets::drawSearchField( "##propFilter", _propertyFilter, SW_LOCKEY( "EditorInspector", "SearchPropertiesHint" ) );
    }

    void EditorPropertyGrid::drawProperties( const EditorPropertyGridTarget& target, const utf8* pSectionTitle, const vector<hashed_string>& listDrawnName )
    {
        // 씬 대상은 이 프레임에 id 로 다시 찾는다 — 사라졌으면 그리지 않는다.
        if ( _model.beginTarget( target ) )
        {
            drawTypeProperties( _model.getInstance(), _model.getType(), pSectionTitle, listDrawnName );
            _model.endTarget();
        }
        // 편집 알림(다중 선택의 나머지에 값 적용)이 끝난 뒤에 되돌리기를 남긴다.
        InspectorPropertyUndo::commitFinishedEdits();
    }

    void EditorPropertyGrid::drawFavoriteProperties( const EditorPropertyGridTarget& target )
    {
        // 즐겨찾기가 없는 타입은 `drawTypeProperties` 가 묶음을 비워 아무것도 그리지 않는다.
        if ( _model.hasFavorites() == false )
            return;
        // 본문 그리드와 같은 카테고리 · 프로퍼티 id 를 쓰므로 id 공간을 나눈다.
        ImGui::PushID( "##favorites" );
        _bFavoritesOnly = SW_TRUE;
        drawProperties( target, nullptr, {} );
        _bFavoritesOnly = SW_FALSE;
        ImGui::PopID();
    }

    void EditorPropertyGrid::drawMethodsAndEvents( const EditorPropertyGridTarget& target, bool bEvents )
    {
        if ( _model.beginTarget( target ) == false )
            return;
        drawTypeMethods( _model.getInstance(), _model.getType() );
        if ( bEvents )
            drawTypeEvents( _model.getInstance(), _model.getType() );
        _model.endTarget();
    }

    void EditorPropertyGrid::drawTypeProperties( void* pInstance, const TypeInfo* pTypeInfo, const utf8* pSectionTitle, const vector<hashed_string>& listDrawnName )
    {
        // 즐겨찾기 거르기는 이번 한 번만이다. 중첩 그리기가 다시 들어와도 거르지 않게 먼저 지운다.
        const bool bFavoritesOnly = _bFavoritesOnly && _model.hasFavorites();
        _bFavoritesOnly           = SW_FALSE;
        if ( pInstance == nullptr || pTypeInfo == nullptr )
            return;

        // 무엇을 어떤 순서로 그릴지는 `InspectorPropertyLayout` 이 정한다. 상속한 프로퍼티까지 포함하고, 카테고리는 기반 클래스부터 처음 나온 순서다.
        // 묶음은 (타입, 그린 이름, 검색 글, 레지스트리 세대)가 같으면 캐시한 것을 쓴다. 즐겨찾기 · 바뀐 것만 보기는 그 사본을 거른다.
        const EditorListFilter                filter{ _propertyFilter.c_str() };
        const vector<InspectorPropertyGroup>& listCachedGroup =
            _model.getOrCreateGroups( *pTypeInfo, listDrawnName, filter, _propertyFilter.c_str(), _layoutUseDepth == 0 );
        vector<InspectorPropertyGroup>        listFilteredGroup;
        const vector<InspectorPropertyGroup>* pListGroup = &listCachedGroup;

        // 즐겨찾기 구역이면 즐겨찾기한 프로퍼티만 타입 이름 머리 하나로 모은다.
        if ( bFavoritesOnly )
        {
            listFilteredGroup = listCachedGroup;
            pListGroup        = &listFilteredGroup;
            _model.keepFavoriteProperties( *pTypeInfo, listFilteredGroup );
            if ( listFilteredGroup.empty() )
                return;
        }

        // 검색어가 아무 프로퍼티도 맞히지 못하면 그렇다고 알려 준다.
        if ( pListGroup->empty() )
        {
            if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            return;
        }
        // 행 메뉴의 즐겨찾기 토글은 대상 자신(컴포넌트나 오브젝트)의 프로퍼티에만 둔다. 중첩 구조체의 필드는 키가 대상 타입이 아니다.
        const bool bFavoriteTarget = _model.hasFavorites() && _model.isTargetInstance( pInstance );

        if ( pSectionTitle != nullptr )
            ImGui::SeparatorText( pSectionTitle );
        // 컴포넌트이면 그 타입의 기본 인스턴스(CDO)와 비교한다. 중첩 구조체와 씬 밖 객체는 메타의 기본값 텍스트만 있다.
        const MutableObjectAddress instance{ pInstance };
        const ObjectAddress        defaultInstance = _model.findDefaultInstance( pInstance, *pTypeInfo );
        // 바뀐 것만 보기이면 기본값과 같은 프로퍼티를 뺀다. 남은 것이 없는 카테고리는 머리째 감춘다.
        if ( _bShowModifiedOnly )
        {
            if ( pListGroup != &listFilteredGroup )
            {
                listFilteredGroup = listCachedGroup;
                pListGroup        = &listFilteredGroup;
            }
            _model.removeDefaultValued( instance, defaultInstance, listFilteredGroup );
            if ( listFilteredGroup.empty() )
                return;
        }
        // 그리는 동안 다른 그리기(프로퍼티 확장 안의 그리드 호출)가 캐시를 비우면 아래 묶음이 사라진다 — 겹을 세어 그동안은 비우지 않게 한다.
        ++_layoutUseDepth;
        for ( const InspectorPropertyGroup& group : *pListGroup )
        {
            const string& category = group._category;
            const auto&   props    = group._listProperty;
            if ( _openAllRequest != 0 )
                ImGui::SetNextItemOpen( _openAllRequest > 0 );
            if ( ImGui::CollapsingHeader( category.c_str(), ImGuiTreeNodeFlags_DefaultOpen ) == false )
                continue;

            if ( ImGui::BeginTable( category.c_str(), 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable ) ) // 줄무늬 바탕은 두지 않는다(앱 화면 규칙 — 행은 간격으로 가른다)
            {
                ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthFixed, 150.0f * EditorThemeUtil::getDPIScale() );
                ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthStretch );

                for ( const PropertyInfo* prop : props )
                {
                    // `EditCondition` 이 거짓이면 숨기거나(EditConditionHides) 막는다. 판정은 ImGui 에 의존하지 않는 `PropertyEditConditionExpr` 이 한다.
                    const PropertyEditState editState = PropertyEditConditionExpr::getEditState( *pTypeInfo, *prop, instance );
                    if ( editState == PropertyEditState::Hidden )
                        continue;

                    drawPropertyRow( instance, *pTypeInfo, *prop, editState == PropertyEditState::Disabled, bFavoriteTarget, defaultInstance );
                }
                ImGui::EndTable();
            }
        }
        --_layoutUseDepth;
    }

    void EditorPropertyGrid::drawPropertyRow( const MutableObjectAddress instance, const TypeInfo& typeInfo, const PropertyInfo& prop, bool bDisabled, bool bFavoriteTarget, const ObjectAddress defaultInstance )
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        ImGui::AlignTextToFramePadding();
        if ( _model.isMixed( prop ) )
        {
            ImGui::TextDisabled( "\xe2\x80\x94" ); // — (혼합)
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "MultipleValues" ) );
            ImGui::SameLine();
        }
        const string& favoriteKey = _model.getLaidOutDisplay( prop )._favoriteKey; // 묶음을 지을 때 만든 키 — 행마다 string 을 짓지 않는다
        const bool    bFavorite   = bFavoriteTarget && _model.isFavorite( favoriteKey );
        if ( bFavorite )
        {
            ImGui::TextUnformatted( SW_EDITOR_ICON( "Star" ) );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "FavoriteProperty" ) );
            ImGui::SameLine();
        }
        ImGui::TextUnformatted( InspectorPropertyLayout::getPropertyLabel( prop ).c_str() );
        EditorWidgets::drawTooltip( prop._metadata->getEditor()._tooltip.c_str() );
        // 시나리오가 행 메뉴를 오른쪽 클릭으로 열 수 있게 이름 글에 레이블을 남긴다(`inspector.label.<타입>.<프로퍼티>`).
        fixed_string<constant::buffer::kMax128> labelMark;
        formatstring( labelMark.data(), labelMark.capacity(), "inspector.label.%#.%#", typeInfo._name.c_str(), prop._name.c_str() );
        EditorSelfTestMarks::record( labelMark.c_str() );

        drawPropertyRowMenu( instance, prop, favoriteKey, bFavoriteTarget, bFavorite, defaultInstance );

        // 기본값과 다르면 이름 영역 끝에 되돌리기 버튼(언리얼의 노란 화살표)을 둔다. 기본값은 컴포넌트의 기본 인스턴스(CDO)이고, 없으면 메타 텍스트다.
        if ( EditorDefaultObjects::isDefaultValue( prop, instance, defaultInstance ) == false )
        {
            if ( EditorCompactControls::drawResetButton( "##reset", SW_LOCKEY( "EditorInspector", "ResetToDefaultTooltip" ) ) )
                _model.resetToDefault( instance, prop, defaultInstance );
            fixed_string<constant::buffer::kMax128> resetMark;
            formatstring( resetMark.data(), resetMark.capacity(), "inspector.reset.%#.%#", typeInfo._name.c_str(), prop._name.c_str() );
            EditorSelfTestMarks::record( resetMark.c_str() );
        }

        if ( prop._metadata->_bTransient == SW_TRUE )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(T)" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "TransientProperty" ) );
        }

        ImGui::TableNextColumn();
        ImGui::PushID( prop._name.c_str() );
        ImGui::SetNextItemWidth( -FLT_MIN );
        ImGui::BeginDisabled( bDisabled );
        drawPropertyWidget( instance, prop );
        ImGui::EndDisabled();
        // 시나리오가 누를 수 있게 값 입력란에 이름을 남긴다(`inspector.property.<타입>.<프로퍼티>`). 테스트를 켰을 때만 기록한다.
        fixed_string<constant::buffer::kMax128> mark;
        formatstring( mark.data(), mark.capacity(), "inspector.property.%#.%#", typeInfo._name.c_str(), prop._name.c_str() );
        EditorSelfTestMarks::record( mark.c_str() );
        ImGui::PopID();
    }

    void EditorPropertyGrid::drawPropertyRowMenu( const MutableObjectAddress instance, const PropertyInfo& prop, const string& favoriteKey, bool bFavoriteTarget, bool bFavorite, const ObjectAddress defaultInstance )
    {
        if ( EditorInputGUI::beginItemContextMenu( "PropCtx" ) == false )
            return;

        // 언리얼 Details 의 행 메뉴 Add to Favorites · Remove from Favorites 에 해당한다. 바꾸면 바로 저장한다.
        if ( bFavoriteTarget )
        {
            if ( ImGui::MenuItem( bFavorite ? SW_LOCKEY( "EditorInspector", "RemoveFromFavorites" ) : SW_LOCKEY( "EditorInspector", "AddToFavorites" ) ) )
                _model.toggleFavorite( favoriteKey );
            ImGui::Separator();
        }
        if ( defaultInstance.get() != nullptr || prop._metadata->_defaultValue.empty() == false )
        {
            if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "ResetToDefault" ) ) )
                _model.resetToDefault( instance, prop, defaultInstance );
        }
        if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "CopyValue" ) ) )
            ImGui::SetClipboardText( SerializerUtil::formatPropertyText( prop, instance, SerializeContext::getDefault( SerializePurpose::Authoring ) ).c_str() );
        // 타입 핸들은 선택기만이 입력 길이다. 글로 붙이면 대상 타입을 확인하지 않은 값이 들어간다.
        const bool bPasteAllowed = ImGui::GetClipboardText() != nullptr && prop._handleTargetTypeName.empty();
        if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "PasteValue" ), nullptr, false, bPasteAllowed ) )
            _model.setPropertyText( instance, prop, ImGui::GetClipboardText(), "Paste Value" );
        EditorSelfTestMarks::record( "inspector.menu.pasteValue" );
        if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "CopyPropertyName" ) ) )
            ImGui::SetClipboardText( prop._name.c_str() );
        ImGui::EndPopup();
    }

    void EditorPropertyGrid::drawPropertyWidget( const MutableObjectAddress instance, const PropertyInfo& prop )
    {
        // ImGui 는 이번 프레임에 활성 아이템이 편집됐는지를 컨텍스트에 전역으로 기록한다. 위젯을
        // 그리기 전후로 그 플래그의 **변화**를 보면 "그 사이에 그린 이 프로퍼티가 편집됐다" 를 정확히
        // 가려낼 수 있다. 위젯이 아이템 하나든 여럿이든, 값이 float 이든 string 이든 상관없다.
        // 위젯마다 판정을 넣으면 새 위젯을 추가할 때 빠뜨리기 쉽지만, 여기 한 곳에 두면 빠뜨릴 수 없다.
        ImGuiContext& g             = *ImGui::GetCurrentContext();
        const bool    bEditedBefore = g.ActiveIdHasBeenEditedThisFrame;

        ++_propertyDrawDepth;
        drawPropertyWidgetBody( instance, prop );
        --_propertyDrawDepth;

        // 컨테이너와 중첩 구조체는 이 함수가 재귀한다. 알림은 가장 바깥에서 한 번만 한다.
        const bool bOutermostEdit = _propertyDrawDepth == 0 && bEditedBefore == false && g.ActiveIdHasBeenEditedThisFrame;
        if ( bOutermostEdit )
            _model.notifyPropertyEdited( prop );
    }

    void EditorPropertyGrid::drawPropertyWidgetBody( const MutableObjectAddress instance, const PropertyInfo& prop )
    {
        IInspectorProperty* pProperty = PropertyGridModel::getPropertyManager().find( prop._typeName.c_str() );
        if ( pProperty != nullptr )
        {
            if ( pProperty->draw( instance, prop, _model ) )
                return;
        }

        const bool bReadOnly = prop._metadata->_bReadOnly != SW_FALSE;

        const TypeRegistry& registry = PropertyGridModel::getTypeRegistry();

        const EnumInfo* pEnumInfo = registry.findEnum( prop._typeName );
        if ( pEnumInfo != nullptr )
        {
            drawEnumProperty( instance, prop, *pEnumInfo, bReadOnly );
            return;
        }

        if ( prop._bIsContainer && prop._containerWrapper != nullptr )
        {
            drawContainerProperty( instance, prop, bReadOnly );
            return;
        }

        const TypeInfo* pFieldType = registry.findType( prop._typeName );
        if ( pFieldType != nullptr || prop._typeName.isPredefinedType( PredefinedNameType::NameType_string ) )
        {
            drawStructOrStringProperty( instance, prop, pFieldType );
            return;
        }

        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "NoInspectorFor", TextArgumentList().addText( "type", prop._typeName.c_str() ) ).c_str() );
    }

    void EditorPropertyGrid::drawEnumProperty( const MutableObjectAddress instance, const PropertyInfo& prop, const EnumInfo& enumInfo, bool bReadOnly )
    {
        using Internal = EditorPropertyGridInternal;

        // enum 의 실제 크기(`EnumInfo::_size` 와 부호)로 읽고 쓴다. 항상 int32 로 읽고 쓰면, 엔진의 enum 은 대부분 uint8 이나
        // uint16 이므로 하나를 고를 때마다 뒤의 필드를 덮어쓰고(`CameraComponent::_role` 을 고르면 바로 뒤의 `_bOrthographic` 이 꺼진다),
        // 읽을 때는 이웃 바이트가 섞여 멀쩡한 값이 "<Unknown>" 으로 보인다.
        void* pEnumMemory = prop.getValuePtr<void>( instance.get() );
        if ( pEnumMemory == nullptr )
            return;
        const int64 currentValue = enumInfo.readValueFromMemory( pEnumMemory );
        int64       editedValue  = currentValue;

        if ( enumInfo._bIsBitFlag )
            Internal::drawBitFlagEnum( enumInfo, currentValue, bReadOnly, editedValue );
        else
            Internal::drawPlainEnum( PropertyGridModel::getTypeRegistry(), enumInfo, prop, currentValue, bReadOnly, editedValue );
        if ( bReadOnly )
            return;

        if ( editedValue != currentValue )
            _model.setEnumValue( prop, MutableValueAddress{ pEnumMemory }, enumInfo, editedValue );
    }

    void EditorPropertyGrid::drawContainerProperty( const MutableObjectAddress instance, const PropertyInfo& prop, bool bReadOnly )
    {
        const utf8* pLabel = "##value";

        // 이 함수의 모든 분기는 무언가를 그리고 끝난다. 아무것도 그리지 않고 반환하면 인스펙터에는 빈 항목 하나만 남아,
        // 값이 비었는지, 그리지 못하는 것인지, 버그인지 화면만 보고는 구분할 수 없다. 모르는 타입도 "No inspector for ..." 라고
        // 알린다. 맵 프로퍼티도 실제로 있다.
        void* pContainer = prop.getRawPtr( instance.get() );
        if ( pContainer == nullptr )
        {
            ImGui::TextDisabled( "컨테이너를 읽을 수 없습니다 (%s)", prop._typeName.c_str() );
            return;
        }

        ISequenceContainerWrapper* pSeq = prop._containerWrapper->asSequence();
        if ( pSeq == nullptr )
        {
            IMapContainerWrapper* pMap = prop._containerWrapper->asMap();
            if ( pMap == nullptr )
            {
                ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "NoInspectorFor", TextArgumentList().addText( "type", prop._typeName.c_str() ) ).c_str() );
                return;
            }

            drawMapContainer( MutableValueAddress{ pContainer }, prop, *pMap, bReadOnly );
            return;
        }
        // 원소가 곧 정렬 키이거나 해시 키인 컨테이너(`set`)는 제자리에서 고칠 수 없으므로 지우고 다시 넣는다.
        if ( pSeq->allowsInPlaceElementWrite() == false )
        {
            drawKeyedSequenceContainer( MutableValueAddress{ pContainer }, prop, *pSeq, bReadOnly );
            return;
        }

        const size_t                            count = pSeq->getSize( pContainer );
        fixed_string<constant::buffer::kMax128> headerBuf;
        formatstring( headerBuf.data(), headerBuf.capacity(), "[%#] (%# elements)", prop._elementTypeName.c_str(), count );

        const bool bElementEditable = EditorSessionPolicy::areContainerElementEditsAllowed( bReadOnly, true );

        if ( ImGui::TreeNodeEx( pLabel, ImGuiTreeNodeFlags_SpanFullWidth, "%s", headerBuf.c_str() ) )
        {
            // 고정 배열은 원소 수가 정해져 있다. 추가와 비우기는 아무 일도 하지 않으므로 그리지 않는다.
            if ( bReadOnly == false && pSeq->isFixedSize() == false )
            {
                if ( ImGui::SmallButton( "+ Add" ) )
                    _model.addElement( prop, MutableValueAddress{ pContainer } );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Clear" ) ) );
                if ( ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Clear" ) ) )
                    _model.clearElements( prop, MutableValueAddress{ pContainer } );
                ImGui::Separator();
            }

            // 원소 위젯도 같은 규칙을 받아야 한다. `bReadOnly` 가 위의 버튼만 가리면
            // `ReadOnly` 컨테이너의 원소가 그대로 편집된다.
            ImGui::BeginDisabled( bElementEditable == false );

            const size_t newCount = pSeq->getSize( pContainer );
            for ( size_t elemIndex = 0; elemIndex < newCount; ++elemIndex )
            {
                void* pElem = pSeq->getElement( pContainer, elemIndex );
                if ( pElem == nullptr )
                    continue;

                ImGui::PushID( static_cast<int32>( elemIndex ) );
                ImGui::AlignTextToFramePadding();
                ImGui::Text( "[%zu]", elemIndex );
                ImGui::SameLine();

                const PropertyInfo elemProp = InspectorPropertyLayout::makeElementProperty( prop );

                // 원소 프로퍼티는 오프셋 0 이라 원소의 값 주소가 곧 그 프로퍼티를 가진 객체 주소다.
                ImGui::SetNextItemWidth( -FLT_MIN );
                drawPropertyWidget( MutableObjectAddress{ pElem }, elemProp );
                ImGui::PopID();
            }

            ImGui::EndDisabled();
            ImGui::TreePop();
        }
    }

    void EditorPropertyGrid::drawMapContainer( const MutableValueAddress container, const PropertyInfo& prop, IMapContainerWrapper& mapWrapper, bool bReadOnly )
    {
        void* const                             pContainer = container.get();
        const SerializeContext&                 context    = SerializeContext::getDefault( SerializePurpose::Authoring );
        fixed_string<constant::buffer::kMax128> headerBuf;
        formatstring( headerBuf.data(), headerBuf.capacity(), "[%# -> %#] (%# entries)", prop._keyTypeName.c_str(), prop._elementTypeName.c_str(),
                      mapWrapper.getSize( pContainer ) );
        if ( ImGui::TreeNodeEx( "##value", ImGuiTreeNodeFlags_SpanFullWidth, "%s", headerBuf.c_str() ) == false )
            return;

        const PropertyInfo valueProp        = InspectorPropertyLayout::makeElementProperty( prop );
        const bool         bElementEditable = EditorSessionPolicy::areContainerElementEditsAllowed( bReadOnly, true );

        // 값은 맵 노드 안에서 바로 고친다. 되돌리기는 위젯 하나 단위다(`drawPropertyWidget`). 삭제는 순회가 끝난 뒤에 한다.
        size_t eraseOrdinal = static_cast<size_t>( -1 );
        size_t ordinal      = 0;
        mapWrapper.forEachMutable( pContainer, SW_DELEGATE_LAMBDA( MapForEachMutableDelegate, [&]( const void* pKey, void* pValue )
        {
            ImGui::PushID( static_cast<int32>( ordinal ) );
            if ( bReadOnly == false && ImGui::SmallButton( "x" ) )
                eraseOrdinal = ordinal;
            if ( bReadOnly == false )
                ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted( SerializerUtil::formatValueText( ValueAddress{ pKey }, prop._keyTypeName, context ).c_str() );
            ImGui::SameLine();
            ImGui::BeginDisabled( bElementEditable == false );
            ImGui::SetNextItemWidth( -FLT_MIN );
            drawPropertyWidget( MutableObjectAddress{ pValue }, valueProp ); // 값 프로퍼티는 오프셋 0 이라 값 주소가 곧 객체 주소다
            ImGui::EndDisabled();
            ImGui::PopID();
            ++ordinal;
        } ) );

        if ( eraseOrdinal != static_cast<size_t>( -1 ) )
            _model.removeMapEntry( prop, container, eraseOrdinal );

        string keyText;
        if ( bReadOnly == false && drawContainerAddRow( SW_LOCKEY( "EditorInspector", "NewKeyHint" ), keyText ) )
            _model.addMapEntry( prop, container, keyText ); // 같은 키(텍스트가 같다)가 이미 있으면 값을 덮어쓰지 않는다
        ImGui::TreePop();
    }

    void EditorPropertyGrid::drawKeyedSequenceContainer( const MutableValueAddress container, const PropertyInfo& prop, ISequenceContainerWrapper& sequence, bool bReadOnly )
    {
        void* const                             pContainer = container.get();
        const SerializeContext&                 context    = SerializeContext::getDefault( SerializePurpose::Authoring );
        const size_t                            count      = sequence.getSize( pContainer );
        fixed_string<constant::buffer::kMax128> headerBuf;
        formatstring( headerBuf.data(), headerBuf.capacity(), "[%#] (%# elements)", prop._elementTypeName.c_str(), count );
        if ( ImGui::TreeNodeEx( "##value", ImGuiTreeNodeFlags_SpanFullWidth, "%s", headerBuf.c_str() ) == false )
            return;

        // 원소는 값 주소다. 텍스트 변환은 값 주소 쪽(`formatValueText`, `parseTextValue`)을 원소 타입 이름으로 쓴다.
        const hashed_string& elementTypeName  = prop._elementTypeName;
        const bool           bElementEditable = EditorSessionPolicy::areContainerElementEditsAllowed( bReadOnly, true );
        size_t               eraseIndex       = static_cast<size_t>( -1 );
        size_t               replaceIndex     = static_cast<size_t>( -1 );
        string               replaceText;
        for ( size_t elemIndex = 0; elemIndex < count; ++elemIndex )
        {
            ImGui::PushID( static_cast<int32>( elemIndex ) );
            if ( bReadOnly == false && ImGui::SmallButton( "x" ) )
                eraseIndex = elemIndex;
            if ( bReadOnly == false )
                ImGui::SameLine();
            // 원소는 텍스트 입력란이다. Enter 로 확정한 텍스트를 원소 타입으로 읽어 지우고 다시 넣는다. 원소가 곧 정렬 키이므로 제자리에 쓰면 트리가 깨진다.
            fixed_string<constant::buffer::kMax256> elementText{ SerializerUtil::formatValueText( ValueAddress{ sequence.getElementConst( pContainer, elemIndex ) }, elementTypeName, context ).c_str() };
            const ImGuiInputTextFlags               flags = bElementEditable ? ImGuiInputTextFlags_EnterReturnsTrue : ImGuiInputTextFlags_ReadOnly;
            ImGui::SetNextItemWidth( -FLT_MIN );
            if ( ImGui::InputText( "##element", elementText.data(), elementText.capacity(), flags ) && bElementEditable )
            {
                replaceIndex = elemIndex;
                replaceText  = elementText.c_str();
            }
            ImGui::PopID();
        }

        if ( replaceIndex != static_cast<size_t>( -1 ) )
            _model.replaceKeyedElement( prop, container, replaceIndex, replaceText );
        else if ( eraseIndex != static_cast<size_t>( -1 ) )
            _model.removeKeyedElement( prop, container, eraseIndex );

        string addText;
        if ( bReadOnly == false && drawContainerAddRow( SW_LOCKEY( "EditorInspector", "NewElementHint" ), addText ) )
            _model.addKeyedElement( prop, container, addText );
        ImGui::TreePop();
    }

    bool EditorPropertyGrid::drawContainerAddRow( const utf8* pHint, string& outText )
    {
        const ImGuiID                            rowID = ImGui::GetID( "##add" );
        fixed_string<constant::buffer::kMax256>& text  = _mapContainerAddText[static_cast<uint32>( rowID )];
        ImGui::SetNextItemWidth( -FLT_MIN );
        const bool bEntered = ImGui::InputTextWithHint( "##add", pHint, text.data(), text.capacity(), ImGuiInputTextFlags_EnterReturnsTrue );
        if ( bEntered == false || text.empty() )
            return false;
        outText = text.c_str();
        text.clear();
        return true;
    }

    void EditorPropertyGrid::drawStructOrStringProperty( const MutableObjectAddress instance, const PropertyInfo& prop, const TypeInfo* pFieldType )
    {
        const utf8* pLabel = "##value";

        void* pNestedPtr = prop.getRawPtr( instance.get() );
        if ( pNestedPtr == nullptr )
            return;

        ImGui::PushStyleColor( ImGuiCol_Header, EditorColorUtil::toImVec4( EditorPropertyGridInternal::kNestedHeader ) );
        bool bNodeOpen = ImGui::TreeNodeEx( pLabel, ImGuiTreeNodeFlags_SpanFullWidth, "[%s]", prop._typeName.c_str() );
        ImGui::PopStyleColor();

        if ( bNodeOpen )
        {
            pFieldType->forEachProperty( [&]( const PropertyInfo& nestedProp )
            {
                // 구조체 필드의 값 주소는 그 구조체 타입의 객체 주소다.
                const PropertyEditState editState = PropertyEditConditionExpr::getEditState( *pFieldType, nestedProp, ObjectAddress{ pNestedPtr } );
                if ( editState == PropertyEditState::Hidden )
                    return;
                ImGui::PushID( nestedProp._name.c_str() );
                ImGui::AlignTextToFramePadding();
                ImGui::BulletText( "%s", InspectorPropertyLayout::getPropertyLabel( nestedProp ).c_str() );
                ImGui::SameLine();
                ImGui::SetNextItemWidth( -FLT_MIN );
                ImGui::BeginDisabled( editState == PropertyEditState::Disabled );
                drawPropertyWidget( MutableObjectAddress{ pNestedPtr }, nestedProp );
                ImGui::EndDisabled();
                ImGui::PopID();
            }, true ); // 구조체의 기반 필드도 그린다(컴포넌트와 같은 규칙)
            ImGui::TreePop();
        }
        return;
    }

    void EditorPropertyGrid::drawTypeMethods( void* pInstance, const TypeInfo* pTypeInfo )
    {
        const int32 frame = ImGui::GetFrameCount();
        if ( frame != _methodCountFrame )
        {
            _methodCountFrame  = frame;
            _methodRowCount    = 0;
            _hiddenMethodCount = 0;
        }

        // 그릴 메서드: 생성자는 빼고, 진단 보기가 아니면 CallInEditor 만. 숨긴 것이 있으면 빈 칸 글이 그렇다고 말한다("메서드 없음" 으로 읽히지 않게).
        uint32 drawableCount = 0;
        uint32 hiddenCount   = 0;
        if ( pInstance != nullptr && pTypeInfo != nullptr )
        {
            for ( const FunctionInfo& method : pTypeInfo->_listMethod )
            {
                if ( method._metadata->_bConstructor != SW_FALSE )
                    continue;
                if ( isMethodListed( method ) )
                    ++drawableCount;
                else
                    ++hiddenCount;
            }
        }
        _hiddenMethodCount += hiddenCount;

        if ( drawableCount == 0 )
        {
            EditorWidgets::drawEmptyHint( hiddenCount == 0 ? SW_LOCKEY( "EditorInspector", "NoFunctionMethods" ) : SW_LOCKEY( "EditorInspector", "NoCallInEditorMethods" ) );
            return;
        }

        const utf8* pLastInvokeResult = _model.getLastInvokeResult();
        if ( StringUtil::isNullOrEmpty( pLastInvokeResult ) == false )
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "LastResult", TextArgumentList().addText( "lastInvokeResult", pLastInvokeResult ) ).c_str() );

        for ( const FunctionInfo& method : pTypeInfo->_listMethod )
        {
            if ( method._metadata->_bConstructor != SW_FALSE || isMethodListed( method ) == false )
                continue;
            ++_methodRowCount;
            drawTypeMethod( pInstance, pTypeInfo, method );
        }
    }

    bool EditorPropertyGrid::isMethodListed( const FunctionInfo& method ) const
    {
        return method._metadata->getEditor()._bCallInEditor != SW_FALSE || _bShowAllMethods == SW_TRUE;
    }

    void EditorPropertyGrid::drawTypeMethod( void* pInstance, const TypeInfo* pTypeInfo, const FunctionInfo& method )
    {
        // 생성자 호출기는 초기화되지 않은 저장 공간(placement new)을 기대한다. 살아 있는 인스펙터 인스턴스에 호출하면 안전하지 않다.
        if ( method._metadata->_bConstructor != SW_FALSE )
            return;

        const utf8* pLabelName = method._name.c_str();
        if ( method._metadata->getEditor()._displayName.empty() == false )
            pLabelName = method._metadata->getEditor()._displayName.c_str();

        const uint32 paramCount = method.getParameterCount();
        // 네트워크 함수(Server · Client · Multicast)를 여기서 부르면 RPC 경로(`RemoteCall`)를 지나지 않고 이 프로세스에서 바로 돈다 — 툴팁이 그렇게 알린다.
        const bool bNetRole = method._metadata->_netRole != FunctionNetRole::Local;

        if ( method._metadata->getEditor()._bCallInEditor != SW_FALSE && paramCount == 0 )
        {
            // 강조색을 쓰지 않는다 — 앱 화면 규칙상 강조는 데이터 · 선택에만 둔다(전에는 패널 전용 파란 단추였다).
            ImGui::PushID( method._hashName.c_str() );
            fixed_string<constant::buffer::kMax128> buttonLabel;
            formatstring( buttonLabel.data(), buttonLabel.capacity(), "Run %#", pLabelName );
            if ( ImGui::Button( buttonLabel.c_str(), ImVec2{ -FLT_MIN, 0.0f } ) )
            {
                _model.invokeMethod( pInstance, *pTypeInfo, method, TaskArgs{} );
            }
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::record( ( string( "inspector.method.run." ) + method._name.c_str() ).c_str() );
            EditorWidgets::drawTooltip( bNetRole ? SW_LOCKEY( "EditorInspector", "NetRoleMethodRunsLocally" ) : method._metadata->getEditor()._tooltip.c_str() );
            ImGui::PopID();
            return;
        }

        ImGui::PushID( method._hashName.c_str() );
        ImGui::Text( "%s (%s)", pLabelName,
                     method._returnTypeName.empty() ? "?" : method._returnTypeName.c_str() );
        if ( method._metadata->getEditor()._bCallInEditor == SW_FALSE )
        {
            // 진단 보기에서만 보이는 줄이다 — 실행 안전성을 선언하지 않은 함수라는 표시를 색이 아니라 글로 단다.
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorInspector", "DiagnosticMethodTag" ) );
        }
        if ( bNetRole )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorInspector", "NetRoleNotRoutedTag" ) );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "NetRoleMethodRunsLocally" ) );
        }
        EditorWidgets::drawTooltip( method._metadata->getEditor()._tooltip.c_str() );
        // 키는 타입 · 메서드 이름 원문이다(해시 조합은 부딪히면 다른 메서드의 인자 슬롯을 쓴다).
        const string                    slotKey  = string( pTypeInfo->_fullyQualifiedName.c_str() ) + '\x1F' + method._hashName.c_str();
        vector<InspectorMethodArgSlot>& listSlot = _mapMethodArgSlot[slotKey];
        const bool                      bArgsOk  = InspectorBuiltinValue::prepareMethodArgs( method, listSlot );
        drawMethodArgInputs( method, listSlot );

        if ( paramCount > InspectorBuiltinValue::kMaxMethodArgCount )
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "TooManyArgumentsMax", TextArgumentList().addInteger( "maxMethodArgCount", static_cast<int64>( InspectorBuiltinValue::kMaxMethodArgCount ) ) ).c_str() );

        if ( bArgsOk == false )
        {
            ImGui::BeginDisabled();
            ImGui::Button( SW_LOCKEY( "EdText", "Invoke" ) );
            ImGui::EndDisabled();
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "UnsupportedFUNCTIONArgsInvokeSkipped" ) );
        }
        else
        {
            const bool bInvoke = ImGui::Button( SW_LOCKEY( "EdText", "Invoke" ) );
            if ( EditorSelfTestMarks::isEnabled() )
            {
                const string invokeMark = string( "inspector.method.invoke." ) + method._name.c_str();
                EditorSelfTestMarks::record( invokeMark.c_str() );
            }
            if ( bInvoke )
                _model.invokeMethod( pInstance, *pTypeInfo, method, InspectorBuiltinValue::makeMethodArgs( listSlot ) );
        }

        ImGui::PopID();
    }

    void EditorPropertyGrid::drawMethodArgInputs( const FunctionInfo& method, vector<InspectorMethodArgSlot>& inoutListSlot )
    {
        for ( uint32 paramIndex = 0; paramIndex < static_cast<uint32>( inoutListSlot.size() ); ++paramIndex )
        {
            ImGui::PushID( static_cast<int32>( paramIndex ) );
            // 선언에 인자 이름이 있으면 그것을 쓰고, 없으면 순번을 쓴다.
            const FunctionParameterInfo&           parameter = method._listParameter[paramIndex];
            fixed_string<constant::buffer::kMax64> label;
            if ( parameter._name.empty() )
                formatstring( label.data(), label.capacity(), "arg%# (%#)", paramIndex, parameter._typeName.c_str() );
            else
                formatstring( label.data(), label.capacity(), "%# (%#)", parameter._name.c_str(), parameter._typeName.c_str() );
            InspectorPropertyManager::drawMethodArg( label.c_str(), inoutListSlot[paramIndex], parameter, _model.findEditOwnerManager(), _listMethodArgChoice );
            if ( EditorSelfTestMarks::isEnabled() )
            {
                // 시나리오가 인자 입력란을 누른다(핸들 선택기의 콤보가 마지막 항목이다). 인자 레이블 글("mesh (ComponentHandle)")이 오른쪽에 붙어
                // 좁은 창에서 위젯 사각형의 가운데가 레이블 글 위에 놓이므로 입력 칸만 표식으로 남긴다.
                StringBuilder<constant::buffer::kMax256> argMarkBuilder;
                argMarkBuilder.appendFormat( "%#%#.%#", string( "inspector.method.arg." ), method._name.c_str(), paramIndex );
                const string argMark{ argMarkBuilder.view() };
                EditorSelfTestMarks::recordInputFrame( argMark.c_str() );
            }
            ImGui::PopID();
        }
    }

    void EditorPropertyGrid::drawTypeEvents( void* pInstance, const TypeInfo* pTypeInfo )
    {
        bool bHasEvent = false;
        pTypeInfo->forEachEventWithBase( [&bHasEvent]( const EventInfo& )
        { bHasEvent = true; } );
        if ( bHasEvent == false )
            return;

        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Events" ) );
        pTypeInfo->forEachEventWithBase( [pInstance]( const EventInfo& event )
        {
            ImGui::PushID( event._name.c_str() );
            const string signature = InspectorPropertyLayout::formatParameterList( event._listParameter );
            ImGui::BulletText( "%s(%s)", event._name.c_str(), signature.c_str() );
            EditorWidgets::drawTooltip( event._metadata->getEditor()._tooltip.c_str() );
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", ReflectionInvoke::isEventBound( event, pInstance ) ? "(bound)" : "(unbound)" );
            if ( event.getParameterCount() == 0 )
            {
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Broadcast" ) ) );
                if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "Broadcast" ) ) )
                    PropertyGridModel::broadcastEvent( event, pInstance );
            }
            ImGui::PopID();
        } );
    }
} // namespace sw::editor
