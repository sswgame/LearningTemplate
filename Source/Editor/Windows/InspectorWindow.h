/**
 * @file InspectorWindow.h
 * @brief GameObject / Component 프로퍼티와 엔진 섹션을 편집하는 선택 인스펙터
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Editor/Windows/IEditorWindow.h"

namespace sw
{
	class Material;
	class IRHIDevice;
	class GameObject;
	class Component;
	struct TypeInfo;
	struct PropertyInfo;
	struct FunctionInfo;

	/** @brief 현재 아웃라이너 선택을 검사하고 편집합니다 */
	class InspectorWindow : public IEditorWindow
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) IEditorWindow — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 윈도우 제목을 반환합니다. */
		const utf8* getWindowTitle() const override { return "Inspector"; }
		/** @brief 인스펙터 UI를 그립니다. */
		void draw() override;

	private:
		// ------------------------------------------------------------------------------
		// 2) 엔진 / 선택 / 컴포넌트
		// ------------------------------------------------------------------------------
		/** @brief 엔진 전역 설정 섹션을 그립니다. */
		void drawEngineSection();
		/** @brief 현재 선택 섹션을 그립니다. */
		void drawSelectionSection();
		/** @brief GameObject 헤더(이름 등)를 그립니다. */
		void drawGameObjectHeader( GameObject* pObj );
		/** @brief 컴포넌트 섹션을 그립니다. */
		void drawComponentSection( Component* pComp, IRHIDevice* pRhiDevice );

		// ------------------------------------------------------------------------------
		// 3) 리플렉션 위젯 · 머티리얼 · 드롭
		// ------------------------------------------------------------------------------
		/** @brief 타입의 프로퍼티 목록을 그립니다. */
		void drawTypeProperties( void* pInstance, const TypeInfo* pTypeInfo );
		/** @brief 단일 프로퍼티 위젯을 그립니다. */
		void drawPropertyWidget( void* pInstance, const PropertyInfo& prop );
		/** @brief 타입의 메서드(FUNCTION) 목록을 그립니다. */
		void drawTypeMethods( void* pInstance, const TypeInfo* pTypeInfo );
		/** @brief 머티리얼 편집 UI를 렌더링합니다. */
		void renderMaterialUI( Material* pMaterial, IRHIDevice* pRhiDevice );
		/** @brief 드롭된 애셋 경로를 처리합니다. */
		void acceptAssetDrop( const utf8* path );
		/** @brief 마지막으로 드롭된 애셋 경로를 기록합니다. */
		void setLastDroppedAsset( const utf8* path );

	private:
		/** @brief FUNCTION() 인자 편집용 스크래치 버퍼 (윈도우 로컬). */
		int32	_arrArgInt[8]{};
		float32 _arrArgFloat[8]{};
		bool	_arrArgBool[8]{};
		utf8	_arrArgString[8][256]{};
		utf8	_arrLastInvokeResult[256]{};
		string	_lastDroppedAsset;
	};
} // namespace sw
