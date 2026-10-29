#include "pch.h"

#include "Engine/Asset/Cache/BuiltInAssetCache.h"

#include "Core/String/RegistrationList.h"

namespace sw
{
    namespace
    {
        /** @brief 등록 줄 목록. 함수 정적이라 다른 .cpp 의 정적 초기화가 먼저 와도 비어 있는 채로 만들어진다. 정적 등록부는 모두 `RegistrationList` 하나로 쓴다. */
        RegistrationList<const BuiltInAssetCacheRegistration>& getBuiltInAssetCacheRegistrations()
        {
            static RegistrationList<const BuiltInAssetCacheRegistration> s_listRegistration{ "BuiltInAssetCacheRegistry" };
            return s_listRegistration;
        }
    } // namespace
} // namespace sw

namespace sw
{
    void BuiltInAssetCacheRegistry::add( const BuiltInAssetCacheRegistration* pRegistration )
    {
        (void)getBuiltInAssetCacheRegistrations().addLogged( pRegistration, "Built-in asset cache" ); // 같은 줄을 다시 올리면 `AlreadyPresent` 라 할 일이 없다
    }

    const vector<const BuiltInAssetCacheRegistration*>& BuiltInAssetCacheRegistry::getAll()
    {
        return getBuiltInAssetCacheRegistrations().getItems();
    }

    BuiltInAssetCacheRegistrar::BuiltInAssetCacheRegistrar( const BuiltInAssetCacheRegistration* pRegistration )
    {
        BuiltInAssetCacheRegistry::add( pRegistration );
    }
} // namespace sw
