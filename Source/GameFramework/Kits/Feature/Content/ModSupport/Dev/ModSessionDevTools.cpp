/**
 * @file ModSessionDevTools.cpp
 * @brief `ModSession` 의 개발 명령 · 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/AssetPackIndex.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/PackStorageSettings.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/AssetRegistry.h"
#include "Engine/Content/ContentHash.h"
#include "Engine/Content/Pack/PackManager.h"
#include "Engine/Content/Pack/PackReader.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModAllowList.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModPackBuilder.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModResolver.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModSession.h"
#include "GameFramework/Runtime/Foundation/GameData/GamePackDataUtil.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct ModSessionDevInternal
        {
            static constexpr size_t kReplyLineLimit = 200; ///< `mod.read` 답의 글자 수 상한

            static bool runStatus( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                const ModSession* const pSession = game::findSubsystem<ModSession>();
                if ( pSession == nullptr )
                {
                    outReply = "mods: no mod session";
                    return true;
                }
                const ModSetDigest&                      digest = pSession->getDigest();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "mods: %# active [%#] config %#", digest._listEntry.size(), digest.toText(), digest._configHash );
                outReply = string( replyBuilder.view() );
                return true;
            }

            static bool runRead( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                string text;
                if ( XMLReadUtil::readText( AssetID{ listArgument[0] }, text ) == false ) // 팩의 카탈로그는 이진 쿠킹본이다
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "mod.read %#: not found", listArgument[0] );
                    outReply = string( replyBuilder.view() );
                    return true;
                }
                string line{ StringUtil::trim( text ) };
                for ( utf8& ch : line )
                {
                    if ( ch == '\r' || ch == '\n' || ch == '\t' )
                        ch = ' ';
                }
                if ( line.size() > kReplyLineLimit )
                    line.resize( kReplyLineLimit );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "mod.read %#: %#", listArgument[0], line );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `mod.pack <id>` — 소스 모드를 배포 형태로 묶어 내보내기 폴더에 둔다(`ModSession::exportPackedMod`). */
            static bool runPack( const vector<string>& listArgument, string& outReply )
            {
                const GameConfig* const pGameConfig = game::findSubsystem<const GameConfig>();
                if ( listArgument.size() != 1 || pGameConfig == nullptr )
                    return false;
                AbsolutePath folder;
                if ( ModSession::exportPackedMod( listArgument[0], pGameConfig->_packRoot.get(), folder ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "mod.pack %#: failed (see the error above)", listArgument[0] );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "mod.pack %#: %#", listArgument[0], folder.str() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            [[nodiscard]] static bool readActiveCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const ModSession* const pSession = game::findSubsystem<ModSession>();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->getDigest()._listEntry.size() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( ModStatus, "mod.status", "mod.status", "Show the mods of this session: count, id@version#content hash list and the config hash", &ModSessionDevInternal::runStatus );
    SW_DEV_COMMAND( ModRead, "mod.read", "mod.read <resource id>", "Read a text resource through the resource entrance (mod packs included) and reply its text on one line",
                    &ModSessionDevInternal::runRead );
    SW_DEV_COMMAND( ModPack, "mod.pack", "mod.pack <mod id>", "Pack the source mod <id> from the Mods folder into Saved/ModPacks/<id> (mod.xml + content.pack) for a shipping package",
                    &ModSessionDevInternal::runPack );
    SW_AUTOMATION_WATCH( modActiveCount, "Mods.ActiveCount", "Mods mounted for this game session", &ModSessionDevInternal::readActiveCount );
} // namespace sw
