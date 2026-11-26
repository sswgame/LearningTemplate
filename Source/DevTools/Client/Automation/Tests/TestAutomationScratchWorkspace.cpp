// AutomationScratchWorkspaceTest — 시나리오 사본의 정체성: 원본 GUID 글을 사본 GUID 로 바꾸는 규칙(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "DevTools/Client/Automation/Scenario/AutomationScratchWorkspace.h"

using namespace sw;

/** @brief [AutomationScratchWorkspaceTest] 머리 · `.meta` · 참조 글의 GUID 만 바꾼다 — 사본 밖 에셋의 GUID, 더 긴 낱말 속의 16진, 경로 글은 그대로다 */
SW_TEST_CASE( AutomationScratchWorkspaceTest, OnlyKnownGUIDTextsAreRebased )
{
    const AssetGUID                     original = *AssetGUID::tryParse( "0123456789abcdef0123456789abcdef" );
    const AssetGUID                     copy     = *AssetGUID::tryParse( "fedcba9876543210fedcba9876543210" );
    unordered_map<AssetGUID, AssetGUID> mapGUID;
    mapGUID.emplace( original, copy );
    SW_EXPECT_EQUAL( string( "<?asset guid=\"fedcba9876543210fedcba9876543210\"?><S _prefab=\"guid:fedcba9876543210fedcba9876543210\" _base=\"guid:fedcba9876543210fedcba9876543210#a\"/>" ),
                     AutomationScratchWorkspace::rebaseIdentities( "<?asset guid=\"0123456789abcdef0123456789abcdef\"?><S _prefab=\"guid:0123456789abcdef0123456789abcdef\" _base=\"guid:0123456789abcdef0123456789abcdef#a\"/>", mapGUID ) );
    const string_view kept = "<S _a=\"guid:00000000000000000000000000000001\" _b=\"x0123456789abcdef0123456789abcdef\" _c=\"game/x/0123456789abcdef.xml\"/>";
    SW_EXPECT_EQUAL( string( kept ), AutomationScratchWorkspace::rebaseIdentities( kept, mapGUID ) );
    SW_EXPECT_EQUAL( string( "fedcba9876543210fedcba9876543210" ), AutomationScratchWorkspace::rebaseIdentities( "0123456789abcdef0123456789abcdef", mapGUID ) ); // 글의 처음 · 끝
}
