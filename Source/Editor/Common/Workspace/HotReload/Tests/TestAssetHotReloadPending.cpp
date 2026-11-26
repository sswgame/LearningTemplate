// AssetHotReloadPendingTest — 에디터 모듈 리로드가 끝내지 못한 바뀐 파일 목록을 다음 인스턴스가 한 번 되찾는다(감시 사건은 한 번만 온다, nogpu).
#include "pch.h"

#include "Core/Container/map.h"
#include "Core/TestFramework/TestFramework.h"

#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"

#include "ModuleAPI/Service/IModuleStateStore.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct AssetHotReloadPendingTestInternal
    {
        /** @brief 호스트 맡김 칸의 테스트 대역입니다(`ModuleStateStore` 와 같은 동작). */
        class FakeStateStore final : public IModuleStateStore
        {
        public:
            void               store( string_view key, vector<uint8>&& bytes ) override { _mapStored[string( key )] = std::move( bytes ); }
            [[nodiscard]] bool take( string_view key, vector<uint8>& outBytes ) override
            {
                const auto it = _mapStored.find( string( key ) );
                if ( it == _mapStored.end() )
                    return false;
                outBytes = std::move( it->second );
                _mapStored.erase( it );
                return true;
            }
            size_t getStoredCount() const { return _mapStored.size(); }

        private:
            map<string, vector<uint8>> _mapStored;
        };
    };
} // namespace

/** @brief [AssetHotReloadPendingTest] 맡긴 목록은 같은 순서로 한 번만 꺼내지고, 빈 목록은 맡기지 않으며, 읽을 수 없는 칸은 빈 목록이다 */
SW_TEST_CASE( AssetHotReloadPendingTest, PendingPathsRoundTripOnce )
{
    AssetHotReloadPendingTestInternal::FakeStateStore store;
    AssetHotReload::storePendingPaths( store, {} );
    SW_EXPECT_EQUAL( size_t{ 0 }, store.getStoredCount() );

    const vector<string> listPath{ "game/hub/models/a.fbx", "engine/materials/b.material" };
    AssetHotReload::storePendingPaths( store, listPath );
    vector<string> listTaken;
    AssetHotReload::takePendingPaths( store, listTaken );
    SW_EXPECT_TRUE( listTaken == listPath );
    AssetHotReload::takePendingPaths( store, listTaken );
    SW_EXPECT_TRUE( listTaken.empty() );

    SW_EXPECT_LOG( "AssetHotReload.PendingPathsUnreadable" );
    store.store( "editor.assetHotReload.pending", vector<uint8>{ 0xFF, 0xFF, 0xFF, 0xFF } );
    AssetHotReload::takePendingPaths( store, listTaken );
    SW_EXPECT_TRUE( listTaken.empty() );
}
