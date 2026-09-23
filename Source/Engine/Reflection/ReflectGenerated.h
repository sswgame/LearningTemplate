/**
 * @file ReflectGenerated.h
 * @brief ReflectionParser 가 만드는 .gen.cpp 와 builtins 생성 코드가 맨 앞에 include 하는 공통 머리 헤더입니다.
 * @details 생성 코드가 쓰는 헤더는 여기에서만 더하고 뺍니다. FileHeader.tpl / BuiltinFileHeader.tpl 은 이 파일을 include 합니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Task/TaskManager.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Common/Common.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
