#include "pch.h"

#include "Editor/Windows/InspectorWindow.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorConstants.h"
#include "Editor/Config/EditorData.h"
#include "Editor/Property/ComponentDrawerRegistry.h"
#include "Editor/Property/DefaultPropertyDrawers.h"
#include "Editor/Property/IComponentDrawer.h"
#include "Editor/Property/IPropertyDrawer.h"
#include "Editor/Property/PropertyDrawerHelper.h"
#include "Editor/Property/PropertyDrawerRegistry.h"
#include "Editor/Widgets/EditorWidgets.h"
#include "Editor/Workspace/EditorAssetDrop.h"
#include "Editor/Workspace/EditorWorkspace.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/Task/TaskTypes.h"

#include "RuntimeAPI/EditorService.h"
#include "RuntimeAPI/EditorUIContext.h"

#include <imgui.h>

namespace sw
{

	namespace
	{
		const utf8* propLabel( const PropertyInfo& prop )
		{
			if ( prop._metadata._displayName.empty() == false )
				return prop._metadata._displayName.c_str();
			if ( prop._listAliases.empty() == false && prop._listAliases.front().empty() == false )
				return prop._listAliases.front().c_str();
			return prop._name.c_str();
		}

		string defaultMaterialDir()
		{
			const string& mat	= editor::getEditorData()._defaultMaterial;
			const size_t  slash = mat.find_last_of( "/\\" );
			if ( slash == string::npos )
				return {};
			return mat.substr( 0, slash + 1 );
		}

		// trackPodPropertyUndo moved to DefaultPropertyDrawers.cpp
		// trackStringPropertyUndo moved to DefaultPropertyDrawers.cpp

		bool propertyNameHintsAsset( const PropertyInfo& prop, const utf8* pPath )
		{
			if ( pPath == nullptr || pPath[0] == '\0' )
				return false;

			auto containsCaseless = []( string_view hay, const utf8* pNeedle ) -> bool
			{
				const size_t nlen = StringUtil::strlen( pNeedle );
				if ( nlen == 0 )
					return true;
				if ( hay.size() < nlen )
					return false;
				for ( size_t matchIndex = 0; matchIndex <= hay.size() - nlen; ++matchIndex )
				{
					if ( StringUtil::strnicmp( hay.data() + matchIndex, pNeedle, static_cast<uint32>( nlen ) ) == 0 )
						return true;
				}
				return false;
			};

			auto aliasContains = [&]( const utf8* pNeedle ) -> bool
			{
				for ( const hashed_string& alias : prop._listAliases )
				{
					if ( alias.empty() )
						continue;
					if ( containsCaseless( alias.c_str(), pNeedle ) )
						return true;
				}
				return false;
			};

			const string_view name = prop._name.c_str();
			if ( editor::isTextureAssetPath( pPath ) )
			{
				return containsCaseless( name, "texture" ) || containsCaseless( name, "tex" ) ||
					   aliasContains( "texture" ) || aliasContains( "tex" );
			}
			if ( editor::isMaterialAssetPath( pPath ) )
				return containsCaseless( name, "material" ) || aliasContains( "material" );
			if ( editor::isPrefabAssetPath( pPath ) )
			{
				return containsCaseless( name, "prefab" ) || containsCaseless( name, "asset" ) ||
					   aliasContains( "prefab" ) || aliasContains( "asset" );
			}
			return false;
		}

		bool typeNameIs( const PropertyInfo& prop, const utf8* pCanonicalName )
		{
			return editor::getService<TypeRegistry>()->isType( prop._typeName, pCanonicalName );
		}

		bool isSupportedMethodArgType( string_view typeName )
		{
			TypeRegistry& registry = *editor::getService<TypeRegistry>();
			return registry.isType( typeName, "int32" ) || registry.isType( typeName, "int64" ) ||
				   registry.isType( typeName, "float32" ) || registry.isType( typeName, "bool" ) ||
				   registry.isType( typeName, "string" );
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
			if ( registry.isType( returnType, "string" ) )
			{
				formatstring( pOutBuf, cap, "%#", value.getValue<string>().c_str() );
				return true;
			}

			formatstring( pOutBuf, cap, "(unsupported return: %#)",
						  string( returnType ).c_str() );
			return false;
		}

	} // namespace

	void InspectorWindow::draw( const EditorUIContext& ctx )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		editor::pushInspectorStyle();
		drawSelectionSection( ctx );
		ImGui::Separator();
		if ( ImGui::CollapsingHeader( "Engine / Material", ImGuiTreeNodeFlags_DefaultOpen ) )
			drawEngineSection( ctx );

		if ( ImGui::CollapsingHeader( "Debug / Global Variables" ) )
		{
			GlobalVariableManager& gvm = *editor::getService<GlobalVariableManager>();
			ImGui::Text( "%zu variables registered", gvm.getAllVariables().size() );
			if ( ImGui::Button( "Open Global Variables Window" ) )
				EditorWorkspace::requestOpenWindow( "Global Variables" );
			ImGui::SameLine();
			if ( ImGui::Button( "Reset All Defaults" ) )
				gvm.resetAllToDefault();
		}

		ImGui::Separator();
		ImGui::TextUnformatted( "Asset Drop Target" );
		ImGui::TextDisabled( "Drag a path from Content Browser (SW_ASSET_PATH). Materials / textures / prefabs apply to matching selection properties." );
		if ( _lastDroppedAsset.empty() == false )
			ImGui::TextDisabled( "Last dropped: %s", _lastDroppedAsset.c_str() );
		ImGui::Button( "Drop asset path here", ImVec2( -1.0f, 36.0f ) );
		if ( ImGui::BeginDragDropTarget() )
		{
			const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload( "SW_ASSET_PATH" );
			if ( pPayload != nullptr )
			{
				const utf8* pPath = static_cast<const utf8*>( pPayload->Data );
				if ( pPath != nullptr )
					acceptAssetDrop( pPath );
			}
			ImGui::EndDragDropTarget();
		}

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
		ImGui::End();
	}

	void InspectorWindow::drawEngineSection( const EditorUIContext& ctx )
	{
		IRHIDevice* pRHIDevice = static_cast<IRHIDevice*>( ctx._pRHIDevice );
		if ( pRHIDevice != nullptr )
			ImGui::Text( "Active RHI Backend : %s", pRHIDevice->getBackendName() );

		GlobalVariableInfo* pGvSpeed = editor::getService<GlobalVariableManager>()->findVariable( string( editor::kGlobalVarPlayerSpeed ) );
		if ( pGvSpeed != nullptr )
		{
			float32* pSpeed = static_cast<float32*>( pGvSpeed->_pData );
			if ( pSpeed != nullptr )
				ImGui::SliderFloat( "Player Speed", pSpeed, 0.0f, 20.0f );
		}

		if ( ImGui::CollapsingHeader( "App Frame (Read Only)", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			ImGui::ColorEdit4( "Clear Color", ctx._arrClearColor );
		}

		Material* pMaterial = static_cast<Material*>( ctx._pMaterial );
		if ( pMaterial != nullptr )
		{
			renderMaterialUI( pMaterial, pRHIDevice );
			if ( ImGui::Button( "Save Material" ) )
			{
				const string dir = defaultMaterialDir();
				if ( dir.empty() )
					SW_LOG_WARNING( "[Inspector] Material save skipped — EditorData defaultMaterial is empty." );
				else
				{
					const string savePath = dir + pMaterial->getName() + string( editor::kMaterialExtension );
					if ( pMaterial->saveToFile( savePath ) )
						SW_LOG_INFO( "[Inspector] Saved material %#", savePath.c_str() );
					else
						SW_LOG_WARNING( "[Inspector] Material save failed: %#", savePath.c_str() );
				}
			}
		}

		if ( ImGui::Button( "Reset Engine Settings" ) )
		{
			const EditorData& data = editor::getEditorData();
			if ( pGvSpeed != nullptr )
			{
				float32* pSpeed = static_cast<float32*>( pGvSpeed->_pData );
				if ( pSpeed != nullptr )
					*pSpeed = data._playerSpeed;
			}

			if ( pMaterial != nullptr && pRHIDevice != nullptr )
			{
				const string& matPath = editor::getEditorData()._defaultMaterial;
				if ( matPath.empty() == false )
				{
					pMaterial->loadFromFile( matPath );
					pMaterial->setPropertyData( pRHIDevice, 0, static_cast<uint32>( pMaterial->getBuffer().size() ),
												pMaterial->getBuffer().data() );
				}
			}
		}
	}

	void InspectorWindow::drawSelectionSection( const EditorUIContext& ctx )
	{
		ImGui::TextUnformatted( "Selection" );
		ImGui::Separator();

		const size_t selCount = SelectionManager::getSelectedObjectCount();
		if ( selCount > 1 )
		{
			ImGui::TextColored( ImVec4{ 0.4f, 0.7f, 1.0f, 1.0f }, "Multi-Selection (%u objects)",
								static_cast<uint32>( selCount ) );
			ImGui::Separator();
		}

		if ( EditorWorkspace::selectedObjectId() == 0 )
		{
			ImGui::TextDisabled( "Nothing selected. Use Hierarchy." );
			return;
		}

		Scene* pScene = editor::getService<SceneManager>()->getActiveScene();
		if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
		{
			ImGui::TextDisabled( "No active scene." );
			return;
		}

		GameObject* pObj = pScene->getObjectManager()->findGameObjectById( EditorWorkspace::selectedObjectId() );
		if ( pObj == nullptr )
		{
			ImGui::TextDisabled( "Selected object no longer exists." );
			EditorWorkspace::clearSelection();
			return;
		}

		drawGameObjectHeader( pObj );

		const TypeInfo* pTypeInfo = pObj->getTypeInfo();
		if ( pTypeInfo != nullptr )
		{
			ImGui::SeparatorText( "Reflected Properties" );
			drawTypeProperties( pObj, pTypeInfo );
			ImGui::SeparatorText( "Methods" );
			drawTypeMethods( pObj, pTypeInfo );
		}

		ImGui::SeparatorText( "Components" );
		auto*					  pRhiDevice	 = static_cast<IRHIDevice*>( ctx._pRHIDevice );
		const vector<Component*>& listComponents = pObj->getAllComponents();
		for ( Component* pComp : listComponents )
		{
			if ( pComp == nullptr )
				continue;

			const utf8* pName	= pComp->getComponentName().empty() == false ? pComp->getComponentName().c_str() : "Component";
			bool		bActive = pComp->isActive();
			bool		bRemove{ false };
			const bool	bAccent	  = ( pComp->asSceneComponent() != nullptr );
			const bool	bScrollTo = ( EditorWorkspace::scrollToComponentId() != 0 &&
									  EditorWorkspace::scrollToComponentId() == pComp->getComponentId() );

			if ( bScrollTo )
			{
				ImGui::SetNextItemOpen( true );
				EditorWorkspace::scrollToComponentId() = 0;
			}

			if ( editor::beginComponentCard( pName, pComp->getComponentId(), &bActive, &bRemove, bAccent ) )
			{
				if ( bScrollTo )
					ImGui::SetScrollHereY( 0.25f );
				pComp->setActive( bActive );
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

		if ( ImGui::Button( "Open Bone Hierarchy" ) )
			EditorWorkspace::boneHierarchyPopupOpen() = true;
	}

	void InspectorWindow::drawGameObjectHeader( GameObject* pObj )
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
	}

	void InspectorWindow::drawComponentSection( Component* pComp, IRHIDevice* pRhiDevice )
	{
		ImGui::TextDisabled( "ID: %llu", static_cast<uint64>( pComp->getComponentId() ) );

		const TypeInfo*	  pTypeInfo = pComp->getTypeInfo();
		IComponentDrawer* pDrawer	= ( pTypeInfo != nullptr )
										? ComponentDrawerRegistry::getDrawer( pTypeInfo->_name.c_str() )
										: nullptr;

		if ( pDrawer != nullptr )
			pDrawer->drawHeader( pComp );

		bool bHandledByCustomBody{ false };
		if ( pDrawer != nullptr )
			bHandledByCustomBody = pDrawer->drawBody( pComp, pRhiDevice );

		if ( bHandledByCustomBody == false )
		{
			if ( pTypeInfo != nullptr )
			{
				ImGui::SeparatorText( "Properties" );
				drawTypeProperties( pComp, pTypeInfo );
				Component::EcsDataView ecsData = pComp->ensureEcsData();
				if ( ecsData.instance != nullptr && ecsData.typeInfo != nullptr && ecsData.instance != pComp )
					drawTypeProperties( ecsData.instance, ecsData.typeInfo );
				ImGui::SeparatorText( "Methods" );
				drawTypeMethods( pComp, pTypeInfo );
			}
			else
				ImGui::TextDisabled( "No TypeInfo registered for this component." );
		}

		if ( pDrawer != nullptr )
			pDrawer->drawFooter( pComp, pRhiDevice );
	}

	void InspectorWindow::drawTypeProperties( void* pInstance, const TypeInfo* pTypeInfo )
	{
		if ( pInstance == nullptr || pTypeInfo == nullptr )
			return;

		map<string, vector<const PropertyInfo*>> grouped;
		for ( const PropertyInfo& prop : pTypeInfo->getPropertiesWithBase() )
		{
			const string category =
				prop._metadata._category.empty() ? "General" : string( prop._metadata._category.c_str() );
			grouped[category].push_back( &prop );
		}

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

	void InspectorWindow::drawPropertyWidget( void* pInstance, const PropertyInfo& prop )
	{
		IPropertyDrawer* pDrawer = PropertyDrawerRegistry::getDrawer( prop._typeName.c_str() );
		if ( pDrawer != nullptr )
		{
			if ( pDrawer->draw( pInstance, prop ) )
				return;
		}

		const utf8* pLabel	  = "##value";
		const bool	bReadOnly = prop._metadata._bReadOnly != 0;

		auto*			pRegistry = editor::getService<TypeRegistry>();
		const EnumInfo* pEnumInfo = pRegistry->findEnum( prop._typeName );
		if ( pEnumInfo != nullptr )
		{
			int32*		pEnumValue = prop.getValuePtr<int32>( pInstance );
			const utf8* pName	   = pRegistry->enumToString( prop._typeName, *pEnumValue );
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
			trackPodPropertyUndo( pEnumValue, sizeof( *pEnumValue ), pLabel );
			return;
		}

		const TypeInfo* pFieldType = pRegistry->findType( prop._typeName );
		if ( pFieldType != nullptr )
		{
			void* pNestedPtr = prop.getValuePtr<void>( pInstance );
			if ( pNestedPtr == nullptr )
				return;

			ImGui::PushStyleColor( ImGuiCol_Header, ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f } );
			bool bNodeOpen = ImGui::TreeNodeEx( pLabel, ImGuiTreeNodeFlags_SpanFullWidth, "[%s]", prop._typeName.c_str() );
			ImGui::PopStyleColor();

			if ( bNodeOpen )
			{
				for ( const PropertyInfo& nestedProp : pFieldType->getPropertiesWithBase() )
				{
					ImGui::PushID( nestedProp._name.c_str() );
					ImGui::AlignTextToFramePadding();
					ImGui::BulletText( "%s", propLabel( nestedProp ) );
					ImGui::SameLine();
					ImGui::SetNextItemWidth( -FLT_MIN );
					drawPropertyWidget( pNestedPtr, nestedProp );
					ImGui::PopID();
				}
				ImGui::TreePop();
			}
			return;
		}

		ImGui::TextDisabled( "No PropertyDrawer for %s", prop._typeName.c_str() );
	}

	void InspectorWindow::drawTypeMethods( void* pInstance, const TypeInfo* pTypeInfo )
	{
		if ( pInstance == nullptr || pTypeInfo == nullptr || pTypeInfo->_listMethods.empty() )
		{
			ImGui::TextDisabled( "No FUNCTION() methods." );
			return;
		}

		if ( _arrLastInvokeResult[0] != '\0' )
			ImGui::TextDisabled( "Last result: %s", _arrLastInvokeResult );

		for ( const FunctionInfo& method : pTypeInfo->_listMethods )
		{
			// 생성자 인보커는 raw 스토리지(placement-new)를 기대합니다 — 살아있는 인스펙터 인스턴스에는 안전하지 않습니다.
			if ( method._metadata._bConstructor != 0 )
				continue;

			const utf8* pLabelName = method._name.c_str();
			if ( method._metadata._displayName.empty() == false )
				pLabelName = method._metadata._displayName.c_str();

			ImGui::PushID( method._hashName.c_str() );
			ImGui::Text( "%s (%s)", pLabelName,
						 method._returnTypeName.empty() ? "?" : method._returnTypeName.c_str() );
			if ( method._metadata._tooltip.empty() == false && ImGui::IsItemHovered() )
				ImGui::SetTooltip( "%s", method._metadata._tooltip.c_str() );

			const uint32 paramCount = static_cast<uint32>( method._listParamTypeNames.size() );
			bool		 bArgsOk{ true };
			for ( uint32 paramIndex = 0; paramIndex < paramCount; ++paramIndex )
			{
				if ( paramIndex >= 8 || isSupportedMethodArgType( method._listParamTypeNames[paramIndex] ) == false )
				{
					bArgsOk = false;
					break;
				}
			}

			for ( uint32 paramIndex = 0; paramIndex < paramCount && paramIndex < 8; ++paramIndex )
			{
				ImGui::PushID( static_cast<int32>( paramIndex ) );
				const string& p = method._listParamTypeNames[paramIndex];
				utf8		  label[64];
				formatstring( label, sizeof( label ), "arg%# (%#)", paramIndex, p.c_str() );

				TypeRegistry& registry = *editor::getService<TypeRegistry>();
				if ( registry.isType( p, "int32" ) || registry.isType( p, "int64" ) )
					ImGui::InputInt( label, &_arrArgInt[paramIndex] );
				else if ( registry.isType( p, "float32" ) )
					ImGui::DragFloat( label, &_arrArgFloat[paramIndex], 0.1f );
				else if ( registry.isType( p, "bool" ) )
					ImGui::Checkbox( label, &_arrArgBool[paramIndex] );
				else if ( registry.isType( p, "string" ) )
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
					const string& p		   = method._listParamTypeNames[paramIndex];
					TypeRegistry& registry = *editor::getService<TypeRegistry>();
					if ( registry.isType( p, "int32" ) )
						args.add( int32{ _arrArgInt[paramIndex] } );
					else if ( registry.isType( p, "int64" ) )
						args.add( int64{ _arrArgInt[paramIndex] } );
					else if ( registry.isType( p, "float32" ) )
						args.add( _arrArgFloat[paramIndex] );
					else if ( registry.isType( p, "bool" ) )
						args.add( _arrArgBool[paramIndex] );
					else if ( registry.isType( p, "string" ) )
						args.add( string( _arrArgString[paramIndex] ) );
				}

				const TaskValue result = editor::getService<TypeRegistry>()->invokeMethod(
					pInstance, pTypeInfo->_fullyQualifiedName, method._hashName, args );
				formatTaskValue( result, method._returnTypeName, _arrLastInvokeResult, sizeof( _arrLastInvokeResult ) );
			}

			ImGui::PopID();
		}
	}

	void InspectorWindow::renderMaterialUI( Material* pMaterial, IRHIDevice* pRHIDevice )
	{
		if ( pMaterial == nullptr || pRHIDevice == nullptr )
			return;

		ImGui::PushID( pMaterial );

		const vector<MaterialProperty>& props  = pMaterial->getProperties();
		const vector<uint8>&			buffer = pMaterial->getBuffer();

		bool		  bChanged{ false };
		vector<uint8> listTempBuffer = buffer;

		for ( const MaterialProperty& prop : props )
		{
			ImGui::PushID( prop._name.c_str() );

			if ( prop._type == MaterialPropertyType::Float )
			{
				float32* pPtr = reinterpret_cast<float32*>( listTempBuffer.data() + prop._offset );
				if ( ImGui::DragFloat( prop._name.c_str(), pPtr, 0.01f ) )
					bChanged = true;
			}
			else if ( prop._type == MaterialPropertyType::Float2 )
			{
				float32* pPtr = reinterpret_cast<float32*>( listTempBuffer.data() + prop._offset );
				if ( ImGui::DragFloat2( prop._name.c_str(), pPtr, 0.01f ) )
					bChanged = true;
			}
			else if ( prop._type == MaterialPropertyType::Float3 )
			{
				float32* pPtr = reinterpret_cast<float32*>( listTempBuffer.data() + prop._offset );
				if ( ImGui::DragFloat3( prop._name.c_str(), pPtr, 0.01f ) )
					bChanged = true;
			}
			else if ( prop._type == MaterialPropertyType::Float4 )
			{
				float32* pPtr = reinterpret_cast<float32*>( listTempBuffer.data() + prop._offset );
				if ( ImGui::ColorEdit4( prop._name.c_str(), pPtr ) )
					bChanged = true;
			}
			else if ( prop._type == MaterialPropertyType::Float4x4 )
			{
				float32* pPtr = reinterpret_cast<float32*>( listTempBuffer.data() + prop._offset );
				ImGui::Text( "%s", prop._name.c_str() );
				if ( ImGui::DragFloat4( "##r0", pPtr, 0.01f ) )
					bChanged = true;
				if ( ImGui::DragFloat4( "##r1", pPtr + 4, 0.01f ) )
					bChanged = true;
				if ( ImGui::DragFloat4( "##r2", pPtr + 8, 0.01f ) )
					bChanged = true;
				if ( ImGui::DragFloat4( "##r3", pPtr + 12, 0.01f ) )
					bChanged = true;
			}
			else if ( prop._type == MaterialPropertyType::Uint || prop._type == MaterialPropertyType::Int )
			{
				int32* pPtr = reinterpret_cast<int32*>( listTempBuffer.data() + prop._offset );
				if ( ImGui::InputInt( prop._name.c_str(), pPtr ) )
					bChanged = true;
			}
			else if ( prop._type == MaterialPropertyType::Uint2 || prop._type == MaterialPropertyType::Int2 )
			{
				int32* pPtr = reinterpret_cast<int32*>( listTempBuffer.data() + prop._offset );
				if ( ImGui::InputInt2( prop._name.c_str(), pPtr ) )
					bChanged = true;
			}
			else if ( prop._type == MaterialPropertyType::Uint3 || prop._type == MaterialPropertyType::Int3 )
			{
				int32* pPtr = reinterpret_cast<int32*>( listTempBuffer.data() + prop._offset );
				if ( ImGui::InputInt3( prop._name.c_str(), pPtr ) )
					bChanged = true;
			}
			else if ( prop._type == MaterialPropertyType::Uint4 || prop._type == MaterialPropertyType::Int4 )
			{
				int32* pPtr = reinterpret_cast<int32*>( listTempBuffer.data() + prop._offset );
				if ( ImGui::InputInt4( prop._name.c_str(), pPtr ) )
					bChanged = true;
			}

			ImGui::PopID();
		}

		if ( bChanged )
			pMaterial->setPropertyData( pRHIDevice, 0, static_cast<uint32>( listTempBuffer.size() ), listTempBuffer.data() );

		ImGui::PopID();
	}

	void InspectorWindow::acceptAssetDrop( const utf8* pPath )
	{
		if ( pPath == nullptr || pPath[0] == '\0' )
			return;

		setLastDroppedAsset( pPath );

		Scene* pScene = editor::getService<SceneManager>()->getActiveScene();
		if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
		{
			SW_LOG_INFO( "[Inspector] Stored dropped asset (no scene): %#", pPath );
			return;
		}

		GameObject* pObj = pScene->getObjectManager()->findGameObjectById( EditorWorkspace::selectedObjectId() );
		Component*	pComp{ nullptr };
		if ( pObj != nullptr && EditorWorkspace::selectedComponentId() != 0 )
		{
			for ( Component* pC : pObj->getAllComponents() )
			{
				if ( pC != nullptr && pC->getComponentId() == EditorWorkspace::selectedComponentId() )
				{
					pComp = pC;
					break;
				}
			}
		}

		auto tryAssign = [&]( void* pInstance, const TypeInfo* pTypeInfo ) -> bool
		{
			if ( pInstance == nullptr || pTypeInfo == nullptr )
				return false;

			bool bAssigned{ false };
			for ( const PropertyInfo& prop : pTypeInfo->_propertyList )
			{
				if ( prop._metadata._bReadOnly || prop._bIsContainer )
					continue;
				if ( typeNameIs( prop, "string" ) == false && typeNameIs( prop, "hashed_string" ) == false )
					continue;
				if ( propertyNameHintsAsset( prop, pPath ) == false )
					continue;

				if ( typeNameIs( prop, "string" ) )
				{
					string* pPtr = prop.getValuePtr<string>( pInstance );
					if ( pPtr != nullptr )
					{
						*pPtr	  = pPath;
						bAssigned = true;
					}
				}
				else if ( typeNameIs( prop, "hashed_string" ) )
				{
					hashed_string* pPtr = prop.getValuePtr<hashed_string>( pInstance );
					if ( pPtr != nullptr )
					{
						*pPtr	  = hashed_string( pPath );
						bAssigned = true;
					}
				}
			}
			return bAssigned;
		};

		bool bAssigned{ false };
		if ( pComp != nullptr )
		{
			const TypeInfo* pTypeInfo = pComp->getTypeInfo();
			if ( pTypeInfo != nullptr )
				bAssigned = tryAssign( pComp, pTypeInfo );
		}
		else if ( pObj != nullptr )
		{
			const TypeInfo* pTypeInfo = pObj->getTypeInfo();
			if ( pTypeInfo != nullptr )
				bAssigned = tryAssign( pObj, pTypeInfo );
		}

		if ( bAssigned )
			SW_LOG_INFO( "[Inspector] Assigned asset path to selection: %#", pPath );
		else
			SW_LOG_INFO( "[Inspector] Stored dropped asset (no matching property): %#", pPath );
	}

	void InspectorWindow::setLastDroppedAsset( const utf8* pPath )
	{
		_lastDroppedAsset = ( pPath != nullptr ) ? pPath : "";
	}
} // namespace sw
