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
#include "Engine/Serialization/Base/DataPath.h"

namespace sw::editor
{
    // ------------------------------------------------------------------------------
    // 1) EditorToolDefaults: 맵 · 아틀라스 · 폰트 시드
    //    사람이 적는 파일이고, 앱은 다시 쓰지 않는다(앱이 쓰는 것은 EditorConfig). 에디터 자기 파일 · 폴더 이름은 코드 상수(`EditorUtil::k…FileName`)다
    // ------------------------------------------------------------------------------

    /**
     * @brief editortooldefaults.json 의 에디터 도구 시드입니다.
     * @details 읽기는 `JSONSerializer` 가 PROPERTY 그래프로 합니다. 필드를 추가하면 읽기가 따라옵니다(손으로 파싱하지 않습니다).
     *          값의 정본은 json 파일 하나이고 이 헤더는 형식만 가진다. `Required` 칸의 초기값은 형식일 뿐 기본값이 아니다 — 파일에 없으면 읽기가 실패한다.
     */
    REFLECT()
    struct EditorToolDefaults
    {
        REFLECT_BODY();
        PROPERTY()
        DataPath _defaultMap; ///< 타일맵 패널이 처음 여는 맵(리소스 경로, 칸이 없으면 없음)
        PROPERTY()
        DataPath _warpMap; ///< 타일맵 패널의 워프 대상 기본값(리소스 경로, 칸이 없으면 없음)
        PROPERTY()
        DataPath _spriteAtlas; ///< 스프라이트 클립 패널의 기본 아틀라스(리소스 경로, 칸이 없으면 없음)
        PROPERTY( Required )
        DataPath _uiPreviewDocument; ///< UI 미리보기 패널이 처음 여는 UI 문서(리소스 경로)

        PROPERTY( Required )
        float4 _clearColor{}; ///< 씬 뷰 · 게임 뷰 렌더 타깃 클리어 색

        PROPERTY( Required )
        vector<string> _listBaseFont{}; ///< 라틴 글꼴 후보 — 에디터 팩 `fonts/` → OS 글꼴 폴더 순으로 앞의 것부터 찾는다. 하나도 없으면 에디터가 서지 않는다
        PROPERTY( Required )
        vector<string> _listKoreanFont{}; ///< 한글 글꼴 후보(병합) — 찾는 순서는 위와 같다. 하나도 없으면 에디터가 서지 않는다

        /**
         * @brief 실행 중에 다시 읽을 애셋 확장자입니다. **비우면 처리기가 있는 확장자 전부**를 봅니다.
         * @details 무엇을 감시할지는 설정이 정하고, 다시 읽는 방법이 있는지는 코드가 정합니다(`AssetHotReload` 의 처리기 표).
         *          처리기가 없는 확장자를 적으면 경고를 남기고 뺍니다. 감시만 하고 아무 일도 하지 않는 자리를 만들지 않습니다.
         *          변경이 쏟아지는 폴더를 잠시 빼고 싶을 때 이 목록을 좁히면 됩니다.
         */
        PROPERTY()
        vector<string> _listHotReloadExtension{};

        /**
         * @brief 프로젝트 루트 기준 Host 경로에서 에디터 시드를 로드합니다.
         * @param hostRelativePath 비어 있으면 `config::kFileRuntimeEditorToolDefaults`(Config/Editor/editortooldefaults.json)
         * @return 읽었으면 true. 파일이 없거나 깨졌거나 `Required` 칸이 빠졌으면 false 이고 값은 그대로다(오류는 키 이름과 함께 로그에 남는다).
         *         대신 쓸 기본값은 없다 — 기동은 이 실패로 멈춘다.
         */
        [[nodiscard]] bool loadFromHostPath( string_view hostRelativePath = {} );
    };
} // namespace sw::editor
