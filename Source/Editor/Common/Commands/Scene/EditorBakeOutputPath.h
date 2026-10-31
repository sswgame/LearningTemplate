/**
 * @file EditorBakeOutputPath.h
 * @brief 베이크 커맨드(라이트 프로브 · 반사 프로브)가 결과를 쓰는 씬 옆 리소스 경로 하나를 만듭니다.
 */
#pragma once
#include "Core/Container/string.h"

namespace sw::editor
{
    /**
     * @brief 오브젝트 하나의 베이크 결과 리소스 id 입니다 — `<씬 id 에서 .scene.xml 을 뗀 것>.<오브젝트 이름 소문자><extension>`. 디스크 자리는 부르는 쪽이 `AssetPaths::toDiskPath` 로 푼다.
     * @details 씬 id 와 이름만 보는 순수 함수다. 리소스 경로는 소문자만 받는다. 이름의 영숫자 밖 글자는 `_` 로 바꾼다.
     * @return @p sceneAssetID 가 `.scene.xml` 로 끝나지 않거나 @p ownerName 이 비었으면 빈 문자열입니다 — 다른 글을 씬 id 로 보고 옆에 붙이지 않는다.
     */
    [[nodiscard]] string makeSceneBakeAssetPath( string_view sceneAssetID, string_view ownerName, string_view extension );
} // namespace sw::editor
