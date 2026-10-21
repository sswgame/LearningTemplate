/**
 * @file EditorBakeOutputPath.h
 * @brief 베이크 커맨드(라이트 프로브 · 반사 프로브)가 결과를 쓰는 씬 옆 리소스 경로 하나를 만듭니다.
 */
#pragma once
#include "Core/Container/string.h"

namespace sw
{
    class GameObject;
    class Scene;
} // namespace sw

namespace sw::editor
{
    /**
     * @brief 오브젝트 하나의 베이크 결과 리소스 경로입니다 — `<씬 경로에서 .scene.xml 을 뗀 것>.<오브젝트 이름 소문자><extension>`. 씬을 저장한 적이 없으면 빈 문자열입니다.
     * @details 리소스 경로는 소문자만 받는다(CheckResourceCasing). 이름의 영숫자 밖 글자는 `_` 로 바꾼다.
     */
    string makeSceneBakeResourcePath( const Scene& scene, const GameObject& owner, string_view extension );
} // namespace sw::editor
