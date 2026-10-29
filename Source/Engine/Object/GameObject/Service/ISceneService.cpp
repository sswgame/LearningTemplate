#include "pch.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"

#include "Core/String/RegistrationList.h"

namespace sw
{
    namespace
    {
        /** @brief 등록 줄 목록. 함수 정적이라 다른 .cpp 의 정적 초기화가 먼저 와도 비어 있는 채로 만들어진다. 정적 등록부는 모두 `RegistrationList` 하나로 쓴다. */
        RegistrationList<const SceneServiceRegistration>& getSceneServiceRegistrations()
        {
            static RegistrationList<const SceneServiceRegistration> s_listRegistration{ "SceneServiceRegistry" };
            return s_listRegistration;
        }
    } // namespace
} // namespace sw

namespace sw
{
    void SceneServiceRegistry::add( const SceneServiceRegistration* pRegistration )
    {
        SW_ASSERT( pRegistration == nullptr || pRegistration->_pCreate != nullptr );      // 등록 매크로가 늘 채운다
        (void)getSceneServiceRegistrations().addLogged( pRegistration, "Scene service" ); // 같은 줄을 다시 올리면 `AlreadyPresent` 라 할 일이 없다
    }

    const vector<const SceneServiceRegistration*>& SceneServiceRegistry::getAll()
    {
        return getSceneServiceRegistrations().getItems();
    }
} // namespace sw

namespace sw
{
    SceneServiceRegistrar::SceneServiceRegistrar( const SceneServiceRegistration* pRegistration )
    {
        SceneServiceRegistry::add( pRegistration );
    }
} // namespace sw
