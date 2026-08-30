/**
 * @file pch.h
 * @brief 엔진 공통 Precompiled Header (CoreMinimal + EngineMinimal + Object/Scene/RHI)
 * @details CMake `sw_configurePch`가 `SW_ENABLE_PCH=ON`일 때 타겟별로 이 헤더를 강제 포함합니다.
 */
#pragma once
#include "Core/CoreMinimal.h"

#include "Engine/Common/Common.h"
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
