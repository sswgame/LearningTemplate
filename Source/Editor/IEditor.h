#pragma once
/**
 * @file IEditor.h
 * @brief App↔Editor Runtime API에 대응하는 에디터 코어 인터페이스
 */
#include "Core/Graphics/RHI/RHITypes.h"
#include "Runtime/EditorUIContext.h"
#include "Core/Window/NativeWindowEvent.h"

namespace sw
{
	class IRHIDevice;
	class IWindow;

	/**
	 * @class IEditor
	 * @brief EditorAPI 함수 테이블이 위임하는 최소 표면 (위젯/도킹은 ImGuiEditor 내부)
	 */
	class IEditor
	{
	public:
		virtual ~IEditor() = default;

		virtual bool  initialize( IWindow* window, IRHIDevice* rhiDevice ) = 0;
		virtual void  shutdown()										  = 0;
		virtual void  preRender( IRHIDevice* rhiDevice )				  = 0;
		virtual void  render( const EditorUIContext& context )			  = 0;
		virtual bool  processEvent( const NativeWindowEvent& event )	  = 0;
		virtual void* registerTexture( RHITextureHandle texture )		  = 0;
	};
}
