/**
 * @file SceneFrameStep.h
 * @brief 씬 한 프레임의 단계 열거입니다. 값은 표(`SceneFrameStepList.xxx`)의 줄 순서입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief 씬 한 프레임(`GameObjectManager::tick`)의 단계입니다. 값은 표의 줄 순서이고, 그 순서로 돕니다. */
    enum class SceneFrameStep : uint8
    {
#define SW_SCENE_FRAME_STEP( Name ) Name,
#include "Engine/Object/GameObject/SceneFrameStepList.xxx"
#undef SW_SCENE_FRAME_STEP
        Count
    };

    /** @brief 단계 이름(표의 식별자)입니다. 범위 밖이면 "Unknown" 입니다. */
    SW_API const utf8* getSceneFrameStepName( SceneFrameStep step );
} // namespace sw
