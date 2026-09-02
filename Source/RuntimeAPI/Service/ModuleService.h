/**
 * @file ModuleService.h
 * @brief App ↔ 모듈 공통 서비스 로케이터. 게임 모듈이 요청할 수 있는 id만 여기에 둡니다.
 */
#pragma once
#include "Core/Common/Macros.h"

namespace sw
{
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed )       Tag Type;
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed ) Tag Type;
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed )             Tag Type;
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT

#define SW_HOST_SERVICE( member, Tag, Type, getter, gameAllowed ) Tag Type;
#include "RuntimeAPI/Service/HostServiceList.xxx"
#undef SW_HOST_SERVICE

    namespace internal
    {
        enum class ModuleServiceId : uint32
        {
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed )       Type,
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed ) Type,
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed )             Type,
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT

#define SW_HOST_SERVICE( member, Tag, Type, getter, gameAllowed ) Type,
#include "RuntimeAPI/Service/HostServiceList.xxx"
#undef SW_HOST_SERVICE

            Count
        };

        inline constexpr uint32 kModuleServiceCount = static_cast<uint32>( ModuleServiceId::Count );

        inline constexpr uint32 toRawServiceId( ModuleServiceId id )
        {
            return static_cast<uint32>( id );
        }

        template <typename T>
        struct ModuleServiceTraits
        {
            static constexpr ModuleServiceId id = ModuleServiceId::Count;
        };

#define SW_DECLARE_MODULE_SERVICE( Type, Id )     \
    template <>                                   \
    struct ModuleServiceTraits<Type>              \
    {                                             \
        static constexpr ModuleServiceId id = Id; \
    }

#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed ) SW_DECLARE_MODULE_SERVICE( Type, ModuleServiceId::Type );
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed ) \
    SW_DECLARE_MODULE_SERVICE( Type, ModuleServiceId::Type );                       \
    SW_DECLARE_MODULE_SERVICE( const Type, ModuleServiceId::Type );
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed ) SW_DECLARE_MODULE_SERVICE( Type, ModuleServiceId::Type );
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT

#define SW_HOST_SERVICE( member, Tag, Type, getter, gameAllowed ) SW_DECLARE_MODULE_SERVICE( Type, ModuleServiceId::Type );
#include "RuntimeAPI/Service/HostServiceList.xxx"
#undef SW_HOST_SERVICE
    } // namespace internal

    struct ModuleService
    {
        const void* arrServices[internal::kModuleServiceCount]{ nullptr };
    };
} // namespace sw
