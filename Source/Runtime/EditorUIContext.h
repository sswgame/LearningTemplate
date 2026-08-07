#pragma once

#include "Core/Graphics/RHI/RHITypes.h"

namespace sw
{
	class IRHIDevice;
	class Material;
	struct ShaderReflectionData;

	/** @brief App ↔ EditorModule 간 UI 렌더링에 필요한 런타임 컨텍스트 (헤더 결합 최소화) */
	struct EditorUIContext
	{
		float32*				 playerSpeed	 = nullptr;
		float32*				 clearColor		 = nullptr;
		Material*				 material		 = nullptr;
		ShaderReflectionData*	 reflectionData	 = nullptr;
		IRHIDevice*				 rhiDevice		 = nullptr;
		void*					 gameTextureID	 = nullptr;
	};
}
