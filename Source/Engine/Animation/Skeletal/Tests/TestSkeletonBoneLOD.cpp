// SkeletonBoneLODTest — 본 LOD 경로에서 스켈레톤 후보 둘을 구하고, 그 둘이 `makePathForSkeleton` 으로 같은 본 LOD 경로에 돌아온다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Animation/Skeletal/SkeletonBoneLOD.h"

using namespace sw;

/**
 * @brief [SkeletonBoneLODTest] `makeSkeletonCandidatePaths` 는 `makePathForSkeleton` 의 역함수다 — 임포트 옆 폴더 안 · 같은 폴더 후보 둘 다 원래 본 LOD 경로로 돌아온다
 * @details 본 LOD 확장자가 아니면 후보를 내지 않는다(둘 다 빈 글).
 */
SW_TEST_CASE( SkeletonBoneLODTest, SkeletonCandidatesRoundTripToTheBoneLODPath )
{
    string importedPath;
    string siblingPath;
    SkeletonBoneLOD::makeSkeletonCandidatePaths( "a/knight.bonelod.xml", importedPath, siblingPath );
    SW_EXPECT_EQUAL( string( "a/knight/knight.skeleton.xml" ), importedPath );
    SW_EXPECT_EQUAL( string( "a/knight.skeleton.xml" ), siblingPath );
    SW_EXPECT_EQUAL( string( "a/knight.bonelod.xml" ), SkeletonBoneLOD::makePathForSkeleton( importedPath ) );
    SW_EXPECT_EQUAL( string( "a/knight.bonelod.xml" ), SkeletonBoneLOD::makePathForSkeleton( siblingPath ) );

    importedPath = "stale";
    siblingPath  = "stale";
    SkeletonBoneLOD::makeSkeletonCandidatePaths( "a/knight.skeleton.xml", importedPath, siblingPath );
    SW_EXPECT_TRUE( importedPath.empty() );
    SW_EXPECT_TRUE( siblingPath.empty() );
}
