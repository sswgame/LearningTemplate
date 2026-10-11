/**
 * @file AppearanceTypes.h
 * @brief 캐릭터 외형 데이터가 함께 쓰는 작은 타입입니다. 소켓 배치, 소켓 덮어쓰기, 슬롯 요청(해석기 입력), 로드 오류 모음이 있습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/formatString.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/CustomizationValueSet.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @brief 소켓에 붙이는 배치입니다. 소켓 후보 목록(앞에서부터 있는 것을 씀), 로컬 오프셋, 회전(도 단위 오일러 x y z)으로 이루어집니다.
     * @details 소켓 이름은 해석된 외형의 한 이름 공간에 속한다. 몸 소켓(본 이름 포함)은 이름 그대로 쓰고, 장착 부품의 소켓은 주인 이름이 앞에 붙는다
     *          (`MainHand.Muzzle`). 첫 마디가 슬롯이나 꾸미기 매개변수 이름이면 부품 소켓이고, 아니면 몸 소켓이다.
     */
    struct AppearancePlacement
    {
        vector<hashed_string> _listSocket{};
        float3                _offset{};
        float3                _rotation{};

        bool isEmpty() const { return _listSocket.empty(); }
        bool operator==( const AppearancePlacement& other ) const { return _listSocket == other._listSocket && _offset == other._offset && _rotation == other._rotation; }
    };
} // namespace sw

namespace sw
{
    /** @brief 이름 붙은 소켓 하나를 옮기거나 새로 만듭니다. 외형 레이어이며, 스켈레톤과 메시 레이어 위에 이름으로 덮어씁니다. */
    struct AppearanceSocketOverride
    {
        AppearancePlacement _placement{}; ///< `_listSocket` 은 부모 후보(본 이름이나 소켓 이름)
        hashed_string       _name{};
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 슬롯 하나의 요청입니다. 낀 아이템과 그 인스턴스 상태, 보이는 외형 덮어쓰기(형상 변경), 외형 상태를 담으며 해석기의 입력으로 쓰입니다.
     * @details `_damage` 는 맞은 피해와 닳은 내구도 중 큰 쪽(0..1)이다. `AppearanceInputUtil::fillFromEquipment` 가 채운다.
     */
    struct AppearanceSlotRequest
    {
        CustomizationValueSet _customization{};    ///< 아이템 인스턴스의 꾸미기 값
        vector<hashed_string> _listDetachedPart{}; ///< 떨어져 나간 부품
        hashed_string         _slot{};
        hashed_string         _itemID{};                ///< 비어 있으면 빈 슬롯
        hashed_string         _visibleVisual{};         ///< 형상 변경. 비어 있으면 아이템 자신의 외형
        hashed_string         _state{};                 ///< 외형 상태(뽑음, 꽂음, 켬, 끔). 비어 있으면 외형의 기본 상태
        float32               _damage{ 0.0f };          ///< 외형 피해 0..1
        uint8                 _bSuppressed{ SW_FALSE }; ///< 장착 조건이 깨져 숨겨진 상태
    };
} // namespace sw

namespace sw
{
    /**
     * @class AppearanceLoadReport
     * @brief 외형 데이터를 읽고 검사하면서 나온 오류를 모은 것입니다. 오류마다 경고 로그도 남기며, `ResourceDataSchemaTest` 가 그 로그를 확인합니다.
     * @details 편집 창은 이 목록을 그대로 보여 준다. 규칙 충돌과 알 수 없는 이름이 여기에 들어 있다.
     */
    class SW_GF_API AppearanceLoadReport
    {
    public:
        template <typename... Args>
        void addError( string_view format, Args&&... args )
        {
            utf8 arrBuffer[constant::kMaxBuffer1024];
            FormatString::formatstring( arrBuffer, constant::kMaxBuffer1024, format, std::forward<Args>( args )... );
            addMessage( string_view( arrBuffer ) );
        }
        void clear() { _listError.clear(); }

        bool                  hasErrors() const { return _listError.empty() == false; }
        const vector<string>& getErrors() const { return _listError; }
        /** @brief @p text 를 포함한 오류의 수입니다. 테스트에 씁니다. */
        uint32 countContaining( string_view text ) const;
        /** @brief 오류를 줄바꿈으로 연결한 문자열입니다. 테스트 메시지에 씁니다. */
        string joined() const;

    private:
        void addMessage( string_view message );

        vector<string> _listError{};
    };
} // namespace sw
