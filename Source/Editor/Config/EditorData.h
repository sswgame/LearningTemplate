/**
 * @file EditorData.h
 * @brief Config/Editor/editordata.xml — 에디터 도구 시드 (배포 Resource data 아님)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) EditorData — 맵/아틀라스/폰트 시드
	//    레이아웃 파일명·Config 폴더는 EditorConfig (Host JSON)
	// ------------------------------------------------------------------------------

	/** @brief editordata.xml 에디터 도구 시드 */
	struct EditorData
	{
		string _defaultMap{ "game/demo/maps/town01.xml" };
		string _warpMap{ "game/demo/maps/route01.xml" };
		string _spriteAtlas{ "game/demo/textures/00_character/00_player/00_player.png" };
		string _defaultMaterial{ "engine/materials/defaultmaterial._material" };

		float32 _fontSize{ 16.0f };
		float32 _playerSpeed{ 5.0f };
		float32 _arrClearColor[4]{ 0.12f, 0.15f, 0.18f, 1.0f };

		string _editorFolder{ "editor" };
		string _fontsFolder{ "fonts" };

		vector<string> _listBaseFonts{

			"consola.ttf",
			"Consolas.ttf",
			"DejaVuSansMono.ttf",
			"DejaVuSansMono-Bold.ttf",
			"LiberationMono-Regular.ttf",
			"NotoSansMono-Regular.ttf",
			"UbuntuMono-R.ttf",
			"FreeMono.ttf",
		};
		vector<string> _listKoreanFonts{
			"malgun.ttf",
			"malgunsl.ttf",
			"NanumGothic.ttf",
			"NanumBarunGothic.ttf",
			"NotoSansCJK-Regular.ttc",
			"NotoSansCJKkr-Regular.otf",
			"NotoSansKR-Regular.otf",
			"DroidSansFallbackFull.ttf",
		};

		/**
		 * @brief 프로젝트 루트 상대 Host 경로에서 에디터 시드를 로드합니다.
		 * @param hostRelativePath 빈 경로면 EditorConfig::_editorData / Config/Editor/editordata.xml
		 */
		bool loadFromHostPath( string_view hostRelativePath = {} );
	};

} // namespace sw
