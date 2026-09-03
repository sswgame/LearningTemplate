/**
 * @file EditorService.h
 * @brief 에디터 모듈 내부에서 사용하는 C++ 서비스 로케이터 및 작업공간 상태 관리.
 */
#pragma once
#include "Core/String/StringUtil.h"

#include "RuntimeAPI/Service/ModuleService.h"

#include <type_traits>

namespace sw::editor
{
    struct EditorData;

    namespace internal
    {
        template <typename T, typename = void>
        struct HasModuleServiceTraits : std::false_type
        {
        };

        template <typename T>
        struct HasModuleServiceTraits<T, std::void_t<decltype( sw::internal::ModuleServiceTraits<T>::id )>> : std::true_type
        {
        };
    } // namespace internal

    namespace internal
    {
        template <typename T>
        constexpr uint64 getServiceTypeHash() noexcept
        {
            return StringUtil::computeHash64( SW_FUNCTION_SIGNATURE, sizeof( SW_FUNCTION_SIGNATURE ) - 1, false );
        }

        void* getRawService( sw::internal::ModuleServiceId id );
        void  bindRawLocalService( uint64 typeHash, void* pService );
        void* getRawLocalService( uint64 typeHash );
    } // namespace internal

    void bindEditorService( const ModuleService& service );
    void unbindEditorService();

    template <typename T>
    void bindLocalService( T* pService )
    {
        internal::bindRawLocalService( internal::getServiceTypeHash<T>(), static_cast<void*>( pService ) );
    }

    template <typename T>
    void unbindLocalService()
    {
        internal::bindRawLocalService( internal::getServiceTypeHash<T>(), nullptr );
    }

    template <typename T>
    T* getService()
    {
        void* pLocal = internal::getRawLocalService( internal::getServiceTypeHash<T>() );
        if ( pLocal != nullptr )
            return static_cast<T*>( pLocal );

        if constexpr ( internal::HasModuleServiceTraits<T>::value )
            return static_cast<T*>( internal::getRawService( sw::internal::ModuleServiceTraits<T>::id ) );
        return nullptr;
    }

    EditorData& getEditorData();
    void        setEditorData( EditorData* pData );
} // namespace sw::editor
