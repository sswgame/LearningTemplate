/**
 * @file CustomizationSchema.h
 * @brief 꾸미기 스키마입니다. 캐릭터와 아이템이 함께 쓰는 매개변수 정의(슬라이더, 색, 고르기, 부착)를 담습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/GameData/CustomizationValueSet.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AppearanceLoadReport;
    class XMLNode;

    /** @brief 매개변수 종류입니다. */
    ENUM()
    enum class CustomizationKind : uint8
    {
        Slider = 0, ///< 범위 안의 수. 모프 가중치, 본 비율, 머티리얼 스칼라에 쓴다
        Color,      ///< rgba 값. 머티리얼 값, 마스크 염색 채널, 스프라이트 팔레트에 쓴다
        Choice,     ///< 항목 하나. 부품(외형), 메시 변형, 머티리얼 변형에 쓴다
        Attachment  ///< 항목 하나. 부품 소켓에 다는 것(총의 조준경, 검의 보석, 데칼)에 쓴다
    };

    /** @brief 슬라이더와 색이 움직이는 대상의 종류입니다. */
    ENUM()
    enum class CustomizationDriveKind : uint8
    {
        Morph = 0,      ///< 모프 가중치(슬라이더)
        BoneProportion, ///< 본 비율 보정(슬라이더, `BoneProportion` 대상 이름)
        MaterialScalar, ///< 머티리얼 스칼라(슬라이더)
        MaterialColor,  ///< 머티리얼 색 값(색)
        DyeChannel,     ///< 마스크 염색 채널 0..3(색)
        PaletteSwap     ///< 스프라이트 팔레트 슬롯(색, 2D 종이 인형)
    };

    /** @brief 슬라이더와 색 값이 움직이는 대상 하나입니다. 슬라이더는 범위 [min, max] 를 [from, to] 로 옮겨서 씁니다. */
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
    /** @brief 고르기와 부착의 항목 하나입니다. 비워 둔 항목은 그 효과가 없다는 뜻입니다. */
    struct CustomizationOptionDef
    {
        float3        _offset{};   ///< 부착 오프셋
        float3        _rotation{}; ///< 부착 회전(도)
        hashed_string _name{};
        hashed_string _visual{};          ///< 고르기: 추가할 외형(머리카락, 수염). 주인은 매개변수 이름이다
        hashed_string _variant{};         ///< 고르기: 주인의 메시 변형 이름
        hashed_string _materialVariant{}; ///< 고르기: 주인의 머티리얼 변형 이름
        hashed_string _asset{};           ///< 부착: 프리팹(3D)이나 스프라이트(2D) 경로
    };
} // namespace sw

namespace sw
{
    /** @brief 다른 매개변수에 건 조건입니다. 맞지 않으면 이 매개변수는 꺼지고 값이 쓰이지 않습니다. */
    struct CustomizationConditionDef
    {
        vector<hashed_string> _listOption{}; ///< 고르기와 부착: 이 항목들 중 하나
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
        hashed_string                     _category{}; ///< 부분 프리셋이 고르는 카테고리("Hair", "Face", "Dye")
        hashed_string                     _symmetry{}; ///< 대칭 그룹. 같은 그룹끼리 함께 바뀐다(좌우 귀)
        hashed_string                     _socket{};   ///< 부착: 주인의 소켓 이름. 주인 이름이 앞에 붙는다
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
    /** @brief 스키마 하나입니다. 캐릭터 종류 하나(사람)나 아이템 종류 하나(소총)의 꾸미기 매개변수를 담습니다. 이 순서가 정규화와 조건 판정의 순서입니다. */
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
     *        <Option name="Full" visual="beard_full"/></Choice><Slider name="BeardLength"><Condition parameter="Beard" options="Full"/></Slider></Schema></CustomizationSchemaCatalog>` 형식으로 읽습니다.
     * @details 조건은 **앞에 선언한** 매개변수만 가리킬 수 있다. 그래서 조건끼리 순환할 수 없고, 정규화가 앞에서 뒤로 한 번에 끝난다.
     */
    class SW_GF_API CustomizationSchemaCatalog
    {
    public:
        [[nodiscard]] bool loadFromNode( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName );
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
     * @brief 꾸미기 값의 정규화, 양자화, 조건, 대칭을 처리합니다.
     * @details **값은 전송 정밀도로 저장한다.** 슬라이더는 범위를 65535 단계, 색은 성분마다 255 단계로 맞춘다(`normalize`). 해석기는 맞춘 값만 쓰므로
     *          네트워크나 공유 코드로 오간 값이 보낸 쪽과 비트까지 같고, 해석 해시도 같다.
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
         * @brief @p values 를 스키마에 맞춰 @p outValues 를 채웁니다. 매개변수마다 정확히 하나를 채우고(없으면 기본값), 범위와 격자에 맞추며, 없는 항목은 기본 항목으로 채웁니다.
         * @details 스키마에 없는 이름은 버리고 @p pOutListDropped 에 담는다. 콘텐츠가 바뀌어 지워진 매개변수가 여기에 해당한다. 결과 순서는 스키마 순서이다.
         */
        static void normalize( const CustomizationSchemaDef& schema, const CustomizationValueSet& values, CustomizationValueSet& outValues, vector<hashed_string>* pOutListDropped );
        /** @brief 매개변수가 켜져 있는지, 즉 조건이 모두 맞는지 판정합니다. @p normalizedValues 는 `normalize` 의 결과입니다. */
        static bool isActive( const CustomizationParamDef& param, const CustomizationSchemaDef& schema, const CustomizationValueSet& normalizedValues );
        /**
         * @brief 값을 넣습니다. @p bSymmetric 이 true 이면 같은 대칭 그룹의 다른 매개변수(같은 종류끼리)에도 같은 값을 넣습니다. 알 수 없는 매개변수이면 false 입니다.
         */
        [[nodiscard]] static bool applyValue( const CustomizationSchemaDef& schema, const CustomizationValue& value, bool bSymmetric, CustomizationValueSet& inoutValues );
        /** @brief 슬라이더 값을 대상에 보내는 출력값입니다. 범위 [min,max] 를 [from,to] 로 옮긴 값입니다. */
        static float32 computeDriveValue( const CustomizationParamDef& param, const CustomizationDriveDef& drive, float32 value );
    };
} // namespace sw
