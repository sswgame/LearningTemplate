#include "pch.h"

#include "Engine/Object/GameObject/Serialization/ClientOnlyTypeTable.h"

#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/Logger.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    namespace
    {
        struct ClientOnlyTypeTableInternal
        {
            /** @brief 표를 읽어 이름 집합을 만듭니다. 없거나 틀리면 오류를 남기고 빈 집합이다 — 그때는 모르는 형식마다 예전처럼 경고가 남아 빠진 표가 드러난다. */
            static unordered_set<string> load()
            {
                unordered_set<string> setName;
                const string          path = FileUtil::joinPath( ProcessRoots::get()._project.str(), ClientOnlyTypeTable::kTableFile );
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
        if constexpr ( build::kWithClientCode )
        {
            (void)typeName;
            return false; // 클라이언트는 그 형식을 지었다 — 모르면 진짜 모르는 형식이다
        }
        else
        {
            static const unordered_set<string> s_setName = ClientOnlyTypeTableInternal::load(); // 함수 안 정적 변수 — 처음 묻는 스레드가 한 번 읽는다
            return s_setName.find( string( typeName ) ) != s_setName.end();
        }
    }
} // namespace sw
