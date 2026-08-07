#pragma once

/**
 * @file EditorAPI.h
 * @brief App ↔ EditorModule 통신용 함수 테이블 (IEditor 구현 세부사항 은닉)
 */

#include "Core/Common/CommonMacros.h"
#include "Core/Window/NativeWindowEvent.h"
#include "Runtime/EditorUIContext.h"
#include "Runtime/RuntimeHandles.h"
#include <cstdint>

namespace sw
{
	using EditorHandle	= void*;
	using TextureHandle = uint64_t;

	struct EditorAPI
	{
		EditorHandle ( *create )()																	= nullptr;
		void ( *destroy )( EditorHandle editor )													= nullptr;
		bool ( *initialize )( EditorHandle editor, WindowHandle window, RHIDeviceHandle rhiDevice ) = nullptr;
		void ( *shutdown )( EditorHandle editor )													= nullptr;
		void ( *preRender )( EditorHandle editor, RHIDeviceHandle rhiDevice )						= nullptr;
		void ( *render )( EditorHandle editor, const EditorUIContext* context )						= nullptr;
		bool ( *processEvent )( EditorHandle editor, const NativeWindowEvent* event )				= nullptr;
		void* ( *registerTexture )( EditorHandle editor, TextureHandle texture )					= nullptr;
	};

	/** @brief EditorModule이 export하는 API 테이블 채우기 심볼 이름: fillEditorAPI */
	using PFN_FillEditorAPI = bool ( * )( EditorAPI* outApi );
}

extern "C"
{
	SW_MODULE_API bool fillEditorAPI( sw::EditorAPI* outApi );
}
