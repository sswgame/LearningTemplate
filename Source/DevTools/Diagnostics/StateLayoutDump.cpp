#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/FileUtil.h"

#include "Engine/Foundation/Commandlet.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Reflection/PropertyInfo.h"
#include "Engine/Reflection/ReflectValueBinding.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Format/DurableRecordTypeList.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    namespace
    {
        /**
         * @brief `--write-state-layouts=<파일>` — 상태 칸(`PROPERTY( RuntimeState )`)이 있는 타입(`states`)과 영속 레코드 형식(`records` — `DurableRecordType`)마다
         *        판(`REFLECT( Version )`)과 칸 배치 글을 JSON 으로 쓴다. 레코드는 Transient 가 아닌 칸 전부다(A 가 싣는 칸).
         * @details `py -3 -m Scripts format-release` 가 출시 스냅숏과 견준다 — 배치를 바꾸고 판을 올리지 않은 것을 출시 뒤 기계가 잡는다. 글의 규칙은 읽기가 보는
         *          배치 해시(`StateIO::computeReflectedLayoutHash`)와 같다: 최상위는 상태 칸만, 안쪽 구조체는 Transient 가 아닌 칸 전부, 차례대로.
         */
        struct StateLayoutDumpInternal
        {
            static void appendLayout( const TypeRegistry& registry, const TypeInfo& type, bool bStateRoleOnly, uint32 depth, string& inoutText )
            {
                if ( depth > constant::reflection::kMaxParentChainDepth )
                    return; // 자기를 담는 구조체 — 상태 읽기가 프로세스를 멈춘다
                for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
                {
                    const bool bCarried = bStateRoleOnly ? prop._metadata->_bRuntimeState == SW_TRUE : prop._metadata->_bTransient == SW_FALSE;
                    if ( bCarried == false )
                        continue;
                    inoutText.append( prop._name.c_str() ).append( ":" ).append( prop._bIsBitField == SW_TRUE ? "bit" : prop._typeName.c_str() );
                    const ReflectValueFacts facts = prop._bIsContainer == SW_TRUE ? prop.resolveElementFacts( registry ) : prop.resolveValueFacts( registry );
                    if ( const TypeInfo* pStruct = prop._bIsBitField == SW_TRUE ? nullptr : facts.findStructType() )
                    {
                        inoutText.append( "{" );
                        appendLayout( registry, *pStruct, false, depth + 1, inoutText );
                        inoutText.append( "}" );
                    }
                    inoutText.append( ";" );
                }
            }

            static bool isRequested( const CommandLineManager& commandLine )
            {
                string path;
                return commandLine.getArgument( CommandLineArgument::WRITE_STATE_LAYOUTS, path ) && path.empty() == false;
            }

            static bool run( CommandletContext& context )
            {
                AbsolutePath outputPath;
                (void)context._commandLine.getArgument( CommandLineArgument::WRITE_STATE_LAYOUTS, outputPath );
                JSONDocument        document;
                const JSONValue     root     = document.makeObject();
                const JSONValue     states   = root.set( "states" );
                const JSONValue     records  = root.set( "records" );
                const TypeRegistry& registry = engine::getSubsystem<TypeRegistry>();
                states.setObject();
                records.setObject();
                registry.forEachType( [&registry, &states]( const TypeInfo& type )
                {
                    string layout;
                    appendLayout( registry, type, true, 0, layout );
                    if ( layout.empty() )
                        return;
                    const JSONValue entry = states.set( type._fullyQualifiedName.view() );
                    entry.setObject();
                    entry.set( "version" ).setUint( type._metadata->getVersion() );
                    entry.set( "layout" ).setString( layout );
                } );
                DurableRecordType::forEach( [&registry, &records]( const TypeInfo& type )
                {
                    string layout;
                    appendLayout( registry, type, false, 0, layout );
                    const JSONValue entry = records.set( type._fullyQualifiedName.view() ); // 모듈 둘이 같은 형식을 올리면 같은 칸을 덮는다
                    entry.setObject();
                    entry.set( "version" ).setUint( type._metadata->getVersion() );
                    entry.set( "layout" ).setString( layout );
                } );
                return document.saveToFile( outputPath, 4 );
            }
        };
    } // namespace

    SW_COMMANDLET( stateLayouts, "StateLayouts", CommandletPriority::kStateLayouts, Any, &StateLayoutDumpInternal::isRequested, &StateLayoutDumpInternal::run );
} // namespace sw
