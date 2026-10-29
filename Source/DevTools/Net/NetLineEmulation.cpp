#include "pch.h"

#include "DevTools/Net/NetLineEmulation.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/NetEmulationPresetTable.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Net/GameNetSession.h"

namespace sw
{
    SW_LOG_CALLER( "NetLineEmulation" );

    /**
     * @brief `-gv_netEmulationPreset=<이름>`: 회선 흉내를 프리셋 표(`NetEmulationPresetTable`)의 한 줄로 둡니다. 있으면 `gv_netEmulation*` 낱값을 무시합니다.
     * @details 부하 검증의 `<Load network>` 와 하드웨어 프로필이 이 하나로 넘긴다 — 프리셋 값을 밖에 다시 적지 않는다.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_netEmulationPreset, "", "네트워크 흉내: 회선 프리셋 이름(engine/settings/netemulation.presets.xml — 비면 gv_netEmulation* 낱값)" );

    namespace
    {
        struct NetLineEmulationInternal
        {
            /** @brief 보내는 쪽 · 받는 쪽 조건을 한 줄로 씁니다(`net.emulation` 의 답). */
            static string describeEmulation( const NetEmulationTransport& emulation )
            {
                return "net emulation up [" + emulation.getDefaultConditions().makeOverrideText() + "] down [" + emulation.getIncomingConditions().makeOverrideText() + "]" +
                       ( emulation.isInOutage() ? " (outage)" : "" );
            }

            /**
             * @brief `net.emulation` — 이 프로세스의 회선 흉내를 바꿉니다.
             * @details - `net.emulation` : 지금 조건
             *          - `net.emulation <up|down|both> [<프리셋>] [key=value …]` : 프리셋에서 시작해(없으면 지금 조건에서) 덮어쓰기를 적용한다. up 은 보내는 쪽, down 은 받는 쪽이다
             *          - `net.emulation spike <초>` : 그동안 보내고 받는 것을 모두 버린다(0 이면 끝낸다)
             *          - `net.emulation stats` : `NetEmulationReport` 한 줄
             */
            static bool runEmulation( const vector<string>& listArgument, string& outReply )
            {
                GameNetSession* const            pSession = engine::findSubsystem<GameNetSession>();
                NetLineEmulationSubsystem* const pTool    = engine::findSubsystem<NetLineEmulationSubsystem>();
                if ( pSession == nullptr || pSession->isActive() == false || pTool == nullptr )
                {
                    outReply = "net session: none (start with -gv_netListenPort=<port> or -gv_netConnectAddress=<host:port>)";
                    return true;
                }
                // 샤드마다 흉내가 하나씩이고 모두 같은 조건이다 — 첫 샤드에서 읽고, 바꿀 때는 모두에 건다.
                NetEmulationTransport* pEmulation = pTool->find( *pSession, 0 );
                if ( pEmulation == nullptr )
                {
                    outReply = "net emulation: the session opened before the emulation was installed";
                    return true;
                }
                if ( listArgument.empty() )
                {
                    outReply = describeEmulation( *pEmulation );
                    return true;
                }
                const string& verb = listArgument[0];
                if ( verb == "stats" )
                {
                    outReply = pTool->makeReport( *pSession ).makeLine();
                    return true;
                }
                if ( verb == "spike" )
                {
                    float32    seconds                = 0.0f;
                    const bool bInvalidSpikeArguments = listArgument.size() != 2 || StringUtil::parseFloat( listArgument[1], seconds ) == false || seconds < 0.0f;
                    if ( bInvalidSpikeArguments )
                        return false;
                    for ( int32 shardIndex = 0; shardIndex < pSession->getShardCount(); ++shardIndex )
                    {
                        pTool->find( *pSession, shardIndex )->startOutage( static_cast<float64>( seconds ) );
                    }
                    outReply = "net emulation spike " + listArgument[1] + " s";
                    return true;
                }
                const bool bUp   = verb == "up" || verb == "both";
                const bool bDown = verb == "down" || verb == "both";
                if ( bUp == false && bDown == false )
                    return false;
                size_t                 overrideStart = 1;
                NetEmulationConditions preset;
                const bool             bPreset = listArgument.size() > 1 && listArgument[1].find( '=' ) == string::npos;
                if ( bPreset )
                {
                    NetEmulationPresetTable presetTable;
                    const DataPath&         presetPath = engine::getSubsystem<EngineDefaultAssets>()._netEmulationPresets;
                    if ( presetTable.loadFromAsset( AssetID{ presetPath.get() } ) == false )
                    {
                        outReply = string( "net emulation presets could not be read (" ) + presetPath.get() + ")";
                        return true;
                    }
                    if ( presetTable.tryGetConditions( listArgument[1], preset ) == false )
                    {
                        outReply = "unknown preset '" + listArgument[1] + "' (" + presetTable.makePresetNames() + ")";
                        return true;
                    }
                    overrideStart = 2;
                }
                string overrideText;
                for ( size_t index = overrideStart; index < listArgument.size(); ++index )
                {
                    overrideText += listArgument[index];
                    overrideText += ' ';
                }
                // 방향마다 시작점(프리셋 또는 지금 조건)에 같은 덮어쓰기를 얹는다. 하나라도 틀리면 아무것도 바꾸지 않는다.
                NetEmulationConditions upConditions   = bPreset ? preset : pEmulation->getDefaultConditions();
                NetEmulationConditions downConditions = bPreset ? preset : pEmulation->getIncomingConditions();
                string                 error;
                if ( NetEmulationConditions::parseOverrides( overrideText, upConditions, error ) == false ||
                     NetEmulationConditions::parseOverrides( overrideText, downConditions, error ) == false )
                {
                    outReply = "net.emulation: " + error;
                    return true;
                }
                for ( int32 shardIndex = 0; shardIndex < pSession->getShardCount(); ++shardIndex )
                {
                    NetEmulationTransport* pShardEmulation = pTool->find( *pSession, shardIndex );
                    if ( bUp )
                        pShardEmulation->setDefaultConditions( upConditions );
                    if ( bDown )
                        pShardEmulation->setIncomingConditions( downConditions );
                }
                outReply = describeEmulation( *pEmulation );
                return true;
            }
        };
    } // namespace

    NetLineEmulationSubsystem::NetLineEmulationSubsystem( GameNetSession& session )
        : _pSession{ &session }
        , _arrIssued{}
    {
    }

    NetLineEmulationSubsystem::~NetLineEmulationSubsystem() = default;

    span<const utf8* const> NetLineEmulationSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { GameNetSession::kSubsystemName };
        return kArrDependency;
    }

    SubsystemInitResult NetLineEmulationSubsystem::initializeSubsystem()
    {
        _pSession->setTransportDecorator( SW_DELEGATE_METHOD( GameNetTransportDecoratorDelegate, &NetLineEmulationSubsystem::decorate, this ) );
        return SubsystemInitResult::Succeeded;
    }

    void NetLineEmulationSubsystem::shutdownSubsystem()
    {
        _pSession->setTransportDecorator( GameNetTransportDecoratorDelegate{} ); // 이미 감싼 샤드의 흉내는 세션이 든 채 내려간다(코드는 Core 다)
    }

    unique_ptr<INetTransport> NetLineEmulationSubsystem::decorate( int32 shardIndex, INetTransport& inner )
    {
        if ( shardIndex < 0 || kMaxShardCount <= shardIndex )
            return nullptr;
        NetEmulationConditions conditions = NetEmulationConditions::makeFromGlobalVariables();
        if ( gv_netEmulationPreset.empty() == false )
        {
            NetEmulationPresetTable presetTable;
            conditions = NetEmulationConditions{};
            if ( presetTable.loadFromAsset( AssetID{ engine::getSubsystem<EngineDefaultAssets>()._netEmulationPresets.get() } ) &&
                 presetTable.tryGetConditions( gv_netEmulationPreset, conditions ) == false )
                SW_LOG_ERROR( "gv_netEmulationPreset '%#' is not a preset (%#) - no emulation", gv_netEmulationPreset.c_str(), presetTable.makePresetNames().c_str() );
        }
        unique_ptr<NetEmulationTransport> emulation = make_unique<NetEmulationTransport>( &inner );
        emulation->setDefaultConditions( conditions );
        _arrIssued[shardIndex] = emulation.get();
        return emulation;
    }

    NetEmulationTransport* NetLineEmulationSubsystem::find( GameNetSession& session, int32 shardIndex ) const
    {
        if ( shardIndex < 0 || kMaxShardCount <= shardIndex )
            return nullptr;
        // 세션이 지금 든 데코레이터가 이 서브시스템이 마지막으로 만든 것일 때만 흉내다(형식을 짐작해 바꾸지 않는다).
        const INetTransport* const   pDecorator = session.findTransportDecorator( shardIndex );
        NetEmulationTransport* const pIssued    = _arrIssued[shardIndex];
        return pDecorator != nullptr && pIssued == pDecorator ? pIssued : nullptr;
    }

    NetEmulationReport NetLineEmulationSubsystem::makeReport( GameNetSession& session ) const
    {
        NetEmulationReport report = session.makeConnectionReport();
        for ( int32 shardIndex = 0; shardIndex < session.getShardCount(); ++shardIndex )
        {
            const NetEmulationTransport* const pEmulation = find( session, shardIndex );
            if ( pEmulation == nullptr )
                continue;
            const NetEmulationStats stats = pEmulation->getStats();
            report._sentPacketCount += stats._sentCount;
            report._droppedPacketCount += stats._droppedCount + stats._incomingDroppedCount;
        }
        return report;
    }

    SW_DEV_COMMAND( NetEmulation, "net.emulation", "net.emulation [up|down|both [<preset>] [key=value ...] | spike <seconds> | stats]",
                    "Change the net session's line emulation at run time (presets: off, lan, wifi, lte, badwifi, disconnected)", &NetLineEmulationInternal::runEmulation );
} // namespace sw
