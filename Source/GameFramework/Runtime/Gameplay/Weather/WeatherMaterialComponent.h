/**
 * @file WeatherMaterialComponent.h
 * @brief 날씨 값을 머티리얼 파라미터 묶음(`*.matparams.xml`)에 씁니다 — 비가 오면 땅이 젖고 웅덩이가 차고 빗방울 링이 돕니다.
 * @details 날씨(`WeatherSystem` — GameFramework)는 판 상태가 소유하고, 지면 머티리얼(Engine)은 파라미터 묶음(`engine/materials/weather.matparams.xml` 의 Weather · Rain)을
 *          읽는다. Engine 은 날씨를 읽을 수 없어(층 방향) 이 어댑터가 틱마다 밀어 넣는다(`WeatherWaveComponent` 와 같은 자리). 언리얼에서 날씨 액터가 MPC 를
 *          쓰는 방식(Material Parameter Collection Instance)과 같다. 발걸음의 젖은 반응(`SurfaceKindTable::readGroundWetness`)도 같은 묶음을 읽어 보이는 젖음과 같다.
 *          묶음 칸마다 x · y · z · w 를 날씨 값 이름으로 적는다(`WeatherMaterialBinding` — 비면 그 채널은 파일 값 그대로). 날씨가 없는 씬은 값 0 이다.
 *          묶음은 프로세스에 한 벌이라(에셋 캐시) 플레이가 끝나면 쓴 칸을 시작 전 값으로 되돌린다 — 다음 씬 · 다음 시나리오가 비 온 땅을 물려받지 않는다.
 *          쓰기는 틱 뒤 게임 스레드에서 하고(묶음은 게임 스레드만 고친다 — `MaterialParameterCollection`), 날씨가 낸 값이 지난번에 쓴 값과 다를 때만 쓴다 —
 *          날씨가 그대로인 동안 개발 명령(`matparam`)으로 바꾼 값은 날씨가 움직일 때까지 남는다(언리얼 MPC 를 블루프린트가 바뀔 때만 쓰는 것과 같다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    class MaterialParameterCollection;
    class WeatherSystem;

    /** @brief 묶음 칸 하나와 그 채널마다의 날씨 값 이름입니다(비면 그 채널은 파일 값 그대로). */
    REFLECT()
    struct SW_GF_API WeatherMaterialBinding
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Parameter of the collection the weather writes" )
        hashed_string _parameter{};
        PROPERTY( Tooltip = "Weather value (WeatherCatalog <Values>) written to x (empty: the file value)" )
        hashed_string _valueX{};
        PROPERTY( Tooltip = "Weather value written to y (empty: the file value)" )
        hashed_string _valueY{};
        PROPERTY( Tooltip = "Weather value written to z (empty: the file value)" )
        hashed_string _valueZ{};
        PROPERTY( Tooltip = "Weather value written to w (empty: the file value)" )
        hashed_string _valueW{};
    };

    /**
     * @class WeatherMaterialComponent
     * @brief 틱마다 씬의 첫 열린 판 상태의 날씨 값을 묶음 칸에 씁니다(파일 머리말 참고).
     */
    REFLECT( Category = "Environment", DisplayName = "Weather Material Parameters",
             Tooltip = "Writes weather values into a material parameter collection each frame (rain wets the ground, fills puddles, turns rain rings)" )
    class SW_GF_API WeatherMaterialComponent : public Component
    {
    public:
        REFLECT_BODY();

        WeatherMaterialComponent();
        ~WeatherMaterialComponent() override;

        /** @brief 묶음을 받고 쓸 칸의 시작 값을 적어 둡니다. 묶음을 읽지 못하거나 칸이 없으면 오류를 남기고 그 칸을 쓰지 않습니다. */
        void onBeginPlay() override;
        /** @brief 쓴 칸을 시작 값으로 되돌립니다. */
        void onEndPlay() override;
        /** @brief 쓰기를 틱 뒤 게임 스레드에 맡깁니다. */
        void onTick( TickContext& context ) override;

        /** @brief 날씨 값으로 칸을 씁니다(틱 뒤 게임 스레드 · 테스트). 날씨가 없는 씬은 값 0 입니다. */
        void applyWeather( GameObjectManager& manager );

        /** @brief 칸 @p binding 의 새 값입니다 — 이름이 적힌 채널은 날씨 @p pWeather 의 그 값(날씨가 없으면 0)이고 빈 채널은 @p fileValue 의 그 채널입니다. */
        static float4 computeValue( const WeatherMaterialBinding& binding, const float4& fileValue, const WeatherSystem* pWeather );

    private:
        PROPERTY( Required, Category = "Weather", DisplayName = "Collection", Tooltip = "Material parameter collection (*.matparams.xml) the weather drives" )
        string _collectionPath;
        PROPERTY( Category = "Weather", DisplayName = "Bindings", Tooltip = "Collection parameters and the weather values written to their channels" )
        vector<WeatherMaterialBinding> _listBinding;

        shared_ptr<MaterialParameterCollection> _pCollection;    ///< 플레이 동안 쥔 묶음(읽지 못했으면 nullptr)
        vector<float4>                          _listStartValue; ///< `_listBinding` 과 길이가 같다 — 플레이 시작 때 칸 값(빈 채널 · 되돌리기)
        vector<float4>                          _listWritten;    ///< `_listBinding` 과 길이가 같다 — 지난번에 쓴 값(바뀔 때만 쓴다)
        vector<uint8>                           _listUsable;     ///< `_listBinding` 과 길이가 같다 — 묶음에 있는 칸이면 SW_TRUE
        vector<uint8>                           _listHasWritten; ///< `_listBinding` 과 길이가 같다 — 한 번이라도 썼으면 SW_TRUE
    };

} // namespace sw
