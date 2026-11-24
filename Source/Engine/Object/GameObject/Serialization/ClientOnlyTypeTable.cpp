#include "pch.h"

#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Serialization/Inherit/ResourceTags.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    namespace
    {
        struct ClientOnlyTypeTableInternal
        {
            /** @brief 이 프로세스가 전용 서버로 섰으면 true 입니다. 서버 구성은 언제나, 개발 구성(Game)은 `Engine::initialize` 가 정한 역할(`target` 축 — 에디터가 띄운 Server)로 가린다. */
            static bool isServerHost()
            {
                if constexpr ( build::kWithClientCode == false )
                    return true;
                else
                    return engine::areEngineServicesBound() && engine::getSubsystem<ResourceTags>().capture().contains( "target.server" );
            }

            /** @brief 표를 읽어 이름 집합을 만듭니다. 없거나 틀리면 오류를 남기고 빈 집합이다 — 그때는 모르는 형식마다 예전처럼 경고가 남아 빠진 표가 드러난다. */
            static unordered_set<string> load()
            {
                unordered_set<string> setName;
                const string          path = FileUtil::joinPath( ProcessRoots::get()._binary.str(), ClientOnlyTypeTable::kTableFile );
                JSONDocument          document;
                if ( document.loadFromFile( path ) == false )
                {
                    SW_LOG_ERROR_ID( "ClientOnlyTypeTable.TableUnreadable", "Client-only type table %# could not be read - client-only components in scenes are reported as unknown types", path );
                    return setName;
                }
                const JSONValue listType = document.getRoot().get( "listType" );
                if ( listType.isArray() == false )
                {
                    SW_LOG_ERROR_ID( "ClientOnlyTypeTable.TableInvalid", "Client-only type table %# needs listType", path );
                    return setName;
                }
                for ( size_t index = 0; index < listType.size(); ++index )
                {
                    setName.insert( listType.at( index ).asString() );
                }
                return setName;
            }
        };
    } // namespace

    bool ClientOnlyTypeTable::isClientOnlyTypeSkipped( const string_view typeName )
    {
        if constexpr ( build::kWithServerCode == false )
        {
            (void)typeName;
            return false; // 클라이언트는 그 형식을 지었다 — 모르면 진짜 모르는 형식이다
        }
        else
        {
            if ( ClientOnlyTypeTableInternal::isServerHost() == false )
                return false;                                                                   // 클라이언트 호스트는 그 모듈을 올렸다 — 모르면 진짜 모르는 형식이다
            static const unordered_set<string> s_setName = ClientOnlyTypeTableInternal::load(); // 함수 안 정적 변수 — 처음 묻는 스레드가 한 번 읽는다
            return s_setName.find( string( typeName ) ) != s_setName.end();
        }
    }
} // namespace sw
