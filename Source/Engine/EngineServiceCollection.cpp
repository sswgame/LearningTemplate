#include "pch.h"

#include "Engine/EngineServiceCollection.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/Renderer/Debug/DebugDrawQueue.h"
#include "Engine/Graphics/Renderer/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/Debug/DebugOverlayState.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

// **완전한 타입이 필요한 자리는 여기 하나다.** `unique_ptr` 의 생성과 소멸이 타입 크기를 알아야 해서,
// 이 정의들을 헤더에 두면 `EngineLoop.h` 를 include 하는 모든 TU 가 서비스 스무 개의 헤더를 끌고 들어온다
// 목록에 줄을 더하면 **여기 include 한 줄**이
// 같이 필요하고, 잊으면 컴파일러가 그 자리에서 알려 준다. 조용히 넘어가지 않는다.
// creator 칸에 따라 해제 표에 넣거나 뺀다(`SW_CONCAT` 으로 낱말을 붙여 고른다 — 헤더의 다른 자리와 같은 방식).

#define SW_ENGINE_OWNED_RESET_ENTRY_HostCreated( member )
#define SW_ENGINE_OWNED_RESET_ENTRY_EngineCreated( member ) { #member, &resetMember<&EngineServiceCollection::member> },
#define SW_ENGINE_OWNED_CREATE_HostCreated( member, Type )
// 만드는 동안 그 서비스의 메모리 용도 태그를 건다(`kServiceMemoryTag`). 생성자가 잡는 표 · 풀이 그 줄로 세인다.
#define SW_ENGINE_OWNED_CREATE_EngineCreated( member, Type )                                               \
    if ( member == nullptr )                                                                               \
    {                                                                                                      \
        const ScopedMemoryTag createMemoryTag{ EngineServiceCollectionInternal::kServiceMemoryTag<Type> }; \
        member = make_unique<Type>();                                                                      \
    }

namespace sw
{
    namespace
    {
        struct EngineServiceCollectionInternal
        {
            /** @brief `EngineCreated` 멤버 하나를 놓습니다. */
            template <auto pMember>
            static void resetMember( EngineServiceCollection& owned )
            {
                ( owned.*pMember ).reset();
            }

            /**
             * @brief 서비스를 만드는 동안 거는 메모리 용도 태그입니다. 적지 않은 서비스는 EngineMisc 입니다.
             * @details 생성자가 큰 표 · 풀을 잡는 서비스(`TaskManager` 의 큐 · 노드 풀, `TypeRegistry` 의 표)만 제 줄로 보낸다.
             */
            template <typename T>
            static constexpr MemoryTag kServiceMemoryTag = MemoryTag::EngineMisc;

            /** @brief 해제 표의 칸 하나입니다(멤버 이름과 해제 함수). */
            struct ResetEntry
            {
                const utf8* _pName;
                void ( *_pReset )( EngineServiceCollection& );
            };

            /** @brief `EngineCreated` 행을 목록 순서로 담은 해제 표입니다. 놓는 순서는 `getResetIndex` 가 정합니다. */
            static constexpr ResetEntry kArrResetEntry[] = {
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility, creator )       SW_CONCAT( SW_ENGINE_OWNED_RESET_ENTRY_, creator )( member )
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility, creator ) SW_CONCAT( SW_ENGINE_OWNED_RESET_ENTRY_, creator )( member )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility, creator )                SW_CONCAT( SW_ENGINE_OWNED_RESET_ENTRY_, creator )( member )
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
            };

            /** @brief @p order 번째로 놓을 칸의 자리입니다 — 목록의 역순(소멸자와 같은 순서). `destroyAll` 과 `makeDestroyOrder` 가 함께 쓴다. */
            static constexpr size_t getResetIndex( size_t order ) { return SW_COUNT_OF( kArrResetEntry ) - 1 - order; }
        };

        template <>
        constexpr MemoryTag EngineServiceCollectionInternal::kServiceMemoryTag<TaskManager> = MemoryTag::Task;
        template <>
        constexpr MemoryTag EngineServiceCollectionInternal::kServiceMemoryTag<TypeRegistry> = MemoryTag::Reflection;
        template <>
        constexpr MemoryTag EngineServiceCollectionInternal::kServiceMemoryTag<SceneManager> = MemoryTag::Scene;
        template <>
        constexpr MemoryTag EngineServiceCollectionInternal::kServiceMemoryTag<AssetManager> = MemoryTag::Asset;
        template <>
        constexpr MemoryTag EngineServiceCollectionInternal::kServiceMemoryTag<AssetStreamingQueue> = MemoryTag::Asset;
        template <>
        constexpr MemoryTag EngineServiceCollectionInternal::kServiceMemoryTag<ShaderCache> = MemoryTag::Shader;
        template <>
        constexpr MemoryTag EngineServiceCollectionInternal::kServiceMemoryTag<RHIBackendRegistry> = MemoryTag::RenderCpu;
        template <>
        constexpr MemoryTag EngineServiceCollectionInternal::kServiceMemoryTag<RenderTargetRegistry> = MemoryTag::RenderCpu;
        template <>
        constexpr MemoryTag EngineServiceCollectionInternal::kServiceMemoryTag<DebugDrawQueue> = MemoryTag::RenderCpu;
    } // namespace
} // namespace sw

namespace sw
{
    EngineServiceCollection::EngineServiceCollection() = default;

    EngineServiceCollection::~EngineServiceCollection() = default;

    void EngineServiceCollection::createAll()
    {
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility, creator )       SW_CONCAT( SW_ENGINE_OWNED_CREATE_, creator )( member, Type )
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility, creator ) SW_CONCAT( SW_ENGINE_OWNED_CREATE_, creator )( member, Type )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility, creator )                SW_CONCAT( SW_ENGINE_OWNED_CREATE_, creator )( member, Type )
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
    }

    void EngineServiceCollection::destroyAssetManager()
    {
        if ( _pAssetManager != nullptr )
            _pAssetManager->shutdown();
        _pAssetManager.reset();
    }

    void EngineServiceCollection::destroyCompressionCodecRegistry()
    {
        // 슬롯부터 끊는다. 소유자가 사라진 뒤에도 슬롯이 가리키면 엔진을 내린 다음의 압축 경로가 해제된 레지스트리를 읽는다.
        if ( _pCompressionCodecRegistry != nullptr && CompressionCodecRegistry::getActive() == _pCompressionCodecRegistry.get() )
            CompressionCodecRegistry::setActive( nullptr );
        _pCompressionCodecRegistry.reset();
    }

    void EngineServiceCollection::destroyAll()
    {
        // X-macro 는 목록 순서로만 펼쳐지므로 해제 표를 만들어 거꾸로 돈다.
        for ( size_t order = 0; order < SW_COUNT_OF( EngineServiceCollectionInternal::kArrResetEntry ); ++order )
            EngineServiceCollectionInternal::kArrResetEntry[EngineServiceCollectionInternal::getResetIndex( order )]._pReset( *this );
    }

    vector<const utf8*> EngineServiceCollection::makeDestroyOrder()
    {
        vector<const utf8*> listName;
        listName.reserve( SW_COUNT_OF( EngineServiceCollectionInternal::kArrResetEntry ) );
        for ( size_t order = 0; order < SW_COUNT_OF( EngineServiceCollectionInternal::kArrResetEntry ); ++order )
            listName.push_back( EngineServiceCollectionInternal::kArrResetEntry[EngineServiceCollectionInternal::getResetIndex( order )]._pName );
        return listName;
    }
} // namespace sw
