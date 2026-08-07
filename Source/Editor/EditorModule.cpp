/**
 * @file EditorModule.cpp
 * @brief EditorModule DLL 엔트리포인트 및 Runtime EditorAPI 브릿지
 */
#include "pch.h"
#include "Runtime/EditorAPI.h"
#include "ImGuiEditor.h"
#include "Core/Common/CoreServices.h"
#include "Core/Utility/GlobalVariable/GlobalVariableManager.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Window/IWindow.h"
#include "Core/Graphics/RHI/IRHIDevice.h"

namespace
{
	sw::EditorHandle EditorAPI_Create()
	{
		sw::getGlobalVariableManager().registerPendingVariables(
			"EditorModule", sw::GlobalVariableRegistrar::getHead() );
		sw::getTypeRegistry().registerPendingTypes(
			"EditorModule", sw::TypeRegistrar::getHead(), sw::EnumRegistrar::getHead() );
		return static_cast<sw::EditorHandle>( new sw::ImGuiEditor() );
	}

	void EditorAPI_Destroy( sw::EditorHandle editor )
	{
		auto* pEditor = static_cast<sw::IEditor*>( editor );
		if ( pEditor != nullptr )
			delete pEditor;

		sw::getGlobalVariableManager().unregisterVariablesByModule( "EditorModule" );
		sw::getTypeRegistry().unregisterTypesByModule( "EditorModule" );
	}

	bool EditorAPI_Initialize( sw::EditorHandle editor, sw::WindowHandle window, sw::RHIDeviceHandle rhiDevice )
	{
		auto* pEditor = static_cast<sw::IEditor*>( editor );
		if ( pEditor == nullptr )
			return false;
		return pEditor->initialize( static_cast<sw::IWindow*>( window ), static_cast<sw::IRHIDevice*>( rhiDevice ) );
	}

	void EditorAPI_Shutdown( sw::EditorHandle editor )
	{
		auto* pEditor = static_cast<sw::IEditor*>( editor );
		if ( pEditor != nullptr )
			pEditor->shutdown();
	}

	void EditorAPI_PreRender( sw::EditorHandle editor, sw::RHIDeviceHandle rhiDevice )
	{
		auto* pEditor = static_cast<sw::IEditor*>( editor );
		if ( pEditor != nullptr )
			pEditor->preRender( static_cast<sw::IRHIDevice*>( rhiDevice ) );
	}

	void EditorAPI_Render( sw::EditorHandle editor, const sw::EditorUIContext* context )
	{
		auto* pEditor = static_cast<sw::IEditor*>( editor );
		if ( pEditor != nullptr && context != nullptr )
			pEditor->render( *context );
	}

	bool EditorAPI_ProcessEvent( sw::EditorHandle editor, const sw::NativeWindowEvent* event )
	{
		auto* pEditor = static_cast<sw::IEditor*>( editor );
		if ( pEditor == nullptr || event == nullptr )
			return false;
		return pEditor->processEvent( *event );
	}

	void* EditorAPI_RegisterTexture( sw::EditorHandle editor, sw::TextureHandle texture )
	{
		auto* pEditor = static_cast<sw::IEditor*>( editor );
		if ( pEditor == nullptr )
			return nullptr;
		return pEditor->registerTexture( static_cast<sw::RHITextureHandle>( texture ) );
	}
} // namespace

extern "C"
{
	SW_MODULE_API bool fillEditorAPI( sw::EditorAPI* outApi )
	{
		if ( outApi == nullptr )
			return false;

		outApi->create			= &EditorAPI_Create;
		outApi->destroy			= &EditorAPI_Destroy;
		outApi->initialize		= &EditorAPI_Initialize;
		outApi->shutdown		= &EditorAPI_Shutdown;
		outApi->preRender		= &EditorAPI_PreRender;
		outApi->render			= &EditorAPI_Render;
		outApi->processEvent	= &EditorAPI_ProcessEvent;
		outApi->registerTexture = &EditorAPI_RegisterTexture;
		return true;
	}
}
