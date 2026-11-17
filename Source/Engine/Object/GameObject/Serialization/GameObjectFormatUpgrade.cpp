#include "pch.h"

#include "Engine/Object/GameObject/Serialization/GameObjectFormatUpgrade.h"

#include "Core/Container/vector.h"

#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    bool GameObjectFormatUpgrade::renameSiblingIndex( FormatUpgradePayload& payload )
    {
        if ( payload._root.isValid() == false )
            return false;
        // 깊이를 재귀 대신 목록으로 걷는다 — 중첩 오브젝트가 깊은 씬에서도 스택을 쓰지 않는다.
        vector<XMLNode> listPending;
        listPending.push_back( payload._root );
        while ( listPending.empty() == false )
        {
            const XMLNode node = listPending.back();
            listPending.pop_back();
            (void)node.renameAttribute( "_siblingIndex", "_siblingOrder" ); // 없는 노드가 대부분이다
            for ( XMLNode child = node.findChild(); child.isValid(); child = child.findNextSibling() )
                listPending.push_back( child );
        }
        return true;
    }
} // namespace sw
