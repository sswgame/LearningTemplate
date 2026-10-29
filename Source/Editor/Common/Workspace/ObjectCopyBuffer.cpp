#include "pch.h"

#include "Editor/Common/Workspace/ObjectCopyBuffer.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Serialization/Format/Archive.h"

#include "ModuleAPI/Service/IModuleStateStore.h"

namespace sw::editor
{
    namespace
    {
        struct ObjectCopyBufferInternal
        {
            /** @brief 맡김 칸의 키입니다. */
            static constexpr string_view kStoreKey = "editor.objectCopyBuffer";
            /** @brief 맡긴 형식의 판입니다. 항목 모양이 바뀌면 올린다(옛 판은 버린다). */
            static constexpr uint32 kVersion = 1;
        };
    } // namespace

    SubsystemInitResult ObjectCopyBuffer::initializeSubsystem()
    {
        // 선택 조회(②): 맡김 칸이 없는 호스트(테스트 · 도구)는 빈 버퍼로 시작한다.
        IModuleStateStore* const pStore = editor::findSubsystem<IModuleStateStore>();
        vector<uint8>            bytes;
        if ( pStore == nullptr || pStore->take( ObjectCopyBufferInternal::kStoreKey, bytes ) == false )
            return SubsystemInitResult::Succeeded;
        Archive archive( bytes.data(), bytes.size() );
        uint32  version{ 0 };
        uint32  count{ 0 };
        archive >> version >> count;
        if ( archive.isOk() == false || version != ObjectCopyBufferInternal::kVersion )
            return SubsystemInitResult::Succeeded;
        vector<ObjectCopyEntry> listEntry( count );
        for ( ObjectCopyEntry& entry : listEntry )
        {
            string name;
            archive >> name >> entry._savedID >> entry._stateBytes >> entry._prefabPath >> entry._bRoot;
            entry._name = hashed_string( name.c_str() );
        }
        if ( archive.isOk() )
            _listEntry = std::move( listEntry );
        return SubsystemInitResult::Succeeded;
    }

    void ObjectCopyBuffer::shutdownSubsystem()
    {
        // 선택 조회(②): 종료 경로 - 맡김 칸이 먼저 풀렸으면 넘길 곳이 없다.
        IModuleStateStore* const pStore = editor::findSubsystem<IModuleStateStore>();
        if ( pStore == nullptr || _listEntry.empty() )
            return;
        Archive archive;
        archive << ObjectCopyBufferInternal::kVersion << static_cast<uint32>( _listEntry.size() );
        for ( const ObjectCopyEntry& entry : _listEntry )
        {
            archive << string_view{ entry._name.c_str() } << entry._savedID << entry._stateBytes << string_view{ entry._prefabPath } << entry._bRoot;
        }
        pStore->store( ObjectCopyBufferInternal::kStoreKey, vector<uint8>( archive.getData(), archive.getData() + archive.getSize() ) );
    }
} // namespace sw::editor
