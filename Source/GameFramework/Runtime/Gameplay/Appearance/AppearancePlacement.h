/**
 * @file AppearancePlacement.h
 * @brief 외형의 소켓 배치(`AppearancePlacement`)와 소켓 덮어쓰기(`AppearanceSocketOverride`)입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/Radians.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/String/formatString.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/CustomizationValueSet.h"

namespace sw
{
    /**
     * @brief 소켓에 붙이는 배치입니다. 소켓 후보 목록(앞에서부터 있는 것을 씀), 로컬 오프셋, 회전(오일러 x y z — 파일 글은 도)으로 이루어집니다.
     * @details 소켓 이름은 해석된 외형의 한 이름 공간에 속한다. 몸 소켓(본 이름 포함)은 이름 그대로 쓰고, 장착 부품의 소켓은 주인 이름이 앞에 붙는다
     *          (`MainHand.Muzzle`). 첫 마디가 슬롯이나 꾸미기 매개변수 이름이면 부품 소켓이고, 아니면 몸 소켓이다.
     */
    struct AppearancePlacement
    {
        vector<hashed_string> _listSocket{};
        float3                _offset{};
        EulerAngles           _rotation{};

        bool isEmpty() const { return _listSocket.empty(); }
        bool operator==( const AppearancePlacement& other ) const { return _listSocket == other._listSocket && _offset == other._offset && _rotation == other._rotation; }
    };

    /** @brief 이름 붙은 소켓 하나를 옮기거나 새로 만듭니다. 외형 레이어이며, 스켈레톤과 메시 레이어 위에 이름으로 덮어씁니다. */
    struct AppearanceSocketOverride
    {
        AppearancePlacement _placement{}; ///< `_listSocket` 은 부모 후보(본 이름이나 소켓 이름)
        hashed_string       _name{};
    };
} // namespace sw
