/**
 * @file ModuleServiceTable.h
 * @brief App ↔ 모듈 공통 서비스 로케이터입니다. 엔진 · 호스트 서비스 id 를 모두 두고, 게임 모듈에는 GameVisible 인 칸만 채워 넘깁니다.
 */
#pragma once
#include "Core/Common/Macros.h"

namespace sw
{
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility, creator )       Tag Type;
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility, creator ) Tag Type;
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility, creator )                Tag Type;
#include "RuntimeAPI/Service/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT

#define SW_HOST_SERVICE( member, Tag, Type, getter, visibility ) Tag Type;
#include "RuntimeAPI/Service/HostServiceList.xxx"
#undef SW_HOST_SERVICE

    namespace internal
    {
        enum class ModuleServiceID : uint32
        {
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility, creator )       Type,
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility, creator ) Type,
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility, creator )                Type,
#include "RuntimeAPI/Service/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT

#define SW_HOST_SERVICE( member, Tag, Type, getter, visibility ) Type,
#include "RuntimeAPI/Service/HostServiceList.xxx"
#undef SW_HOST_SERVICE

            Count
        };

        inline constexpr uint32 kModuleServiceCount = static_cast<uint32>( ModuleServiceID::Count );

        inline constexpr uint32 toRawServiceID( ModuleServiceID id )
        {
            return static_cast<uint32>( id );
        }

        template <typename T>
        struct ModuleServiceTraits
        {
            static constexpr ModuleServiceID id = ModuleServiceID::Count;
        };

#define SW_DECLARE_MODULE_SERVICE( Type, ID )     \
    template <>                                   \
    struct ModuleServiceTraits<Type>              \
    {                                             \
        static constexpr ModuleServiceID id = ID; \
    }

#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility, creator ) SW_DECLARE_MODULE_SERVICE( Type, ModuleServiceID::Type );
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility, creator ) \
    SW_DECLARE_MODULE_SERVICE( Type, ModuleServiceID::Type );                                  \
    SW_DECLARE_MODULE_SERVICE( const Type, ModuleServiceID::Type );
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility, creator ) SW_DECLARE_MODULE_SERVICE( Type, ModuleServiceID::Type );
#include "RuntimeAPI/Service/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT

#define SW_HOST_SERVICE( member, Tag, Type, getter, visibility ) SW_DECLARE_MODULE_SERVICE( Type, ModuleServiceID::Type );
#include "RuntimeAPI/Service/HostServiceList.xxx"
#undef SW_HOST_SERVICE
    } // namespace internal

    /**
     * @struct ModuleServiceTable
     * @brief 호스트(엔진 · App)가 모듈에 넘기는 서비스 칸 표입니다. 게임 · 에디터의 서비스 창구가 범위(`SubsystemScope`)에서 못 찾으면 여기를 봅니다.
     */
    struct ModuleServiceTable
    {
        const void* arrServices[internal::kModuleServiceCount]{ nullptr };

        /** @brief 서비스 표에 칸이 있는 타입(`ModuleServiceTraits<T>`)이면 그 칸, 아니면 nullptr 입니다. */
        template <typename T>
        T* find() const
        {
            constexpr internal::ModuleServiceID id = internal::ModuleServiceTraits<T>::id;
            if constexpr ( id == internal::ModuleServiceID::Count )
                return nullptr;
            else
                return static_cast<T*>( const_cast<void*>( arrServices[internal::toRawServiceID( id )] ) );
        }
    };
} // namespace sw
