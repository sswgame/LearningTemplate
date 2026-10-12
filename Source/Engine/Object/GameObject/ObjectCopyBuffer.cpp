#include "pch.h"

#include "Engine/Object/GameObject/ObjectCopyBuffer.h"

namespace sw
{
    namespace
    {
        /** @brief 버퍼 본체입니다. Engine 이미지에 두어 복사하는 모듈의 리로드보다 오래 산다. */
        vector<ObjectCopyEntry>& getStorage()
        {
            static vector<ObjectCopyEntry> s_listEntry;
            return s_listEntry;
        }
    } // namespace

    void ObjectCopyBuffer::setEntries( vector<ObjectCopyEntry>&& listEntry )
    {
        getStorage() = std::move( listEntry );
    }

    const vector<ObjectCopyEntry>& ObjectCopyBuffer::getEntries()
    {
        return getStorage();
    }

    void ObjectCopyBuffer::clear()
    {
        getStorage().clear();
    }
} // namespace sw
