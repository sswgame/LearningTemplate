// TerrainStateTest — 지형의 저장하지 않은 편집(높이)이 상태 스냅샷 계약(`serializeState` · `restoreState`)으로 다시 읽은 지형에 넘어가는지(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Environment/Terrain/HeightfieldAsset.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"

using namespace sw;

namespace TerrainStateTestUtil
{
    /** @brief 해상도 — 청크 칸 기본값(32)의 청크 하나다. */
    constexpr uint32 kResolution = 33;

    /** @brief 평평한 높이장(구멍 없음)을 케이스 도메인에 쓰고 그 리소스 id 를 돌려줍니다. */
    string writeFlatHeightfield( const test::ScopedScratchDomain& domain )
    {
        HeightfieldAsset asset;
        asset._resolution = kResolution;
        asset._listHeight.assign( static_cast<size_t>( kResolution ) * kResolution, uint16{ 0 } );
        vector<uint8> bytes;
        asset.saveToMemory( bytes );
        SW_EXPECT_TRUE( FileUtil::writeFile( domain.makeDiskPath( "flat.heightfield" ), bytes.data(), static_cast<uint64>( bytes.size() ) ) );
        return test::ScopedScratchDomain::makeID( "flat.heightfield" ).str();
    }

    /** @brief @p heightfieldID 를 읽은 지형을 새 오브젝트에 붙입니다. 읽지 못하면 nullptr 입니다. */
    TerrainComponent* addTerrain( GameObjectManager& manager, const utf8* pName, const string& heightfieldID )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
        if ( pObject == nullptr )
            return nullptr;
        TerrainComponent* pTerrain = pObject->addComponent<TerrainComponent>();
        if ( pTerrain == nullptr )
            return nullptr;
        pTerrain->setHeightfieldPath( heightfieldID );
        return pTerrain->reloadTerrain() ? pTerrain : nullptr;
    }
} // namespace TerrainStateTestUtil

/**
 * @brief [TerrainStateTest] 쓴 높이는 상태로 실려 같은 에셋을 다시 읽은 지형에 그대로 돌아온다
 * @details 모듈 리로드 · 백엔드 교체는 씬을 속성으로 복원하고 에셋을 다시 읽으므로, 저장하지 않은 높이는 이 계약으로만 넘어간다.
 *          `ComponentStateStore::capture` 는 `StateIO::writeObject`(= `writeState`)로 쓰고 다시 만든 지형은 `restoreState` 로 받는다.
 */
SW_TEST_CASE( TerrainStateTest, UnsavedHeightEditsRoundTrip )
{
    namespace Util = TerrainStateTestUtil;
    const test::ScopedScratchDomain domain;
    const string                    heightfieldID = Util::writeFlatHeightfield( domain );
    GameObjectManager               manager;
    TerrainComponent*               pEdited = Util::addTerrain( manager, "Edited", heightfieldID );
    SW_REQUIRE_NOT_NULL( pEdited );
    SW_EXPECT_FALSE( pEdited->hasWrittenEdits() );

    constexpr uint32 kSampleX    = 7;
    constexpr uint32 kSampleZ    = 5;
    const size_t     sampleIndex = static_cast<size_t>( kSampleZ ) * Util::kResolution + kSampleX;
    vector<float32>  listHeight  = pEdited->getHeightfield().getHeightSamples();
    const float32    diskHeight  = listHeight[sampleIndex];
    listHeight[sampleIndex]      = diskHeight + 4.5f;
    TerrainSampleRect editedRect{};
    editedRect.include( kSampleX, kSampleZ );
    SW_REQUIRE_TRUE( pEdited->writeHeights( listHeight, editedRect ) );
    SW_REQUIRE_TRUE( pEdited->hasWrittenEdits() );

    Archive written;
    pEdited->writeState( written );
    vector<uint8> bytes( written.getData(), written.getData() + written.getSize() );

    TerrainComponent* pRestored = Util::addTerrain( manager, "Restored", heightfieldID );
    SW_REQUIRE_NOT_NULL( pRestored );
    SW_EXPECT_NEAR( diskHeight, pRestored->getHeightfield().getHeightSamples()[sampleIndex], 1.0e-4f ); // 다시 읽은 것은 디스크 값이다
    pRestored->restoreState( std::move( bytes ) );
    SW_EXPECT_NEAR( diskHeight + 4.5f, pRestored->getHeightfield().getHeightSamples()[sampleIndex], 1.0e-4f );
    SW_EXPECT_TRUE( pRestored->getHeightfield().getHeightSamples() == pEdited->getHeightfield().getHeightSamples() );
    SW_EXPECT_TRUE( pRestored->getHeightfield().getHoleCells() == pEdited->getHeightfield().getHoleCells() );
}
