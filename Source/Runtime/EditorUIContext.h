#pragma once
/**
 * @file EditorUIContext.h
 * @brief App ↔ EditorModule 간 UI 렌더링에 필요한 런타임 컨텍스트
 *
 * @note Dev-only shared Core types — NOT a C ABI / POD freeze.
 *       Pointers (Material*, IRHIDevice*, ShaderReflectionData*) are same-process Core
 *       objects owned by App; EditorModule is a MODULE in the same address space and may
 *       call through these types. Do not serialize across processes or treat as stable ABI.
 *       Opaque handles remain in RuntimeHandles.h for create/destroy API tables.
 */

#include "Core/Common/Types.h"

namespace sw
{
	class IRHIDevice;
	class Material;
	struct ShaderReflectionData;

	/** @brief App ↔ EditorModule 간 UI 렌더링에 필요한 런타임 컨텍스트 */
	struct EditorUIContext
	{
		float32*			  playerSpeed	 = nullptr;
		float32*			  clearColor	 = nullptr;
		Material*			  material		 = nullptr;
		ShaderReflectionData* reflectionData = nullptr;
		IRHIDevice*			  rhiDevice		 = nullptr;
		void*				  gameTextureID	 = nullptr;
	};
} // namespace sw
