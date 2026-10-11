#include "pch.h"

#include "Engine/Resource/Cache/BuiltInAssetCache.h"

namespace sw
{
    namespace
    {
        struct BuiltInAssetCacheInternal
        {
            /** @brief 등록 줄 목록. 함수 정적이라 다른 .cpp 의 정적 초기화가 먼저 와도 비어 있는 채로 만들어진다. */
            static vector<const BuiltInAssetCacheRegistration*>& getRegistrations()
            {
                static vector<const BuiltInAssetCacheRegistration*> s_listRegistration;
                return s_listRegistration;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void BuiltInAssetCacheRegistry::add( const BuiltInAssetCacheRegistration* pRegistration )
    {
        if ( pRegistration == nullptr )
            return;
        vector<const BuiltInAssetCacheRegistration*>& listRegistration = BuiltInAssetCacheInternal::getRegistrations();
        for ( const BuiltInAssetCacheRegistration* pExisting : listRegistration )
        {
            if ( pExisting == pRegistration )
                return;
        }
        listRegistration.push_back( pRegistration );
    }

    const vector<const BuiltInAssetCacheRegistration*>& BuiltInAssetCacheRegistry::getAll()
    {
        return BuiltInAssetCacheInternal::getRegistrations();
    }

    BuiltInAssetCacheRegistrar::BuiltInAssetCacheRegistrar( const BuiltInAssetCacheRegistration* pRegistration )
    {
        BuiltInAssetCacheRegistry::add( pRegistration );
    }
} // namespace sw
