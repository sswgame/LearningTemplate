/**
 * @file EditorData.h
 * @brief Config/Editor/editordata.json — 에디터 도구 시드 (배포 Resource data 아님)
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
     * @brief editordata.json 에디터 도구 시드
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
        float32 _fontSize{ 16.0f };
        PROPERTY()
        float4 _clearColor{ 0.12f, 0.15f, 0.18f, 1.0f }; ///< Game View 렌더 타깃 클리어 색

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
         * @brief 실행 중 다시 읽을 에셋 확장자. **비우면 처리기가 있는 확장자 전부**를 봅니다.
         * @details 무엇을 감시할지는 설정이 정하고, 다시 읽는 방법이 있는지는 코드가 정한다
         *          (`AssetHotReload` 의 처리기 표). 처리기가 없는 확장자를 적으면 경고를
         *          남기고 뺀다 — 감시만 하고 아무 일도 안 하는 자리를 만들지 않는다.
         *          변경이 쏟아지는 폴더를 잠시 빼고 싶을 때 이 목록을 좁히면 된다.
         */
        PROPERTY()
        vector<string> _listHotReloadExtension{};

        /**
         * @brief 프로젝트 루트 상대 Host 경로에서 에디터 시드를 로드합니다.
         * @param hostRelativePath 빈 경로면 EditorConfig::_editorData / Config/Editor/editordata.json
         */
        bool loadFromHostPath( string_view hostRelativePath = {} );
    };
} // namespace sw::editor
