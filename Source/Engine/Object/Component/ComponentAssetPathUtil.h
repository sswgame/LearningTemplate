/**
 * @file ComponentAssetPathUtil.h
 * @brief 컴포넌트의 에셋 경로 프로퍼티(`PROPERTY( AssetPath )` · `AssetType = …`) 값을 리플렉션으로 읽는 도우미입니다.
 * @details 에디터 핫 리로드(바뀐 파일을 쓰는 컴포넌트 찾기)와 씬 프리로드 목록 수집(`ScenePreloadList`)이 같은 읽기를 쓴다 — 경로 프로퍼티의 판정(문자열 · 해시 문자열)이 두 곳에서 갈라지지 않게 한 곳에 둔다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    struct PropertyInfo;

    class Component;

    struct SW_API ComponentAssetPathUtil
    {
        /** @brief 에셋 경로 프로퍼티의 값입니다. 경로 프로퍼티가 아니거나 문자열이 아니면 빈 값입니다. 값은 컴포넌트 안을 가리키므로 컴포넌트가 사는 동안만 씁니다. */
        static string_view readAssetPath( const PropertyInfo& prop, const Component* pComponent );
    };
} // namespace sw
