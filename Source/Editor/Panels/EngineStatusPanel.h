#pragma once
/**
 * @file EngineStatusPanel.h
 * @brief RHI/커맨드라인 등 엔진 상태 표시 패널
 */
#include "Panels/IEditorPanel.h"

namespace sw
{
	/** @brief 현재 RHI·커맨드라인 인자를 표시하는 상태 패널 */
	class EngineStatusPanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "Engine RHI Status & Command Line"; }
		/** @brief 엔진/RHI 상태와 커맨드라인 정보를 그립니다. */
		void		draw( const EditorUIContext& ctx ) override;
	};
}
