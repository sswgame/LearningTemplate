/**
 * @file SceneDataLayerTypes.h
 * @brief 데이터 레이어 서비스(`SceneDataLayerSubsystem`)의 값 형식입니다. 씬의 조립 기록(`Scene::AssemblySnapshot`)이 값으로 들어 따로 둔다 — 서비스 헤더는 씬 서비스 기반(`ISceneService.h`)을 끌어온다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Scene/SceneDocument.h"

namespace sw
{
    /** @brief 레이어 집합이 같은 엔티티 묶음입니다(`SceneDataLayerSubsystem::Group`). */
    struct SceneDataLayerGroup
    {
        vector<string>                         _listLayer;
        vector<SceneDocument::SceneObjectNode> _listNode;               ///< 문서의 엔티티(저작) — 저장이 다시 쓴다
        vector<SceneDocument::SceneObjectNode> _listHeldNode;           ///< 내릴 때 떠 둔 상태(스냅숏, `_bStateHeld` 일 때) — 다시 지을 때 문서 대신 쓴다
        vector<uint64>                         _listObjectID;           ///< 지은 오브젝트(엔티티마다 하나, 부품 제외)
        bool                                   _bBuilt{ SW_FALSE };     ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool                                   _bFailed{ SW_FALSE };    ///< 짓다 실패했다 — 다시 시도하지 않는다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool                                   _bStateHeld{ SW_FALSE }; ///< `_listHeldNode` 로 짓는다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /** @brief 플레이 종료 복원이 들고 있는 값입니다(`SceneDataLayerSubsystem::State`). 오브젝트는 원래 id 로 돌아오므로 묶음의 id 목록이 그대로 맞습니다. */
    struct SceneDataLayerState
    {
        vector<SceneDataLayerGroup> _listGroup;
        vector<string>              _listActiveLayer;
    };
} // namespace sw
