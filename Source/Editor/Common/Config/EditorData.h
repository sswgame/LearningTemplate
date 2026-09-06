/**
 * @file EditorData.h
 * @brief Config/Editor/editordata.xml — 에디터 도구 시드 (배포 Resource data 아님)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    // ------------------------------------------------------------------------------
    // 1) EditorData — 맵/아틀라스/폰트 시드
    //    레이아웃 파일명·Config 폴더는 EditorConfig (Host JSON)
    // ------------------------------------------------------------------------------

    /**
     * @brief editordata.xml 에디터 도구 시드
     * @details 로드는 `XmlSerializer` 가 PROPERTY 그래프로 한다 — 필드를 추가하면 읽기가 따라온다.
     *          예전엔 필드마다 손으로 파싱했고, 폰트 목록과 clearColor 는 전용 파서까지 따로 있었다.
     */
    REFLECT()
    struct EditorData
    {
        REFLECT_BODY();
        PROPERTY()
        string _defaultMap{};
        PROPERTY()
        string _warpMap{};
        PROPERTY()
        string _spriteAtlas{};
        PROPERTY()
        string _defaultMaterial{ "engine/materials/defaultmaterial.material" };

        PROPERTY()
        float32 _fontSize{ 16.0f };
        PROPERTY()
        float32 _playerSpeed{ 5.0f };
        PROPERTY()
        float4 _clearColor{ 0.12f, 0.15f, 0.18f, 1.0f };

        PROPERTY()
        string _editorFolder{ "editor" };
        PROPERTY()
        string _fontsFolder{ "fonts" };

        PROPERTY()
        vector<string> _listBaseFont{

            "consola.ttf",
            "Consolas.ttf",
            "DejaVuSansMono.ttf",
            "DejaVuSansMono-Bold.ttf",
            "LiberationMono-Regular.ttf",
            "NotoSansMono-Regular.ttf",
            "UbuntuMono-R.ttf",
            "FreeMono.ttf",
        };
        PROPERTY()
        vector<string> _listKoreanFont{
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
} // namespace sw::editor
