/**
 * @file ServiceRecordUnknownFields.h
 * @brief 영속 레코드가 드는 "이 빌드가 모르는 칸" 입니다.
 * @details 레코드 타입 헤더가 이 가벼운 헤더만 include 한다 — 읽고 쓰는 코덱(`ServiceRecordUtil`)은 서비스 코드가 include 한다.
 */
#pragma once
#include "Core/Container/vector.h"

#include "Engine/Serialization/Base/SchemaMigrate.h"

namespace sw
{
    /**
     * @brief 영속 레코드에서 이 빌드가 모르는 칸(배포 중 새 빌드가 더한 칸)입니다. 레코드 타입의 `_unknownFields` 칸(PROPERTY 가 아니다)이 들고,
     *        `ServiceRecordUtil::readDurable` 이 담아 `ServiceRecordUtil::encode` 가 같은 레코드를 다시 쓸 때 그대로 싣습니다.
     * @details 칸마다 (이름 해시, 기록 타입 해시, 값 바이트)를 받은 그대로 든다. 맨 바깥 칸만이다 — 안쪽 구조체에 새로 더한 칸은 담지 않는다(그 칸을 더할 때는 버전을 올린다).
     *          레코드를 복사해 고쳐 쓰는 서비스 코드는 이 칸을 몰라도 값이 따라간다. 새로 만든 레코드는 비어 있다.
     */
    struct ServiceRecordUnknownFields
    {
        vector<SchemaOrphanValue> _listField{};

        bool isEmpty() const { return _listField.empty(); }
    };
} // namespace sw
