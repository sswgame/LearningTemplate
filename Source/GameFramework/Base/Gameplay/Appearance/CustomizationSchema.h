/**
 * @file CustomizationSchema.h
 * @brief 꾸미기 스키마 — 캐릭터와 아이템이 함께 쓰는 매개변수 정의(슬라이더 · 색 · 고르기 · 부착)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/CustomizationValueSet.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AppearanceLoadReport;
    class XmlNode;

    /** @brief 매개변수 종류입니다. */
    enum class CustomizationKind : uint8
    {
        Slider = 0, ///< 범위 안의 수 → 모프 가중치 · 본 비율 · 머티리얼 스칼라
        Color,      ///< rgba → 머티리얼 값 · 마스크 염색 채널 · 스프라이트 팔레트
        Choice,     ///< 항목 하나 → 부품(외형) · 메시 변형 · 머티리얼 변형
        Attachment  ///< 항목 하나 → 부품 소켓에 다는 것(총의 조준경 · 검의 보석 · 데칼)
    };

    /** @brief 슬라이더 · 색이 움직이는 대상의 종류입니다. */
    enum class CustomizationDriveKind : uint8
    {
        Morph = 0,      ///< 모프 가중치(슬라이더)
        BoneProportion, ///< 본 비율 보정(슬라이더 — `BoneProportion` 대상 이름)
        MaterialScalar, ///< 머티리얼 스칼라(슬라이더)
        MaterialColor,  ///< 머티리얼 색 값(색)
        DyeChannel,     ///< 마스크 염색 채널 0..3(색)
        PaletteSwap     ///< 스프라이트 팔레트 칸(색 — 2D 종이 인형)
    };

    SW_GF_API const utf8* toString( CustomizationKind kind );
    SW_GF_API const utf8* toString( CustomizationDriveKind kind );

    /** @brief 슬라이더 · 색 값이 움직이는 것 하나입니다. 슬라이더는 범위 [min, max] 를 [from, to] 로 옮겨 씁니다. */
    struct CustomizationDriveDef
    {
        hashed_string          _target{};
        float32                _from{ 0.0f };
        float32                _to{ 1.0f };
        int32                  _channel{ 0 }; ///< DyeChannel
        CustomizationDriveKind _kind{ CustomizationDriveKind::Morph };
    };
} // namespace sw

namespace sw
{
    /** @brief 고르기 · 부착 항목 하나입니다. 비운 칸은 그 효과가 없다는 뜻입니다. */
    struct CustomizationOptionDef
    {
        float3        _offset{};   ///< 부착 오프셋
        float3        _rotation{}; ///< 부착 회전(도)
        hashed_string _name{};
        hashed_string _visual{};          ///< 고르기: 더할 외형(머리카락 · 수염 — 주인은 매개변수 이름)
        hashed_string _variant{};         ///< 고르기: 주인의 메시 변형 이름
        hashed_string _materialVariant{}; ///< 고르기: 주인의 머티리얼 변형 이름
        hashed_string _asset{};           ///< 부착: 프리팹(3D) · 스프라이트(2D) 경로
    };
} // namespace sw

namespace sw
{
    /** @brief 다른 매개변수에 걸린 조건 — 맞지 않으면 이 매개변수는 꺼져 값이 쓰이지 않습니다. */
    struct CustomizationConditionDef
    {
        vector<hashed_string> _listOption{}; ///< 고르기 · 부착: 이 항목들 중 하나
        hashed_string         _parameter{};
        float32               _min{ 0.0f }; ///< 슬라이더: [min, max]
        float32               _max{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 매개변수 하나입니다. */
    struct SW_GF_API CustomizationParamDef
    {
        vector<CustomizationDriveDef>     _listDrive{};
        vector<CustomizationOptionDef>    _listOption{};
        vector<CustomizationConditionDef> _listCondition{};
        float4                            _defaultColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        hashed_string                     _name{};
        hashed_string                     _category{}; ///< 부분 프리셋이 고르는 묶음("Hair" · "Face" · "Dye")
        hashed_string                     _symmetry{}; ///< 대칭 묶음 — 같은 묶음끼리 함께 바뀐다(좌우 귀)
        hashed_string                     _socket{};   ///< 부착: 주인의 소켓 이름(주인 이름이 앞에 붙는다)
        hashed_string                     _defaultOption{};
        float32                           _min{ 0.0f };
        float32                           _max{ 1.0f };
        float32                           _default{ 0.0f };
        CustomizationKind                 _kind{ CustomizationKind::Slider };

        const CustomizationOptionDef* findOption( const hashed_string& option ) const;
        /** @brief 이 매개변수의 기본값입니다. */
        CustomizationValue makeDefaultValue() const;
    };
} // namespace sw

namespace sw
{
    /** @brief 스키마 하나 — 캐릭터 종류 하나(사람) 또는 아이템 종류 하나(소총)의 꾸미기 매개변수입니다. 순서가 정규화 · 조건 판정 순서입니다. */
    struct SW_GF_API CustomizationSchemaDef
    {
        vector<CustomizationParamDef> _listParameter{};
        hashed_string                 _id{};

        const CustomizationParamDef* findParameter( const hashed_string& name ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class CustomizationSchemaCatalog
     * @brief `<CustomizationSchemaCatalog><Schema id="Human"><Slider name="Height" category="Body" min="0" max="1" default="0.5"><Drive kind="BoneProportion" target="Height"/></Slider>
     *        <Color name="Hair" default="0.2 0.1 0.05 1"><Drive kind="DyeChannel" target="Hair" channel="0"/></Color><Choice name="Beard" default="None"><Option name="None"/>
     *        <Option name="Full" visual="beard_full"/></Choice><Slider name="BeardLength"><Condition parameter="Beard" options="Full"/></Slider></Schema></CustomizationSchemaCatalog>` 입니다.
     * @details 조건은 **앞에 선언한** 매개변수만 가리킵니다 — 그래서 조건끼리 순환할 수 없고, 정규화가 한 번에 앞에서 뒤로 끝납니다.
     */
    class SW_GF_API CustomizationSchemaCatalog
    {
    public:
        [[nodiscard]] bool loadFromNode( const XmlNode& root, AppearanceLoadReport& report, string_view sourceName );
        void               clear() { _listSchema.clear(); }

        const CustomizationSchemaDef*         findSchema( const hashed_string& id ) const;
        const vector<CustomizationSchemaDef>& getSchemas() const { return _listSchema; }

    private:
        vector<CustomizationSchemaDef> _listSchema{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct CustomizationUtil
     * @brief 꾸미기 값의 정규화 · 양자화 · 조건 · 대칭입니다.
     * @details **값은 전송 정밀도로 저장합니다.** 슬라이더는 범위를 65535 칸, 색은 성분마다 255 칸으로 맞춥니다(`normalize`). 해석기는 맞춘 값만 쓰므로
     *          네트워크 · 공유 코드로 오간 값이 보낸 쪽과 비트까지 같아 해석 해시가 같습니다.
     */
    struct SW_GF_API CustomizationUtil
    {
        static constexpr uint32 kSliderSteps = 65535u;
        static constexpr uint32 kColorSteps  = 255u;

        /** @brief 슬라이더 값을 범위로 자르고 격자 번호로 바꿉니다. */
        static uint32 quantizeSlider( const CustomizationParamDef& param, float32 value );
        /** @brief 격자 번호를 값으로 바꿉니다. */
        static float32 dequantizeSlider( const CustomizationParamDef& param, uint32 step );
        static uint32  quantizeColorChannel( float32 value );
        static float32 dequantizeColorChannel( uint32 step );

        /**
         * @brief @p values 를 스키마로 맞춰 @p outValues 를 채웁니다 — 매개변수마다 정확히 하나(없으면 기본값), 범위 · 격자로 맞춤, 없는 항목은 기본 항목.
         * @details 스키마에 없는 이름은 버리고 @p pOutListDropped 에 담습니다(콘텐츠가 바뀌어 지워진 매개변수). 결과 순서는 스키마 순서입니다.
         */
        static void normalize( const CustomizationSchemaDef& schema, const CustomizationValueSet& values, CustomizationValueSet& outValues, vector<hashed_string>* pOutListDropped );
        /** @brief 매개변수가 켜져 있는가 — 조건이 모두 맞는가입니다. @p normalizedValues 는 `normalize` 의 결과입니다. */
        static bool isActive( const CustomizationParamDef& param, const CustomizationSchemaDef& schema, const CustomizationValueSet& normalizedValues );
        /**
         * @brief 값을 넣고, @p bSymmetric 이면 같은 대칭 묶음의 다른 매개변수에도 같은 값을 넣습니다(같은 종류끼리). 모르는 매개변수면 false 입니다.
         */
        [[nodiscard]] static bool applyValue( const CustomizationSchemaDef& schema, const CustomizationValue& value, bool bSymmetric, CustomizationValueSet& inoutValues );
        /** @brief 슬라이더 값의 구동 출력값(범위 [min,max] → [from,to])입니다. */
        static float32 computeDriveValue( const CustomizationParamDef& param, const CustomizationDriveDef& drive, float32 value );
    };
} // namespace sw
