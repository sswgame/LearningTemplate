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
// 같이 필요하고, 잊으면 컴파일러가 그 자리에서 알려 준다. 조용히 넘어가지 않는다.

namespace sw
{
    namespace
    {
        struct EngineOwnedServicesInternal
        {
            /** @brief `owned=1` 멤버 하나를 놓습니다(`destroyAll` 이 역순으로 부릅니다). */
            template <auto pMember>
            static void resetMember( EngineOwnedServices& owned )
            {
                ( owned.*pMember ).reset();
            }
        };
    } // namespace
} // namespace sw

// owned 열에 따라 표에 넣거나 뺀다(`SW_CONCAT` 으로 0/1 을 붙여 고른다 — 헤더의 다른 자리와 같은 방식).
#define SW_ENGINE_OWNED_RESET_ENTRY_0( member )
#define SW_ENGINE_OWNED_RESET_ENTRY_1( member ) &EngineOwnedServicesInternal::resetMember<&EngineOwnedServices::member>,

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

    void EngineOwnedServices::destroyResourceManager()
    {
        if ( _pResourceManager != nullptr )
            _pResourceManager->shutdown();
        _pResourceManager.reset();
    }

    void EngineOwnedServices::destroyCompressionCodecRegistry()
    {
        // 슬롯부터 끊는다. 소유자가 사라진 뒤에도 슬롯이 가리키면 엔진을 내린 다음의 압축 경로가 해제된 레지스트리를 읽는다.
        if ( _pCompressionCodecRegistry != nullptr && CompressionCodecRegistry::getActive() == _pCompressionCodecRegistry.get() )
            CompressionCodecRegistry::setActive( nullptr );
        _pCompressionCodecRegistry.reset();
    }

    void EngineOwnedServices::destroyAll()
    {
        // X-macro 는 목록 순서로만 펼쳐지므로 멤버마다 해제 함수를 표로 모아 거꾸로 돈다.
        using ResetMemberFunction                              = void ( * )( EngineOwnedServices& );
        static constexpr ResetMemberFunction kArrResetMember[] = {
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed, owned )       SW_CONCAT( SW_ENGINE_OWNED_RESET_ENTRY_, owned )( member )
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed, owned ) SW_CONCAT( SW_ENGINE_OWNED_RESET_ENTRY_, owned )( member )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed, owned )             SW_CONCAT( SW_ENGINE_OWNED_RESET_ENTRY_, owned )( member )
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
        };
        for ( size_t memberIndex = SW_COUNT_OF( kArrResetMember ); memberIndex > 0; --memberIndex )
            kArrResetMember[memberIndex - 1]( *this );
    }
} // namespace sw
