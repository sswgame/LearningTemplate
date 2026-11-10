#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Client/Automation/Runner/AutomationGoldenUtil.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/Config/RHIBackendType.h"
#include "Engine/Content/AssetIO.h"

#include "sw/config/CookContract.gen.h"

using namespace sw;

namespace
{
    struct ScenarioGoldensTestUtil
    {
        /** @brief Windows 검증 체인의 백엔드 꼬리(쿠킹 표 `scenario_backends`) — 저장소의 골든은 어느 호스트에서 보든 같으므로 호스트 플랫폼이 아니라 이 표와 대조한다. */
        static vector<string> getWindowsChainTags()
        {
            vector<string> listTag;
#define SW_GOLDEN_CHAIN_ROW( Backend ) listTag.emplace_back( *AutomationGoldenUtil::findBackendTag( static_cast<uint32>( RHIBackend::Backend ) ) );
            SW_RHI_SCENARIO_BACKENDS_Windows( SW_GOLDEN_CHAIN_ROW )
#undef SW_GOLDEN_CHAIN_ROW
                return listTag;
        }

        /**
         * @brief 한 폴더의 시나리오마다 `<ExpectImage>` 골든이 검증 체인 백엔드마다 있는지 봅니다. 시나리오가 없는 폴더면 false 입니다.
         * @details 백엔드를 고르는 시나리오(`<SkipUnless>` — 능력이 없는 백엔드는 건너뛴다, `<SwapBackend>` — 단언하는 때의 백엔드가 바뀐다)는 하나 이상이면 된다.
         */
        static bool checkFolder( string_view folder, const vector<string>& listTag )
        {
            vector<AssetID> listScenario;
            if ( AssetIO::collectAssets( AssetID{ folder }, ".scenario.xml", listScenario ) != AssetFolderListing::Listed )
                return false;
            for ( const AssetID& scenarioID : listScenario )
            {
                string text;
                SW_EXPECT_TRUE( AssetIO::readTextAsset( scenarioID, text ) );
                AutomationScenario scenario;
                if ( scenario.parse( text, string( scenarioID.view() ) ).has_value() == false )
                    continue; // 읽기 오류는 ScenarioFilesTest 가 낸다
                bool bChoosesBackend = false;
                for ( const AutomationStep& step : scenario.getSteps() )
                {
                    const string_view kind = step._kind;
                    bChoosesBackend        = bChoosesBackend || kind == "SkipUnless" || kind == "SwapBackend";
                }
                const string goldenFolder = FileUtil::getDirectoryPart( scenarioID.view() ) + "/" + kAutomationGoldenFolder;
                for ( const AutomationStep& step : scenario.getSteps() )
                {
                    const string* pGolden = string_view{ step._kind } == "ExpectImage" ? step.findAttribute( "golden" ) : nullptr;
                    if ( pGolden == nullptr )
                        continue;
                    string missing;
                    uint32 foundCount = 0;
                    for ( const string& tag : listTag )
                    {
                        const string goldenID = AutomationGoldenUtil::makeGoldenID( goldenFolder, scenario.getName(), *pGolden, tag, "" );
                        if ( AssetIO::hasAsset( AssetID{ goldenID } ) )
                            ++foundCount;
                        else
                        {
                            missing += " ";
                            missing += tag;
                        }
                    }
                    const bool bCovered = bChoosesBackend ? foundCount > 0 : missing.empty();
                    SW_EXPECT_TRUE_MSG( bCovered, ( string( scenarioID.view() ) + " golden '" + *pGolden + "' has no golden for: " + missing ).c_str() );
                }
            }
            return listScenario.empty() == false;
        }
    };
    using Util = ScenarioGoldensTestUtil;
} // namespace

// 검증 체인이 도는 백엔드마다 골든이 있어야 한다 — 체인에 백엔드를 더하면(WebGPU) 그 골든을 갖추기 전까지 여기서 진다. 쿠킹 표가 집합의 한 곳이다.
SW_TEST_CASE( ScenarioGoldensTest, EveryChainBackendHasGoldens )
{
    const vector<string> listTag = Util::getWindowsChainTags();
    SW_REQUIRE_FALSE( listTag.empty() );
    const string gameFolder = "game/" + StringUtil::toLower( GameSelection::getName().c_str() ) + "/automation";
    SW_EXPECT_TRUE( Util::checkFolder( "engine/automation", listTag ) );
    (void)Util::checkFolder( "engine/automation/editor", listTag );
    (void)Util::checkFolder( "engine/automation/rhiswap", listTag );
    (void)Util::checkFolder( gameFolder, listTag ); // 시나리오가 없는 게임도 있다
    (void)Util::checkFolder( gameFolder + "/desktop", listTag );
    (void)Util::checkFolder( gameFolder + "/sequence", listTag );
    (void)Util::checkFolder( gameFolder + "/showcase", listTag );
}

// 골든 꼬리는 쿠킹 표의 명령줄 이름이다 — 판정기에 따로 적은 표가 WebGPU 를 빠뜨려 그 백엔드의 골든을 뜰 수 없었다.
SW_TEST_CASE( ScenarioGoldensTest, BackendTagsFollowTheCookTable )
{
    const optional<string_view> dx12 = AutomationGoldenUtil::findBackendTag( static_cast<uint32>( RHIBackend::DirectX12 ) );
    const optional<string_view> wgpu = AutomationGoldenUtil::findBackendTag( static_cast<uint32>( RHIBackend::WebGPU ) );
    SW_REQUIRE_TRUE( dx12.has_value() && wgpu.has_value() );
    SW_EXPECT_TRUE( *dx12 == "dx12" );
    SW_EXPECT_TRUE( *wgpu == "wgpu" );
    SW_EXPECT_FALSE( AutomationGoldenUtil::findBackendTag( static_cast<uint32>( RHIBackend::WebGPU ) + 1u ).has_value() );
}
