#include "pch.h"

#include "Editor/Panels/InspectorPanel.h"

#include "Core/File/FileUtil.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Commands/EditorGlobalVariableCommands.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorComponentManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/Resource/ResourceManager.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		const utf8* propLabel( const PropertyInfo& prop )
		{
			if ( prop._metadata._displayName.empty() == false )
				return prop._metadata._displayName.c_str();
			if ( prop._listAlias.empty() == false && prop._listAlias.front().empty() == false )
				return prop._listAlias.front().c_str();
			return prop._name.c_str();
		}

		bool isSupportedMethodArgType( string_view typeName )
		{
			hashed_string hashedName( typeName );
			return hashedName.isPredefinedType( PredefinedNameType::NameType_int32 ) || hashedName.isPredefinedType( PredefinedNameType::NameType_int64 ) ||
				   hashedName.isPredefinedType( PredefinedNameType::NameType_float32 ) || hashedName.isPredefinedType( PredefinedNameType::NameType_bool ) ||
				   hashedName.isPredefinedType( PredefinedNameType::NameType_string );
		}

		bool formatTaskValue( const TaskValue& value, string_view returnType, utf8* pOutBuf, size_t outSize )
		{
			if ( pOutBuf == nullptr || outSize == 0 )
				return false;
			const uint32  cap	   = static_cast<uint32>( outSize );
			TypeRegistry& registry = *editor::getService<TypeRegistry>();

			if ( returnType.empty() || returnType == "void" || value.hasValue() == false )
			{
				formatstring( pOutBuf, cap, "(void / empty)" );
				return true;
			}
			if ( registry.isType( returnType, "int32" ) )
			{
				formatstring( pOutBuf, cap, "%#", value.getValue<int32>() );
				return true;
			}
			if ( registry.isType( returnType, "int64" ) )
			{
				formatstring( pOutBuf, cap, "%#", value.getValue<int64>() );
				return true;
			}
			if ( registry.isType( returnType, "float32" ) )
			{
				formatstring( pOutBuf, cap, "%#", static_cast<float64>( value.getValue<float32>() ) );
				return true;
			}
			if ( registry.isType( returnType, "float64" ) )
			{
				formatstring( pOutBuf, cap, "%#", value.getValue<float64>() );
				return true;
			}
			if ( registry.isType( returnType, "bool" ) )
			{
				formatstring( pOutBuf, cap, "%#", value.getValue<bool>() ? "true" : "false" );
				return true;
			}
			if ( hashed_string( returnType ).isPredefinedType( PredefinedNameType::NameType_string ) )
			{
				formatstring( pOutBuf, cap, "%#", value.getValue<string>().c_str() );
				return true;
			}

			formatstring( pOutBuf, cap, "(unsupported return: %#)",
						  string( returnType ).c_str() );
			return false;
		}

	} // namespace

	void InspectorPanel::drawContent()
	{
		editor::pushInspectorStyle();
		drawSelectionSection();

		if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) )
		{
			const ImGuiIO& io = ImGui::GetIO();
			if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_Z, false ) )
			{
				if ( editor::getService<CommandStack>()->canUndo() )
					editor::getService<CommandStack>()->undo();
			}
			else if ( io.KeyCtrl && ( ImGui::IsKeyPressed( ImGuiKey_Y, false ) || ( io.KeyShift && ImGui::IsKeyPressed( ImGuiKey_Z, false ) ) ) )
			{
				if ( editor::getService<CommandStack>()->canRedo() )
					editor::getService<CommandStack>()->redo();
			}
		}

		editor::popInspectorStyle();
	}

	void InspectorPanel::drawSelectionSection()
	{
		editor::drawSectionHeader( "Selection" );

		const size_t selCount = EditorContext::get()->getSelectionManager().getSelectedObjectCount();
		if ( selCount > 1 )
		{
			ImGui::TextColored( ImVec4{ 0.4f, 0.7f, 1.0f, 1.0f }, "Multi-Selection (%u objects)",
								static_cast<uint32>( selCount ) );
			ImGui::Separator();
		}

		EditorWorkspace& ws = EditorContext::get()->getWorkspace();
		if ( ws.getSelectedObjectId() == 0 )
		{
			editor::drawEmptyHint( "Nothing selected. Pick in Game View or use Hierarchy." );
			return;
		}

		Scene* pScene = editor::getService<SceneManager>()->getActiveScene();
		if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
		{
			editor::drawEmptyHint( "No active scene." );
			return;
		}

		GameObject* pObj = pScene->getObjectManager()->findGameObjectById( ws.getSelectedObjectId() );
		if ( pObj == nullptr )
		{
			ImGui::TextDisabled( "Selected object no longer exists." );
			ws.clearSelection();
			return;
		}

		drawGameObjectHeader( pObj );

		const string& pfbPath = ws.getGameObjectPrefabPath( pObj->getObjectId() );
		if ( pfbPath.empty() == false )
		{
			editor::drawChip( "Prefab", editor::style::kAccent );
			ImGui::SameLine();
			ImGui::TextDisabled( "%s", pfbPath.c_str() );

			if ( ImGui::Button( "Apply to Prefab" ) )
			{
				PrefabAsset asset;
				asset.setFromGameObject( pObj );
				if ( asset.saveToXmlFile( pfbPath ) )
				{
					SW_LOG_INFO( "Saved prefab changes to %#", pfbPath.c_str() );
				}
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Revert to Prefab" ) )
			{
				PrefabAsset* pLoaded = editor::getService<ResourceManager>()->getPrefabManager().loadPrefab( pfbPath );
				if ( pLoaded != nullptr && pLoaded->isValid() )
				{
					const string beforeXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pObj } );
					ObjectStateSerializer::loadFromXmlString( pObj, pLoaded->getStateData() );
					pObj->applyLoadedHierarchy();
					const string afterXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pObj } );
					EditorTransaction::recordModify( GameObjectPtr{ pObj }, beforeXml, afterXml, "Revert to Prefab" );
				}
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Unlink" ) )
			{
				ws.setGameObjectPrefabPath( pObj->getObjectId(), "" );
			}
			ImGui::Separator();
		}

		editor::drawSearchField( "##propFilter", _arrPropertyFilter, sizeof( _arrPropertyFilter ), "Search properties..." );
		ImGui::Spacing();

		const TypeInfo* pTypeInfo = pObj->getTypeInfo();
		if ( pTypeInfo != nullptr )
		{
			ImGui::SeparatorText( "Reflected Properties" );
			drawTypeProperties( pObj, pTypeInfo );
			ImGui::SeparatorText( "Methods" );
			drawTypeMethods( pObj, pTypeInfo );
		}

		ImGui::SeparatorText( "Components" );
		EditorContext*			  pSelEditorContext = EditorContext::get();
		IRHIDevice*				  pRhiDevice		= ( pSelEditorContext != nullptr ) ? pSelEditorContext->getRhiDevice() : nullptr;
		const vector<Component*>& listComponents	= pObj->getAllComponents();
		for ( Component* pComp : listComponents )
		{
			if ( pComp == nullptr )
				continue;

			const utf8* pName	= pComp->getComponentName().empty() == false ? pComp->getComponentName().c_str() : "Component";
			bool		bActive = pComp->isActive();
			bool		bRemove{ false };
			const bool	bAccent	  = isA<SceneComponent>( pComp );
			const bool	bScrollTo = ( ws.getScrollToComponentId() != 0 &&
									  ws.getScrollToComponentId() == pComp->getComponentId() );

			if ( bScrollTo )
			{
				ImGui::SetNextItemOpen( true );
				ws.setScrollToComponentId( 0 );
			}

			if ( editor::beginComponentCard( pName, pComp->getComponentId(), &bActive, &bRemove, bAccent ) )
			{
				if ( bScrollTo )
					ImGui::SetScrollHereY( 0.25f );
				pComp->setActive( bActive );

				if ( ImGui::BeginPopupContextItem( "CompCardCtx" ) )
				{
					const TypeInfo* pTInfo = pComp->getTypeInfo();
					if ( pTInfo != nullptr )
					{
						if ( ImGui::MenuItem( "Copy Component" ) )
						{
							ws.copyComponent( pComp );
						}
						const string compTypeName = pComp->getComponentName().empty() == false
													  ? pComp->getComponentName().c_str()
													  : pTInfo->_name.c_str();
						const bool	 bCanPaste	  = ( ws.hasCopiedComponent() &&
													  ws.getCopiedComponentTypeName() == compTypeName );
						if ( bCanPaste )
						{
							if ( ImGui::MenuItem( "Paste Component Values" ) )
							{
								ws.pasteComponentValues( pComp );
							}
						}
						else
						{
							ImGui::BeginDisabled();
							ImGui::MenuItem( "Paste Component Values" );
							ImGui::EndDisabled();
						}

						if ( ws.hasCopiedComponent() )
						{
							if ( ImGui::MenuItem( "Paste as New Component" ) )
							{
								ws.pasteComponentAsNew( pObj );
							}
						}

						ImGui::Separator();
						if ( ImGui::BeginMenu( "Presets" ) )
						{
							static utf8 s_presetNameBuf[64]{ 0 };
							ImGui::InputTextWithHint( "##presetName", "Preset Name...", s_presetNameBuf,
													  sizeof( s_presetNameBuf ) );
							ImGui::SameLine();
							if ( ImGui::Button( "Save" ) && s_presetNameBuf[0] != '\0' )
							{
								ws.saveComponentPreset( pComp, s_presetNameBuf );
								s_presetNameBuf[0] = '\0';
							}
							ImGui::Separator();

							// List saved presets
							vector<string> listPresetFiles;
							EditorGlobalVariableCommands::collectComponentPresetFiles( listPresetFiles );
							const string compPrefix = compTypeName + "_";
							bool		 bFoundPresets{ false };
							for ( const string& presetFile : listPresetFiles )
							{
								const string fname = FileUtil::getFileNamePart( presetFile );
								if ( fname.rfind( compPrefix, 0 ) == 0 )
								{
									bFoundPresets		 = true;
									string displayPreset = fname.substr( compPrefix.size() );
									if ( displayPreset.size() > 11 &&
										 displayPreset.substr( displayPreset.size() - 11 ) == ".preset.xml" )
									{
										displayPreset = displayPreset.substr( 0, displayPreset.size() - 11 );
									}
									if ( ImGui::MenuItem( displayPreset.c_str() ) )
									{
										ws.loadComponentPreset( pComp, presetFile );
									}
								}
							}
							if ( bFoundPresets == false )
								ImGui::TextDisabled( "No saved presets." );

							ImGui::EndMenu();
						}
					}
					ImGui::Separator();
					if ( ImGui::MenuItem( "Remove Component" ) )
						bRemove = true;
					ImGui::EndPopup();
				}

				drawComponentSection( pComp, pRhiDevice );
				editor::endComponentCard();
			}
			else
				pComp->setActive( bActive );

			if ( bRemove )
			{
				GameObjectManager* pGameObjectManager = pObj->getManager();
				if ( pGameObjectManager != nullptr )
					pGameObjectManager->destroyComponent( pComp );
				else
					pObj->removeComponent( pComp );
				break;
			}
		}
	}

	void InspectorPanel::drawGameObjectHeader( GameObject* pObj )
	{
		ImGui::Text( "GameObject  ID: %llu", static_cast<uint64>( pObj->getObjectId() ) );

		utf8 nameBuf[constant::kMaxBuffer256];
		formatstring( nameBuf, sizeof( nameBuf ), "%#", pObj->getName().c_str() );
		if ( ImGui::InputText( "Name", nameBuf, sizeof( nameBuf ), ImGuiInputTextFlags_EnterReturnsTrue ) )
			pObj->setName( hashed_string( nameBuf ) );

		bool bActive = pObj->isActive();
		if ( ImGui::Checkbox( "Active", &bActive ) )
			pObj->setActive( bActive );

		GameObject* pParent = pObj->getParent();
		if ( pParent != nullptr )
		{
			ImGui::Text( "Parent: %s", pParent->getName().c_str() );
			ImGui::SameLine();
			if ( ImGui::SmallButton( "Unparent" ) )
				pObj->detachFromParent();
		}
		else
			ImGui::TextDisabled( "Parent: (root)" );

		const vector<TagID>& listTags = pObj->getTags().getTags();
		if ( listTags.empty() == false )
		{
			ImGui::TextUnformatted( "Tags:" );
			for ( const TagID& tag : listTags )
			{
				if ( tag._pString != nullptr && tag._pString[0] != '\0' )
				{
					ImGui::SameLine();
					editor::drawChip( tag._pString, editor::style::kOk );
				}
			}
		}
	}

	void InspectorPanel::drawComponentSection( Component* pComp, IRHIDevice* pRhiDevice )
	{
		ImGui::TextDisabled( "ID: %llu", static_cast<uint64>( pComp->getComponentId() ) );

		const TypeInfo*		 pTypeInfo	= pComp->getTypeInfo();
		IInspectorComponent* pInspector = ( pTypeInfo != nullptr )
											? EditorContext::get()->getInspectorComponentManager().find( pTypeInfo->_name.c_str() )
											: nullptr;

		if ( pInspector != nullptr )
			pInspector->drawHeader( pComp );

		bool bHandledByCustomBody{ false };
		if ( pInspector != nullptr )
			bHandledByCustomBody = pInspector->drawBody( pComp, pRhiDevice );

		if ( bHandledByCustomBody == false )
		{
			if ( pTypeInfo != nullptr )
			{
				ImGui::SeparatorText( "Properties" );
				drawTypeProperties( pComp, pTypeInfo );
				ImGui::SeparatorText( "Methods" );
				drawTypeMethods( pComp, pTypeInfo );
			}
			else
				ImGui::TextDisabled( "No TypeInfo registered for this component." );
		}

		if ( pInspector != nullptr )
			pInspector->drawFooter( pComp, pRhiDevice );
	}

	void InspectorPanel::drawTypeProperties( void* pInstance, const TypeInfo* pTypeInfo )
	{
		if ( pInstance == nullptr || pTypeInfo == nullptr )
			return;

		map<string, vector<const PropertyInfo*>> grouped;
		const bool								 bHasFilter = ( _arrPropertyFilter[0] != '\0' );

		pTypeInfo->forEachProperty( [&]( const PropertyInfo& prop )
		{
			if ( prop._metadata._bHideInInspector == SW_TRUE )
				return;

			if ( bHasFilter )
			{
				const utf8* pLabelName = propLabel( prop );
				if ( StringUtil::stristr( prop._name.c_str(), _arrPropertyFilter ) == nullptr &&
					 StringUtil::stristr( pLabelName, _arrPropertyFilter ) == nullptr &&
					 StringUtil::stristr( prop._metadata._category.c_str(), _arrPropertyFilter ) == nullptr )
				{
					return;
				}
			}

			const string category =
				prop._metadata._category.empty() ? "General" : string( prop._metadata._category.c_str() );
			grouped[category].push_back( &prop );
		} );

		for ( const auto& [category, props] : grouped )
		{
			if ( ImGui::CollapsingHeader( category.c_str(), ImGuiTreeNodeFlags_DefaultOpen ) == false )
				continue;

			if ( ImGui::BeginTable( category.c_str(), 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg ) )
			{
				ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthFixed, 150.0f );
				ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthStretch );

				for ( const PropertyInfo* prop : props )
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();

					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted( propLabel( *prop ) );
					if ( prop->_metadata._tooltip.empty() == false && ImGui::IsItemHovered() )
						ImGui::SetTooltip( "%s", prop->_metadata._tooltip.c_str() );

					if ( ImGui::BeginPopupContextItem( "PropCtx" ) )
					{
						if ( prop->_metadata._defaultValue.empty() == false )
						{
							utf8 resetLabel[constant::kMaxBuffer128];
							formatstring( resetLabel, sizeof( resetLabel ), "Reset to Default (%#)", prop->_metadata._defaultValue.c_str() );
							if ( ImGui::MenuItem( resetLabel ) )
							{
								utf8 jsonWrap[constant::kMaxBuffer256];
								formatstring( jsonWrap, sizeof( jsonWrap ), "{\"%#\":%#}", prop->_name.c_str(), prop->_metadata._defaultValue.c_str() );
								JsonSerializer::deserialize( pInstance, *pTypeInfo, jsonWrap );
							}
						}
						if ( ImGui::MenuItem( "Copy Property Name" ) )
							ImGui::SetClipboardText( prop->_name.c_str() );
						ImGui::EndPopup();
					}

					if ( prop->_metadata._bTransient == SW_TRUE )
					{
						ImGui::SameLine();
						ImGui::TextDisabled( "(T)" );
						if ( ImGui::IsItemHovered() )
							ImGui::SetTooltip( "Transient property: not saved to disk" );
					}

					ImGui::TableNextColumn();
					ImGui::PushID( prop->_name.c_str() );
					ImGui::SetNextItemWidth( -FLT_MIN );
					drawPropertyWidget( pInstance, *prop );
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		}
	}

	void InspectorPanel::drawPropertyWidget( void* pInstance, const PropertyInfo& prop )
	{
		IInspectorProperty* pProperty = EditorContext::get()->getInspectorPropertyManager().find( prop._typeName.c_str() );
		if ( pProperty != nullptr )
		{
			if ( pProperty->draw( pInstance, prop ) )
				return;
		}

		const utf8* pLabel	  = "##value";
		const bool	bReadOnly = prop._metadata._bReadOnly != 0;

		auto*			pRegistry = editor::getService<TypeRegistry>();
		const EnumInfo* pEnumInfo = pRegistry->findEnum( prop._typeName );
		if ( pEnumInfo != nullptr )
		{
			int32* pEnumValue = prop.getValuePtr<int32>( pInstance );
			if ( pEnumValue == nullptr )
				return;

			if ( pEnumInfo->_bIsBitFlag )
			{
				string previewStr;
				for ( const auto& [val, nameHashed] : pEnumInfo->_mapValueToName )
				{
					const int32 val32 = static_cast<int32>( val );
					if ( val32 != 0 && ( *pEnumValue & val32 ) == val32 )
					{
						if ( previewStr.empty() == false )
							previewStr += " | ";
						previewStr += nameHashed.c_str();
					}
				}
				if ( previewStr.empty() )
					previewStr = ( *pEnumValue == 0 ) ? "None" : "<Unknown>";

				if ( bReadOnly )
				{
					ImGui::TextDisabled( "%s", pLabel );
					ImGui::SameLine();
					ImGui::TextUnformatted( previewStr.c_str() );
					return;
				}

				if ( ImGui::BeginCombo( pLabel, previewStr.c_str() ) )
				{
					if ( ImGui::SmallButton( "Select All" ) )
					{
						for ( const auto& [val, _] : pEnumInfo->_mapValueToName )
							*pEnumValue |= static_cast<int32>( val );
					}
					ImGui::SameLine();
					if ( ImGui::SmallButton( "Clear All" ) )
					{
						*pEnumValue = 0;
					}
					ImGui::Separator();

					for ( const auto& [val, nameHashed] : pEnumInfo->_mapValueToName )
					{
						const int32 val32 = static_cast<int32>( val );
						if ( val32 == 0 )
							continue;
						bool bChecked = ( ( *pEnumValue & val32 ) == val32 );
						if ( ImGui::Checkbox( nameHashed.c_str(), &bChecked ) )
						{
							if ( bChecked )
								*pEnumValue |= val32;
							else
								*pEnumValue &= ~val32;
						}
					}
					ImGui::EndCombo();
				}
				InspectorPropertyUndo::trackPod( pEnumValue, sizeof( *pEnumValue ), pLabel );
				return;
			}

			const utf8* pName = pRegistry->enumToString( prop._typeName, *pEnumValue );
			if ( bReadOnly )
			{
				ImGui::TextDisabled( "%s", pLabel );
				ImGui::SameLine();
				ImGui::TextUnformatted( pName != nullptr ? pName : "<Unknown>" );
				return;
			}

			const utf8* pPreview = ( pName != nullptr ) ? pName : "<Unknown>";

			if ( ImGui::BeginCombo( pLabel, pPreview ) )
			{
				for ( const auto& [val, nameHashed] : pEnumInfo->_mapValueToName )
				{
					const int32 val32	  = static_cast<int32>( val );
					const utf8* name	  = nameHashed.c_str();
					bool		bSelected = ( val32 == *pEnumValue );
					if ( ImGui::Selectable( name, bSelected ) )
						*pEnumValue = val32;
					if ( bSelected )
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			InspectorPropertyUndo::trackPod( pEnumValue, sizeof( *pEnumValue ), pLabel );
			return;
		}

		if ( prop._bIsContainer && prop._containerWrapper != nullptr )
		{
			void* pContainer = prop.getRawPtr( pInstance );
			if ( pContainer != nullptr )
			{
				ISequenceContainerWrapper* pSeq = prop._containerWrapper->asSequence();
				if ( pSeq != nullptr )
				{
					const size_t count = pSeq->getSize( pContainer );
					utf8		 headerBuf[constant::kMaxBuffer128];
					formatstring( headerBuf, sizeof( headerBuf ), "[%#] (%# elements)", prop._elementTypeName.c_str(), count );

					if ( ImGui::TreeNodeEx( pLabel, ImGuiTreeNodeFlags_SpanFullWidth, "%s", headerBuf ) )
					{
						if ( bReadOnly == false )
						{
							if ( ImGui::SmallButton( "+ Add" ) )
								pSeq->addElementDefault( pContainer );
							ImGui::SameLine();
							if ( ImGui::SmallButton( "Clear" ) )
								pSeq->clear( pContainer );
							ImGui::Separator();
						}

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

							PropertyInfo elemProp{};
							elemProp._typeName = prop._elementTypeName;
							elemProp._name	   = prop._name;
							elemProp._metadata = prop._metadata;

							ImGui::SetNextItemWidth( -FLT_MIN );
							drawPropertyWidget( pElem, elemProp );
							ImGui::PopID();
						}
						ImGui::TreePop();
					}
					return;
				}
			}
		}

		const TypeInfo* pFieldType = pRegistry->findType( prop._typeName );
		if ( pFieldType != nullptr || prop._typeName.isPredefinedType( PredefinedNameType::NameType_string ) )
		{
			void* pNestedPtr = prop.getRawPtr( pInstance );
			if ( pNestedPtr == nullptr )
				return;

			ImGui::PushStyleColor( ImGuiCol_Header, ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f } );
			bool bNodeOpen = ImGui::TreeNodeEx( pLabel, ImGuiTreeNodeFlags_SpanFullWidth, "[%s]", prop._typeName.c_str() );
			ImGui::PopStyleColor();

			if ( bNodeOpen )
			{
				pFieldType->forEachProperty( [&]( const PropertyInfo& nestedProp )
				{
					ImGui::PushID( nestedProp._name.c_str() );
					ImGui::AlignTextToFramePadding();
					ImGui::BulletText( "%s", propLabel( nestedProp ) );
					ImGui::SameLine();
					ImGui::SetNextItemWidth( -FLT_MIN );
					drawPropertyWidget( pNestedPtr, nestedProp );
					ImGui::PopID();
				} );
				ImGui::TreePop();
			}
			return;
		}

		ImGui::TextDisabled( "No inspector for %s", prop._typeName.c_str() );
	}

	void InspectorPanel::drawTypeMethods( void* pInstance, const TypeInfo* pTypeInfo )
	{
		if ( pInstance == nullptr || pTypeInfo == nullptr || pTypeInfo->_listMethod.empty() )
		{
			ImGui::TextDisabled( "No FUNCTION() methods." );
			return;
		}

		if ( _arrLastInvokeResult[0] != '\0' )
			ImGui::TextDisabled( "Last result: %s", _arrLastInvokeResult );

		for ( const FunctionInfo& method : pTypeInfo->_listMethod )
		{
			// 생성자 인보커는 raw 스토리지(placement-new)를 기대합니다 — 살아있는 인스펙터 인스턴스에는 안전하지 않습니다.
			if ( method._metadata._bConstructor != 0 )
				continue;

			const utf8* pLabelName = method._name.c_str();
			if ( method._metadata._displayName.empty() == false )
				pLabelName = method._metadata._displayName.c_str();

			const uint32 paramCount = static_cast<uint32>( method._listParamTypeName.size() );

			if ( method._metadata._bCallInEditor != 0 && paramCount == 0 )
			{
				ImGui::PushID( method._hashName.c_str() );
				ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ 0.18f, 0.42f, 0.65f, 1.0f } );
				ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4{ 0.25f, 0.52f, 0.78f, 1.0f } );
				ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4{ 0.12f, 0.35f, 0.55f, 1.0f } );

				utf8 buttonLabel[constant::kMaxBuffer128];
				formatstring( buttonLabel, sizeof( buttonLabel ), "Run %#", pLabelName );
				if ( ImGui::Button( buttonLabel, ImVec2{ -FLT_MIN, 0.0f } ) )
				{
					TaskArgs		args;
					const TaskValue result = editor::getService<TypeRegistry>()->invokeMethod(
						pInstance, pTypeInfo->_fullyQualifiedName, method._hashName, args );
					formatTaskValue( result, method._returnTypeName, _arrLastInvokeResult, sizeof( _arrLastInvokeResult ) );
				}
				ImGui::PopStyleColor( 3 );
				if ( method._metadata._tooltip.empty() == false && ImGui::IsItemHovered() )
					ImGui::SetTooltip( "%s", method._metadata._tooltip.c_str() );
				ImGui::PopID();
				continue;
			}

			ImGui::PushID( method._hashName.c_str() );
			ImGui::Text( "%s (%s)", pLabelName,
						 method._returnTypeName.empty() ? "?" : method._returnTypeName.c_str() );
			if ( method._metadata._bCallInEditor != 0 )
			{
				ImGui::SameLine();
				ImGui::TextColored( ImVec4{ 0.3f, 0.8f, 1.0f, 1.0f }, "[Editor]" );
			}
			if ( method._metadata._tooltip.empty() == false && ImGui::IsItemHovered() )
				ImGui::SetTooltip( "%s", method._metadata._tooltip.c_str() );
			bool bArgsOk{ true };
			for ( uint32 paramIndex = 0; paramIndex < paramCount; ++paramIndex )
			{
				if ( paramIndex >= 8 || isSupportedMethodArgType( method._listParamTypeName[paramIndex] ) == false )
				{
					bArgsOk = false;
					break;
				}
			}

			for ( uint32 paramIndex = 0; paramIndex < paramCount && paramIndex < 8; ++paramIndex )
			{
				ImGui::PushID( static_cast<int32>( paramIndex ) );
				const string& p = method._listParamTypeName[paramIndex];
				utf8		  label[64];
				formatstring( label, sizeof( label ), "arg%# (%#)", paramIndex, p.c_str() );

				TypeRegistry& registry = *editor::getService<TypeRegistry>();
				if ( registry.isType( p, "int32" ) || registry.isType( p, "int64" ) )
					ImGui::InputInt( label, &_arrArgInt[paramIndex] );
				else if ( registry.isType( p, "float32" ) )
					ImGui::DragFloat( label, &_arrArgFloat[paramIndex], 0.1f );
				else if ( registry.isType( p, "bool" ) )
					ImGui::Checkbox( label, &_arrArgBool[paramIndex] );
				else if ( hashed_string( p ).isPredefinedType( PredefinedNameType::NameType_string ) )
					ImGui::InputText( label, _arrArgString[paramIndex], sizeof( _arrArgString[paramIndex] ) );
				else
					ImGui::TextDisabled( "%s (unsupported in UI)", label );

				ImGui::PopID();
			}

			if ( paramCount > 8 )
				ImGui::TextDisabled( "Too many arguments (max 8 in UI)." );

			if ( bArgsOk == false )
			{
				ImGui::BeginDisabled();
				ImGui::Button( "Invoke" );
				ImGui::EndDisabled();
				ImGui::TextDisabled( "Unsupported FUNCTION args ??invoke skipped." );
			}
			else if ( ImGui::Button( "Invoke" ) )
			{
				TaskArgs args;
				for ( uint32 paramIndex = 0; paramIndex < paramCount && paramIndex < 8; ++paramIndex )
				{
					const string& p		   = method._listParamTypeName[paramIndex];
					TypeRegistry& registry = *editor::getService<TypeRegistry>();
					if ( registry.isType( p, "int32" ) )
						args.add( int32{ _arrArgInt[paramIndex] } );
					else if ( registry.isType( p, "int64" ) )
						args.add( int64{ _arrArgInt[paramIndex] } );
					else if ( registry.isType( p, "float32" ) )
						args.add( _arrArgFloat[paramIndex] );
					else if ( registry.isType( p, "bool" ) )
						args.add( _arrArgBool[paramIndex] );
					else if ( hashed_string( p ).isPredefinedType( PredefinedNameType::NameType_string ) )
						args.add( string( _arrArgString[paramIndex] ) );
				}

				const TaskValue result = editor::getService<TypeRegistry>()->invokeMethod(
					pInstance, pTypeInfo->_fullyQualifiedName, method._hashName, args );
				formatTaskValue( result, method._returnTypeName, _arrLastInvokeResult, sizeof( _arrLastInvokeResult ) );
			}

			ImGui::PopID();
		}
	}
} // namespace sw::editor
