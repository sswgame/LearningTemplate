/**
 * @file AppearanceTypes.h
 * @brief 캐릭터 외형 데이터가 함께 쓰는 작은 타입 — 소켓 배치, 소켓 덮어쓰기, 칸 요청(해석기 입력), 로드 오류 모음입니다.
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
     * @brief 소켓에 붙이는 자리입니다 — 소켓 후보 목록(앞에서부터 있는 것을 쓴다) + 로컬 오프셋 · 회전(도, 오일러 x y z).
     * @details 소켓 이름은 해석된 외형의 한 이름 공간입니다. 몸 소켓(본 이름 포함)은 그대로, 장착 부품의 소켓은 주인 이름이 앞에 붙습니다
     *          (`MainHand.Muzzle`). 첫 마디가 칸 · 꾸미기 매개변수 이름이면 부품 소켓, 아니면 몸 소켓입니다.
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
    /** @brief 이름 붙은 소켓 하나를 옮기거나 새로 냅니다(외형 층 — 스켈레톤 · 메시 층 위에 이름으로 덮어쓴다). */
    struct AppearanceSocketOverride
    {
        AppearancePlacement _placement{}; ///< `_listSocket` 은 부모 후보(본 · 소켓 이름)
        hashed_string       _name{};
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 칸 하나의 요청 — 낀 아이템과 그 인스턴스 상태, 보이는 외형 덮어쓰기(형상 변경), 외형 상태입니다. 해석기의 입력입니다.
     * @details `_damage` 는 맞은 피해와 닳은 내구도 중 큰 쪽(0..1)입니다(`AppearanceInputUtil::fillFromEquipment` 가 채운다).
     */
    struct AppearanceSlotRequest
    {
        CustomizationValueSet _customization{};    ///< 아이템 인스턴스 꾸미기 값
        vector<hashed_string> _listDetachedPart{}; ///< 떨어져 나간 부품
        hashed_string         _slot{};
        hashed_string         _itemId{};                ///< 비면 빈 칸
        hashed_string         _visibleVisual{};         ///< 형상 변경 — 비면 아이템 자신의 외형
        hashed_string         _state{};                 ///< 외형 상태(뽑음 · 꽂음 · 켬 · 끔) — 비면 외형의 기본 상태
        float32               _damage{ 0.0f };          ///< 외형 피해 0..1
        uint8                 _bSuppressed{ SW_FALSE }; ///< 장착 조건이 깨져 숨김
    };
} // namespace sw

namespace sw
{
    /**
     * @class AppearanceLoadReport
     * @brief 외형 데이터를 읽고 검사하며 나온 오류 모음입니다. 오류마다 경고 로그도 남깁니다(`ResourceDataSchemaTest` 가 그것을 본다).
     * @details 편집 창은 이 목록을 그대로 보여 줍니다(규칙 충돌 · 모르는 이름).
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
        /** @brief @p text 를 담은 오류 수입니다(시험). */
        uint32 countContaining( string_view text ) const;
        /** @brief 오류를 줄바꿈으로 이은 것입니다(시험 메시지). */
        string joined() const;

    private:
        void addMessage( string_view message );

        vector<string> _listError{};
    };
} // namespace sw
