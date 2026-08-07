#pragma once
/**
 * @file IEditorPanel.h
 * @brief Editor ImGui 패널 공통 인터페이스
 */

namespace sw
{
	class IRHIDevice;
	struct EditorUIContext;

	/** @brief 에디터 도킹 윈도우 한 칸을 그리는 패널 인터페이스 */
	class IEditorPanel
	{
	public:
		virtual ~IEditorPanel() = default;

		/** @brief ImGui 윈도우 제목 */
		virtual const char* getWindowTitle() const = 0;
		/** @brief 패널 UI를 그립니다. */
		virtual void		draw( const EditorUIContext& ctx ) = 0;
		/** @brief 프레임 렌더 직전에 GPU 작업이 필요하면 수행합니다. */
		virtual void		preRender( IRHIDevice* /*rhiDevice*/ ) {}
		/** @brief 패널 전용 GPU/구독 리소스를 해제합니다. */
		virtual void		shutdown( IRHIDevice* /*rhiDevice*/ ) {}
	};
}
