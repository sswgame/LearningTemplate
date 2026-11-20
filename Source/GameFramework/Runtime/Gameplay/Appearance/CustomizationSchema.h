/**
 * @file CustomizationSchema.h
 * @brief 꾸미기 스키마입니다. 캐릭터와 아이템이 함께 쓰는 매개변수 정의(슬라이더, 색, 고르기, 부착)를 담습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/PrefabAssetKind.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/CustomizationValueSet.h"

namespace sw
{
    class AppearanceLoadReport;
    struct AppearanceSectionDef;
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
    REFLECT()
    struct SW_GF_API CustomizationDriveDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _target{};
        PROPERTY()
        float32 _from{ 0.0f }; ///< 적지 않으면 매개변수 범위의 아래 끝(읽기가 채운 뒤 읽는다)
        PROPERTY()
        float32 _to{ 1.0f };
        PROPERTY( Min = 0, Max = 3 )
        int32 _channel{ 0 }; ///< DyeChannel 0..3
        PROPERTY( Required )
        CustomizationDriveKind _kind{ CustomizationDriveKind::Morph };
        /**
         * @brief 머티리얼 드라이브가 값을 거는 주인입니다(`owner="Hair"` — 몸 스키마의 고르기 매개변수 이름). 비어 있으면 스키마의 주인(몸이면 몸 부품)이다.
         * @details 몸 꾸미기의 고르기(`<Choice name="Hair">`)가 낸 부품은 그 매개변수 이름을 주인으로 가진다 — 머리카락 뿌리 · 끝 색처럼 몸 스키마의 색이
         *          고른 머리 부품의 머티리얼에 닿으려면 이 칸으로 짚는다(몸 머티리얼에 없는 이름을 걸어 아무 데도 닿지 않던 모양을 막는다).
         */
        PROPERTY()
        hashed_string _owner{};
        /**
         * @brief 머티리얼 드라이브(`MaterialScalar` · `MaterialColor` · `DyeChannel`)가 건드릴 구간입니다(`include="cloth" exclude="buckle"` — 구간의 날 이름 · 별칭 · 묶음). 손으로 읽는다.
         * @details 둘 다 비어 있으면 주인의 모든 구간이다. 염색은 `dyeable="false"` 구간을 늘 건너뛴다. 외형에 없는 이름은 로드 오류다(`Appearance.SectionTagUnknown`).
         *          한 구간 안의 마스크 염색 채널(`channel`)은 그대로 쓴다 — 별칭은 어느 구간에 채널을 거는지만 고른다(오른눈 · 왼눈 홍채를 따로 칠하는 이색증도 이 길이다).
         */
        vector<hashed_string> _listInclude{};
        vector<hashed_string> _listExclude{};

        bool isMaterialDrive() const { return _kind == CustomizationDriveKind::MaterialScalar || _kind == CustomizationDriveKind::MaterialColor || _kind == CustomizationDriveKind::DyeChannel; }
        /** @brief 구간을 골라 거는 드라이브면 true 입니다(`include` · `exclude` 를 적었다). */
        bool isSectionTargeted() const { return _listInclude.empty() == false || _listExclude.empty() == false; }
        /** @brief 구간 @p section 이 이 드라이브의 대상이면 true 입니다 — `include`(비면 모두) 안이고 `exclude` 밖이며, 염색이면 `dyeable` 구간이다(해석과 편집 창 강조가 같은 판정을 쓴다). */
        bool isDrivenSection( const AppearanceSectionDef& section ) const;
    };

    /** @brief 매개변수 원소(`<Slider>` · `<Color>` · `<Choice>` · `<Attachment>`)가 함께 받는 칸입니다. 종류마다 받는 칸이 다르다(읽기가 막는다). `default` 는 종류마다 꼴이 달라 손으로 푼다. */
    REFLECT()
    struct SW_GF_API CustomizationParamEntryParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _name{};
        PROPERTY()
        hashed_string _category{};
        PROPERTY()
        hashed_string _symmetry{};
        PROPERTY()
        hashed_string _socket{};
        PROPERTY()
        hashed_string _palette{}; ///< 색 매개변수가 고를 수 있는 견본 묶음(`<Palette id>`). 비어 있으면 자유 색
        PROPERTY()
        float32 _min{ 0.0f };
        PROPERTY()
        float32 _max{ 1.0f };
    };

    /** @brief 선택지(`<Option …/>`)의 칸입니다(파일 스키마 — 회전은 파일 글 그대로 도 단위). */
    REFLECT()
    struct SW_GF_API CustomizationOptionParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _name{};
        PROPERTY()
        hashed_string _visual{};
        PROPERTY()
        hashed_string _variant{};
        PROPERTY()
        hashed_string _materialVariant{};
        /** @brief 소켓에 붙일 프리팹입니다 — 저장 꼴은 GUID 참조(`AssetRefBase`, 같은 이름의 다른 칸과 저장 꼴이 같다). */
        PROPERTY()
        AssetRef<PrefabAsset> _prefab{};
        PROPERTY()
        hashed_string _sprite{};
        PROPERTY( Units = m )
        float3 _offset{};
        PROPERTY( Units = deg )
        float3 _rotation{};
    };

    /** @brief 조건(`<Condition parameter options min max/>`)의 칸입니다. 선택지 목록은 손으로 푼다. 구간은 적지 않으면 대상 슬라이더의 범위다. */
    REFLECT()
    struct SW_GF_API CustomizationConditionParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _parameter{};
        PROPERTY()
        float32 _min{ 0.0f };
        PROPERTY()
        float32 _max{ 0.0f };
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
    /** @brief 스키마 판 올림의 단계 종류입니다(`<Migration>` 의 자식 원소 이름). */
    enum class CustomizationMigrationKind : uint8
    {
        Rename = 0,          ///< `<Rename from to/>` — 매개변수 이름을 바꾼다
        Split,               ///< `<Split from to="A B"/>` — 값 하나를 여러 매개변수에 똑같이 나눠 준다(염색 채널 하나 → 둘)
        PaletteIndexToColor, ///< `<PaletteIndexToColor parameter palette/>` — 슬라이더에 담던 견본 번호를 그 견본의 색으로 바꾼다
        Remove               ///< `<Remove parameter/>` — 값을 지운다(지운 매개변수가 새 이름으로 다시 쓰일 때 옛 값이 끼어들지 않게)
    };

    /** @brief 판 올림 단계 하나입니다. 이름은 공유 코드가 해시만 실은 자리 이름(`#1a2b3c4d`)과도 맞춘다(`CustomizationSchemaDef::matchesStoredName`). */
    struct CustomizationMigrationOp
    {
        vector<hashed_string>      _listTo{};     ///< Rename: 새 이름 하나, Split: 나눠 받을 이름들
        vector<float4>             _listSwatch{}; ///< PaletteIndexToColor: 견본 묶음의 색(읽기가 채운다)
        hashed_string              _from{};       ///< 옛 이름(Rename · Split) 또는 대상 이름(PaletteIndexToColor · Remove)
        hashed_string              _palette{};    ///< PaletteIndexToColor: 견본 묶음 id
        CustomizationMigrationKind _kind{ CustomizationMigrationKind::Rename };
    };

    /** @brief 판 `_toVersion - 1` 의 값을 `_toVersion` 으로 올리는 단계들(`<Migration toVersion>`)입니다. 적은 순서로 한다. */
    struct CustomizationMigrationDef
    {
        vector<CustomizationMigrationOp> _listOp{};
        uint32                           _toVersion{ 0 };

        static constexpr uint32 kFirstVersion = 2; ///< 첫 단계가 올리는 판(판 1 은 판을 두기 전의 값이다)
    };
} // namespace sw

namespace sw
{
    struct CustomizationSchemaDef;

    /** @brief 매개변수 하나입니다. */
    struct SW_GF_API CustomizationParamDef
    {
        vector<CustomizationDriveDef>     _listDrive{};
        vector<CustomizationOptionDef>    _listOption{};
        vector<CustomizationConditionDef> _listCondition{};
        vector<float4>                    _listSwatch{}; ///< 견본 묶음의 색(`palette` — 읽기가 채운다). 있으면 정규화가 가장 가까운 견본으로 맞춘다
        float4                            _defaultColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        hashed_string                     _name{};
        hashed_string                     _category{}; ///< 부분 프리셋이 고르는 카테고리("Hair", "Face", "Dye")
        hashed_string                     _symmetry{}; ///< 대칭 그룹. 같은 그룹끼리 함께 바뀐다(좌우 귀)
        hashed_string                     _socket{};   ///< 부착: 주인의 소켓 이름. 주인 이름이 앞에 붙는다
        hashed_string                     _palette{};  ///< 색: 견본 묶음 id(비어 있으면 자유 색)
        hashed_string                     _defaultOption{};
        float32                           _min{ 0.0f };
        float32                           _max{ 1.0f };
        float32                           _default{ 0.0f };
        CustomizationKind                 _kind{ CustomizationKind::Slider };

        static constexpr uint32 kSliderSteps = 65535u;
        static constexpr uint32 kColorSteps  = 255u;

        const CustomizationOptionDef* findOption( const hashed_string& option ) const;
        /** @brief 이 매개변수의 기본값입니다. */
        CustomizationValue makeDefaultValue() const;
        /** @brief 슬라이더 값을 범위로 자르고 격자 번호로 바꿉니다. */
        uint32 quantizeSlider( float32 value ) const;
        /** @brief 격자 번호를 값으로 바꿉니다. */
        float32        dequantizeSlider( uint32 step ) const;
        static uint32  quantizeColorChannel( float32 value );
        static float32 dequantizeColorChannel( uint32 step );
        /** @brief 색 @p color 를 견본 묶음에서 가장 가까운 색으로 맞춥니다(rgba 거리). 견본이 없으면 그대로입니다 — 염색 · 홍채 · 머리 색이 팔레트만 고르게 한다. */
        float4 snapToSwatch( const float4& color ) const;
        /** @brief 이 매개변수가 켜져 있는지, 즉 조건이 모두 맞는지 판정합니다. @p normalizedValues 는 `CustomizationSchemaDef::normalize` 의 결과입니다. */
        bool isActive( const CustomizationSchemaDef& schema, const CustomizationValueSet& normalizedValues ) const;
        /** @brief 슬라이더 값을 대상에 보내는 출력값입니다. 범위 [min,max] 를 [from,to] 로 옮긴 값입니다. */
        float32 computeDriveValue( const CustomizationDriveDef& drive, float32 value ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 스키마 하나입니다. 캐릭터 종류 하나(사람)나 아이템 종류 하나(소총)의 꾸미기 매개변수를 담습니다. 이 순서가 정규화와 조건 판정의 순서입니다.
     * @details **판**(`version`, 적지 않으면 1): 매개변수의 뜻이 바뀌면(이름 · 채널 나눔 · 팔레트 번호가 색으로) 판을 올리고 `<Migration toVersion>` 에 옛 값을 옮기는 단계를 적는다.
     *          세이브 · 공유 코드의 값은 쓴 판을 함께 싣고, 읽는 쪽이 `migrate` 로 지금 판까지 올린다 — 언리얼 `FCustomVersion`(데이터가 쓴 판을 싣고 읽는 쪽이 판으로 가른다) ·
     *          유니티 `ISerializationCallbackReceiver::OnAfterDeserialize`(읽은 뒤 옛 칸을 새 칸으로 옮긴다)와 같은 자리를, 분기 코드 대신 데이터 단계로 둔다.
     */
    REFLECT()
    struct SW_GF_API CustomizationSchemaDef
    {
        REFLECT_BODY();

        vector<CustomizationParamDef>     _listParameter{};
        vector<CustomizationMigrationDef> _listMigration{}; ///< 판 2..`_version` 의 단계(`_listMigration[n]` 은 판 n+2 로 올린다 — 읽기가 빈 판 없이 맞춘다)
        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Min = 1 )
        int32 _version{ 1 };

        uint32 getVersion() const { return static_cast<uint32>( _version ); }
        /**
         * @brief @p values 를 이 스키마의 지금 판으로 올려 @p outValues 에 담습니다. 판을 찍지 않은 값(0)과 지금 판의 값은 단계 없이 옮기고, 결과에는 지금 판을 찍습니다.
         * @details 지금 판에 뜻이 없는 값(단계를 지나고도 없는 이름 · 견본 밖 번호)은 그 값만 빼고 이름을 @p pOutListDropped 에 담는다 — 나머지는 읽는다(패치가 세이브 전체를 버리지 않게).
         *          세이브 · 공유 코드를 읽는 쪽이 빠진 것을 오류 로그로 남긴다. 정상 콘텐츠에서는 생기지 않는다(쿠킹 검사 `customization-release`).
         * @return 이 스키마보다 새 판(다른 빌드 · 더 새 콘텐츠가 쓴 값 — 다운그레이드)이면 까닭이고 @p outValues 는 그대로입니다. 원본을 덮지 않는다.
         */
        [[nodiscard]] expected<void> migrate( const CustomizationValueSet& values, CustomizationValueSet& outValues, vector<hashed_string>* pOutListDropped = nullptr ) const;
        /** @brief 저장된 이름 @p stored 가 @p name 인지 봅니다. 이름이 같거나, 공유 코드가 모르는 해시로 둔 자리 이름(`makeHashPlaceholder`)이 @p name 의 해시와 같으면 true 입니다. */
        static bool matchesStoredName( const hashed_string& stored, const hashed_string& name );
        /** @brief 받는 쪽이 모르는 32 비트 이름 해시의 자리 이름(`#1a2b3c4d`)입니다. 0 이면 빈 이름입니다. 공유 코드 읽기가 두고, 펼치기는 "지워진 콘텐츠" 로 보고하며, 판 올림은 옛 이름과 맞춘다. */
        static hashed_string makeHashPlaceholder( uint32 hash );

        const CustomizationParamDef* findParameter( const hashed_string& name ) const;
        /**
         * @brief @p values 를 이 스키마에 맞춰 @p outValues 를 채웁니다. 매개변수마다 정확히 하나를 채우고(없으면 기본값), 범위와 격자에 맞춥니다.
         * @details **값은 전송 정밀도로 저장한다.** 슬라이더는 범위를 65535 단계, 색은 성분마다 255 단계로 맞춘다. 해석기는 맞춘 값만 쓰므로
         *          네트워크나 공유 코드로 오간 값이 보낸 쪽과 비트까지 같고, 해석 해시도 같다.
         *          스키마에 없는 이름은 버리고 @p pOutListDropped 에 담는다. 콘텐츠가 바뀌어 지워진 매개변수가 여기에 해당한다. 결과 순서는 스키마 순서다.
         *          값을 쓰는 입구는 이것 하나다 — 옛 판 값은 먼저 `migrate` 로 올린다(빠진 값도 @p pOutListDropped). 결과에는 지금 판을 찍는다.
         * @return @p values 가 더 새 판이면 까닭이고, @p outValues 는 기본값만 담는다(그리는 쪽은 실패하지 않는다 — 원본은 그대로다).
         */
        [[nodiscard]] expected<void> normalize( const CustomizationValueSet& values, CustomizationValueSet& outValues, vector<hashed_string>* pOutListDropped ) const;
        /**
         * @brief 값을 넣습니다. @p bSymmetric 이 true 이면 같은 대칭 그룹의 다른 매개변수(같은 종류끼리)에도 같은 값을 넣습니다. 알 수 없는 매개변수이면 false 입니다.
         * @details @p inoutValues 가 옛 판이면 먼저 지금 판으로 올리고(새 판 값을 옛 판 집합에 섞지 않는다) 지금 판을 찍습니다. 올리지 못하면 false 이고 @p inoutValues 는 그대로입니다.
         */
        [[nodiscard]] bool applyValue( const CustomizationValue& value, bool bSymmetric, CustomizationValueSet& inoutValues ) const;
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
        [[nodiscard]] bool loadFromXML( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName );
        void               clear()
        {
            _listSchema.clear();
            _listPalette.clear();
        }

        const CustomizationSchemaDef*         findSchema( const hashed_string& id ) const;
        const vector<CustomizationSchemaDef>& getSchemas() const { return _listSchema; }

        /** @brief 견본 묶음 하나(`<Palette id><Swatch name color/></Palette>`)입니다. 스키마보다 앞에 적는다. */
        struct Palette
        {
            vector<hashed_string> _listSwatchName{};
            vector<float4>        _listSwatch{};
            hashed_string         _id{};
        };
        const Palette*         findPalette( const hashed_string& id ) const;
        const vector<Palette>& getPalettes() const { return _listPalette; }

    private:
        vector<CustomizationSchemaDef> _listSchema{};
        vector<Palette>                _listPalette{};
    };
} // namespace sw
