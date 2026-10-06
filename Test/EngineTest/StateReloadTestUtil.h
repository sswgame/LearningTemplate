/**
 * @file StateReloadTestUtil.h
 * @brief 플레이 중인 오브젝트의 상태를 그 자리에 다시 읽는 시험 도우미입니다(플레이 중 되돌리기 · 핫 리로드와 같은 길).
 */
#pragma once
#include "Core/Container/vector.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"

namespace sw
{
    /**
     * @struct StateReloadTestUtil
     * @brief 오브젝트 상태를 바이너리로 찍어 같은 오브젝트에 다시 읽습니다.
     * @details 제자리 읽기는 컴포넌트를 모두 지우고 새로 만든 뒤 `onPostLoad` 를 부릅니다. 매니저가 플레이 중이면 새 컴포넌트는 시작 줄에 서고,
     *          다음 틱 단계가 `onBeginPlay` 를 부릅니다 — 여기서는 그 단계를 바로 대신합니다.
     */
    struct StateReloadTestUtil
    {
        /** @brief @p pObject 의 상태를 다시 읽고 새 컴포넌트의 시작을 부릅니다. 찍거나 읽지 못하면 false 입니다. */
        [[nodiscard]] static bool reloadInPlace( GameObject* pObject )
        {
            if ( pObject == nullptr )
                return false;
            vector<uint8> bytes;
            if ( ObjectStateSerializer::saveToBinaryBuffer( pObject, bytes ) == false )
                return false;
            if ( ObjectStateSerializer::loadFromBinaryBuffer( pObject, bytes.data(), bytes.size() ) == 0 )
                return false;
            for ( Component* pComp : pObject->getComponents() )
            {
                if ( pComp != nullptr )
                    pComp->dispatchBeginPlay();
            }
            return true;
        }
    };
} // namespace sw
