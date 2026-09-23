/**
 * @file EditorService.h
 * @brief 에디터 모듈 내부에서 사용하는 C++ 서비스 로케이터 및 작업공간 상태 관리.
 */
#pragma once
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/String/StringUtil.h"

#include "RuntimeAPI/Service/ModuleService.h"

#include <type_traits>

namespace sw
{
    class Component;
    class GameObject;
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

    /**
     * @brief 에디터 서비스를 찾습니다. **없으면 nullptr 입니다** — 받는 쪽이 확인해야 합니다.
     * @details 짝인 `game::getService<T>()` 와 같은 계약이다. 그쪽에는 한동안 실패 자리에
     *          `SW_ASSERT( false )` 가 있어서 **Debug 에서만** 프로세스가 죽었다 — 같은 모양의
     *          함수가 두 벌 있으면 한쪽만 고쳐지고 끝나기 쉽다는 예다. `CheckNullableServiceUse`
     *          린트가 두 창구를 모두 본다.
     * @return 찾은 서비스. 로컬에도 호스트에도 없으면 nullptr.
     */
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
    /**
     * @brief 지금 편집 중인 씬에서 핸들이 가리키는 오브젝트를 찾습니다. 씬이 없거나 대상이 사라졌으면 nullptr.
     * @details 에디터가 프레임을 넘겨 드는 참조(선택 · 기즈모 대상 · 인스펙터 되돌리기)는 전부 핸들이고, 쓸 때마다 이것으로 푼다.
     */
    GameObject* findGameObject( GameObjectHandle handle );
    /** @brief 지금 편집 중인 씬에서 핸들이 가리키는 컴포넌트를 찾습니다. 씬이 없거나 대상이 사라졌으면 nullptr. */
    Component* findComponent( ComponentHandle handle );
} // namespace sw::editor
