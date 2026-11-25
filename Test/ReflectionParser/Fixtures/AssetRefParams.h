// EXPECT-TYPE: sw::fixture::FixtureAssetUser
// EXPECT-GEN: "_single"
// EXPECT-GEN: kContainer1{ sw::ContainerKind::Sequence, "AssetRef", nullptr,
// EXPECT-ASSET-FIELD: _single FixtureAsset
// EXPECT-ASSET-FIELD: _listMany FixtureAsset
// EXPECT-ASSET-FIELD: single FixtureAsset
// EXPECT-ASSET-FIELD: _dataPath -
// EXPECT-ASSET-FIELD: _listDataPath -
// EXPECT-NOT-ASSET-FIELD: _notAPath -
// EXPECT-NOT-ASSET-FIELD: _namedBySource -
// EXPECT-NOT-GEN: AssetRef<FixtureAsset>"
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"

namespace sw::fixture
{
    /** @brief 대상 종류입니다. 쓰는 쪽 헤더에는 앞선 선언만 있어도 되지만, 생성 코드의 팩토리가 칸을 만들므로 여기서는 완전하다. */
    struct FixtureAsset
    {
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".fixture.xml", AssetSuffixRole::Plain }
        };
        static constexpr AssetTypeDef kAssetType{ "FixtureAsset", kArrAssetSuffix, 1, {}, 0, {} };
    };

    /** @brief 값 하나 · 한 겹 시퀀스 · 데이터 경로 칸과 그 시퀀스가 모두 생성 목록에 든다. 경로가 아닌 글 칸 · 이름 칸은 들지 않는다. */
    REFLECT()
    struct FixtureAssetUser
    {
        REFLECT_BODY();

        PROPERTY()
        AssetRef<FixtureAsset> _single;
        PROPERTY()
        vector<AssetRef<FixtureAsset>> _listMany;
        PROPERTY()
        DataPath _dataPath;
        PROPERTY()
        vector<DataPath> _listDataPath;
        PROPERTY()
        string _notAPath;
        PROPERTY( Meta = "NameSource=PhysicsMaterial" )
        hashed_string _namedBySource;
    };
} // namespace sw::fixture
