/**
 * @file InspectorPanel.h
 * @brief GameObject / Component 프로퍼티를 편집하는 선택 인스펙터
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw
{
	class IRHIDevice;
	class GameObject;
	class Component;
	struct TypeInfo;
	struct PropertyInfo;
}

namespace sw::editor
{
	/** @brief 현재 아웃라이너 선택을 검사하고 편집합니다 */
	class InspectorPanel : public IEditorPanel
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) IEditorPanel — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 패널 제목을 반환합니다. */
		const utf8* getPanelTitle() const override { return "Inspector"; }
		/** @brief 인스펙터 UI를 그립니다. */
		void drawContent() override;

	private:
		// ------------------------------------------------------------------------------
		// 2) 선택 / 컴포넌트
		// ------------------------------------------------------------------------------
		/** @brief 현재 선택 섹션을 그립니다. */
		void drawSelectionSection();
		/** @brief GameObject 헤더(이름 등)를 그립니다. */
		void drawGameObjectHeader( GameObject* pObj );
		/** @brief 컴포넌트 섹션을 그립니다. */
		void drawComponentSection( Component* pComp, IRHIDevice* pRhiDevice );

		// ------------------------------------------------------------------------------
		// 3) 리플렉션 위젯
		// ------------------------------------------------------------------------------
		/** @brief 타입의 프로퍼티 목록을 그립니다. */
		void drawTypeProperties( void* pInstance, const TypeInfo* pTypeInfo );
		/** @brief 단일 프로퍼티 위젯을 그립니다. */
		void drawPropertyWidget( void* pInstance, const PropertyInfo& prop );
		/** @brief 타입의 메서드(FUNCTION) 목록을 그립니다. */
		void drawTypeMethods( void* pInstance, const TypeInfo* pTypeInfo );

	private:
		/** @brief FUNCTION() 인자 편집용 스크래치 버퍼 (윈도우 로컬). */
		int32	_arrArgInt[8]{};
		float32 _arrArgFloat[8]{};
		bool	_arrArgBool[8]{};
		utf8	_arrArgString[8][256]{};
		utf8	_arrLastInvokeResult[256]{};
	};
} // namespace sw::editor
