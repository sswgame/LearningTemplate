#include "pch.h"

#include "Engine/Object/GameObject/ObjectCopyBuffer.h"

#include "Core/Memory/LifetimeScope.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 버퍼 본체입니다. Engine 이미지에 두어 복사하는 모듈의 리로드보다 오래 살고, 엔진 수명 범위가 닫히면(엔진 종료) 담긴 상태와 함께 소멸합니다.
         * @details 복사는 에디터 모듈이 하므로 처음 만드는 쪽의 태그(Editor)로 셉니다. 복사가 없던 실행에서는 만들지 않습니다.
         */
        ScopeOwned<vector<ObjectCopyEntry>> s_objectCopyEntries;
    } // namespace

    void ObjectCopyBuffer::setEntries( vector<ObjectCopyEntry>&& listEntry )
    {
        vector<ObjectCopyEntry>* const pListEntry = s_objectCopyEntries.getOrCreate();
        if ( pListEntry != nullptr )
            *pListEntry = std::move( listEntry );
    }

    const vector<ObjectCopyEntry>& ObjectCopyBuffer::getEntries()
    {
        const vector<ObjectCopyEntry>* const pListEntry = s_objectCopyEntries.find();
        if ( pListEntry != nullptr )
            return *pListEntry;
        // 아직 복사한 적이 없거나 엔진 종료 뒤다. 빈 목록은 할당이 없다.
        static const vector<ObjectCopyEntry> s_listEmpty;
        return s_listEmpty;
    }

    void ObjectCopyBuffer::clear()
    {
        vector<ObjectCopyEntry>* const pListEntry = s_objectCopyEntries.find();
        if ( pListEntry != nullptr )
            *pListEntry = vector<ObjectCopyEntry>{};
    }
} // namespace sw
