/**
 * @file SpriteClipTool.h
 * @brief 아틀라스 스프라이트 프레임 / 트랜스폼 키 클립 에디터 (파일명은 editordata.xml)
 */
#pragma once
#include "Editor/Windows/IEditorWindow.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	/** @brief 프레임 목록과 선택적 TransformAnimation 키를 편집합니다 (AnimGraph과 별개) */
	class SpriteClipTool : public IEditorWindow
	{
	public:
		/** @brief 스프라이트 클립 도구를 생성합니다. */
		SpriteClipTool();

		// ------------------------------------------------------------------------------
		// 1) IEditorWindow — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 온디맨드 도구이므로 기본적으로 닫힌 채 시작합니다. */
		bool isToolWindow() const override { return true; }
		/** @brief 윈도우 제목을 반환합니다. */
		const utf8* getWindowTitle() const override { return "Sprite Clip"; }
		/** @brief 스프라이트 클립 편집 UI를 그립니다. */
		void draw( const EditorUIContext& ctx ) override;

	private:
		// ------------------------------------------------------------------------------
		// 2) 프레임 · 트랜스폼 키
		// ------------------------------------------------------------------------------
		/** @brief 아틀라스 UV 한 프레임 */
		struct Frame
		{
			float32 _u{ 0.0f };
			float32 _v{ 0.0f };
			float32 _w{ 1.0f };
			float32 _h{ 1.0f };
			int32	_durationMs{ 100 };
		};

		/** @brief 시간축 트랜스폼 키 (위치 / 각도) */
		struct TransformKey
		{
			float32 _time{ 0.0f };
			float32 _x{ 0.0f };
			float32 _y{ 0.0f };
			float32 _angleDeg{ 0.0f };
		};

		// ------------------------------------------------------------------------------
		// 3) SpriteClip.json 로드/저장
		// ------------------------------------------------------------------------------
		/** @brief SpriteClip.json을 불러옵니다. */
		void loadJson();
		/** @brief SpriteClip.json을 저장합니다. */
		void saveJson() const;

	private:
		utf8				 _arrAtlasPath[256];
		vector<Frame>		 _listFrames;
		vector<TransformKey> _listKeys;
		int32				 _selectedFrame;
		int32				 _selectedKey;
		string				 _status;
	};
} // namespace sw
