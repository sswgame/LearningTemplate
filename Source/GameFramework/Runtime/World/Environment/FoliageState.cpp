#include "pch.h"

#include "Engine/Environment/Foliage/FoliageComponent.h"

#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"

namespace sw
{
    namespace
    {
        /** @brief 식생의 벤 자리를 모든 게임의 상태 스냅샷(세이브 · 핫 리로드, 스트리밍 셀에 든 식생은 셀의 떠 둔 상태)에 싣습니다 — 식생은 엔진 형식이라 등록만 여기 둔다. */
        const StatefulComponentRegistrar<FoliageComponent> s_foliageStateRegistration{ "FoliageComponent" };
    } // namespace
} // namespace sw
