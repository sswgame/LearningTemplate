#include "pch.h"

#include "Editor/Common/Workspace/EditorStateStore.h"

namespace sw::editor
{
    EditorStateStore::EditorStateStore()
        : _mutex{}
        , _listEntry{}
    {
    }

    EditorStateStore::~EditorStateStore() = default;

    void* EditorStateStore::findOrCreate( uint64 typeHash, size_t size, CreateFunc pfnCreate )
    {
        ScopedLock<mutex> lock{ _mutex };
        for ( Entry& entry : _listEntry )
        {
            if ( entry._typeHash != typeHash )
                continue;
            // 같은 열쇠에 크기가 다르면 이름이 같은 익명 타입 둘이 한 칸을 쓰는 것이다 — 값이 서로를 덮어쓰기 전에 멈춘다.
            SW_ASSERT( entry._size == size );
            return entry._pHolder->getValue();
        }
        Entry added{ typeHash, size, pfnCreate() };
        void* pValue = added._pHolder->getValue();
        _listEntry.push_back( std::move( added ) );
        return pValue;
    }

    uint32 EditorStateStore::getStateCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return static_cast<uint32>( _listEntry.size() );
    }
} // namespace sw::editor
