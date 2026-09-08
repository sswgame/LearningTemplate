/**
 * @file EditorService.h
 * @brief 에디터 모듈 내부에서 사용하는 C++ 서비스 로케이터 및 작업공간 상태 관리.
 */
#pragma once
#include "Core/String/StringUtil.h"

#include "RuntimeAPI/Service/ModuleService.h"

#include <type_traits>

namespace sw
{
    class GameObjectManager;
    class Scene;
} // namespace sw

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

    // ------------------------------------------------------------------------------
    // 활성 씬 바로가기
    //
    // 에디터 코드는 거의 항상 "지금 편집 중인 씬"과 그 GameObjectManager 를 원한다. 그런데
    // 거기까지 가려면 SceneManager 서비스 → getActiveScene() → getObjectManager() 를 거치며
    // 단계마다 nullptr 을 확인해야 해서, 커맨드·패널마다 같은 대여섯 줄이 다시 쓰였다(24곳).
    // 새 커맨드를 하나 더 쓸 때마다 그 검사를 또 쓰게 되고, 한 군데서 빠뜨리면 그때만
    // 조용히 죽는다. 원하는 것을 한 줄로 돌려주고 실패는 nullptr 하나로 합친다.
    // ------------------------------------------------------------------------------
    /** @brief 지금 편집 중인 씬. 씬이 없으면 nullptr. */
    Scene* getActiveScene();
    /** @brief 지금 편집 중인 씬의 GameObjectManager. 씬이 없으면 nullptr. */
    GameObjectManager* getActiveObjectManager();
} // namespace sw::editor
