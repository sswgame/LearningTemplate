/**
 * @file EditorToolDefaults.h
 * @brief Config/Editor/editortooldefaults.json 에서 읽는 에디터 도구 시드입니다(배포용 Resource 데이터가 아닙니다).
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
    // 1) EditorToolDefaults: 맵 · 아틀라스 · 폰트 시드
    //    사람이 적는 파일이고, 앱은 다시 쓰지 않는다(앱이 쓰는 것은 EditorConfig). 에디터 자기 파일 · 폴더 이름은 코드 상수(`EditorUtil::k…FileName`)다
    // ------------------------------------------------------------------------------

    /**
     * @brief editortooldefaults.json 의 에디터 도구 시드입니다.
     * @details 읽기는 `JsonSerializer` 가 PROPERTY 그래프로 합니다. 필드를 추가하면 읽기가 따라옵니다(손으로 파싱하지 않습니다).
     */
    REFLECT()
    struct EditorToolDefaults
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
         * @brief 실행 중에 다시 읽을 애셋 확장자입니다. **비우면 처리기가 있는 확장자 전부**를 봅니다.
         * @details 무엇을 감시할지는 설정이 정하고, 다시 읽는 방법이 있는지는 코드가 정합니다(`AssetHotReload` 의 처리기 표).
         *          처리기가 없는 확장자를 적으면 경고를 남기고 뺍니다. 감시만 하고 아무 일도 하지 않는 자리를 만들지 않습니다.
         *          변경이 쏟아지는 폴더를 잠시 빼고 싶을 때 이 목록을 좁히면 됩니다.
         */
        PROPERTY()
        vector<string> _listHotReloadExtension{};

        /**
         * @brief 출력 로그 줄을 IDE 로 여는 명령 틀입니다. `{file}` · `{line}` 이 위치로 바뀝니다(예: `code -g "{file}:{line}"`,
         *        `rider64 --line {line} "{file}"`). 비우면 VS Code 입니다(`EditorLogCommands::getOpenCommandTemplate`).
         */
        PROPERTY()
        string _ideOpenCommand{};

        /**
         * @brief 프로젝트 루트 기준 Host 경로에서 에디터 시드를 로드합니다.
         * @param hostRelativePath 비어 있으면 `config::kFileRuntimeEditorToolDefaults`(Config/Editor/editortooldefaults.json)
         */
        [[nodiscard]] bool loadFromHostPath( string_view hostRelativePath = {} );
    };
} // namespace sw::editor
