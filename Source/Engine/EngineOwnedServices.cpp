#include "pch.h"

#include "Engine/EngineOwnedServices.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/Renderer/Debug/DebugDrawQueue.h"
#include "Engine/Graphics/Renderer/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/Debug/DebugOverlayState.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

// **완전한 타입이 필요한 자리는 여기 하나다.** `unique_ptr` 의 생성과 소멸이 타입 크기를 알아야 해서,
// 이 정의들을 헤더에 두면 `EngineLoop.h` 를 include 하는 모든 TU 가 서비스 스무 개의 헤더를 끌고 들어온다
// (실제로 그렇게 두었다가 엔진 곳곳이 컴파일되지 않았다). 목록에 줄을 더하면 **여기 include 한 줄**이
// 같이 필요하고, 잊으면 컴파일러가 그 자리에서 말해 준다 — 조용히 넘어가지 않는다.

namespace sw
{
    EngineOwnedServices::EngineOwnedServices() = default;

    EngineOwnedServices::~EngineOwnedServices() = default;

    void EngineOwnedServices::createAll()
    {
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed, owned )       SW_CONCAT( SW_ENGINE_OWNED_CREATE_, owned )( member, Type )
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed, owned ) SW_CONCAT( SW_ENGINE_OWNED_CREATE_, owned )( member, Type )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed, owned )             SW_CONCAT( SW_ENGINE_OWNED_CREATE_, owned )( member, Type )
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
    }

    void EngineOwnedServices::destroyAll()
    {
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed, owned )       SW_CONCAT( SW_ENGINE_OWNED_DESTROY_, owned )( member )
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed, owned ) SW_CONCAT( SW_ENGINE_OWNED_DESTROY_, owned )( member )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed, owned )             SW_CONCAT( SW_ENGINE_OWNED_DESTROY_, owned )( member )
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
    }
} // namespace sw
