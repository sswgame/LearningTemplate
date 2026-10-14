#include "pch.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"

namespace sw
{
    namespace
    {
        struct ISceneServiceInternal
        {
            /** @brief 등록 줄 목록. 함수 정적이라 다른 .cpp 의 정적 초기화가 먼저 와도 비어 있는 채로 만들어진다. */
            static vector<const SceneServiceRegistration*>& getRegistrations()
            {
                static vector<const SceneServiceRegistration*> s_listRegistration;
                return s_listRegistration;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void SceneServiceRegistry::add( const SceneServiceRegistration* pRegistration )
    {
        if ( pRegistration == nullptr || pRegistration->_pCreate == nullptr )
            return;
        vector<const SceneServiceRegistration*>& listRegistration = ISceneServiceInternal::getRegistrations();
        for ( const SceneServiceRegistration* pExisting : listRegistration )
        {
            if ( pExisting == pRegistration )
                return;
        }
        listRegistration.push_back( pRegistration );
    }

    const vector<const SceneServiceRegistration*>& SceneServiceRegistry::getAll()
    {
        return ISceneServiceInternal::getRegistrations();
    }
} // namespace sw

namespace sw
{
    SceneServiceRegistrar::SceneServiceRegistrar( const SceneServiceRegistration* pRegistration )
    {
        SceneServiceRegistry::add( pRegistration );
    }
} // namespace sw
