/**
 * @file WeatherMaterialWriter.h
 * @brief 날씨 값을 머티리얼 파라미터 묶음(`*.matparams.xml`)에 씁니다 — 비가 오면 땅이 젖고 웅덩이가 차고 빗방울 링이 돕니다.
 * @details 날씨(`WeatherSystem` — GameFramework)는 판 상태(`SessionStateComponent`)가 소유하고, 지면 머티리얼(Engine)은 파라미터 묶음(`engine/materials/weather.matparams.xml` 의
 *          Weather · Rain)을 읽는다. Engine 은 날씨를 읽을 수 없어(층 방향) 판 상태가 이 쓰개를 들고 틱마다 민다 — 장면마다 컴포넌트를 놓지 않는다. 날씨를 연 판 상태면
 *          (디렉터 컴포넌트든 키트 게임 모드든) 늘 이어진다. 어느 칸에 어느 날씨 값을 쓰는지는 날씨 카탈로그의 데이터다(`WeatherCatalog::getMaterialBindings` —
 *          언리얼에서 날씨 에셋이 자기 MPC 와 칸을 정하는 방식). 바인딩이 없는 카탈로그(강수가 없는 날씨)는 아무것도 쓰지 않는다.
 *          날씨가 낸 값이 지난번에 쓴 값과 다를 때만 쓴다 — 날씨가 그대로인 동안 개발 명령(`matparam`)으로 바꾼 값은 날씨가 움직일 때까지 남는다.
 *          묶음은 프로세스에 한 벌이라(에셋 캐시) 닫을 때 쓴 칸을 연 때의 값으로 되돌린다 — 다음 씬 · 다음 시나리오가 비 온 땅을 물려받지 않는다.
 *          열기 · 쓰기 · 닫기는 게임 스레드에서 한다(묶음은 게임 스레드만 고친다 — `MaterialParameterCollection`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class MaterialParameterCollection;
    class WeatherCatalog;
    class WeatherSystem;
    struct WeatherMaterialBinding;

    /**
     * @class WeatherMaterialWriter
     * @brief 판 상태의 날씨를 카탈로그의 바인딩대로 머티리얼 파라미터 묶음에 씁니다(파일 머리말 참고).
     */
    class SW_GF_API WeatherMaterialWriter
    {
    public:
        WeatherMaterialWriter();
        ~WeatherMaterialWriter();

        WeatherMaterialWriter( const WeatherMaterialWriter& )            = delete;
        WeatherMaterialWriter& operator=( const WeatherMaterialWriter& ) = delete;

        /**
         * @brief 카탈로그 @p pCatalog 의 묶음을 받고 쓸 칸의 지금 값을 적어 둡니다. 열려 있으면 먼저 닫습니다. 카탈로그가 없거나 바인딩이 없으면 열지 않습니다.
         * @details 묶음을 읽지 못하면 받기가 오류를 남기고, 묶음에 없는 칸은 오류를 남기고 그 칸만 쓰지 않는다.
         */
        void open( const WeatherCatalog* pCatalog );
        /** @brief 날씨 @p weather 의 값으로 바뀐 칸을 씁니다. 열리지 않았으면 아무것도 하지 않습니다. */
        void apply( const WeatherSystem& weather );
        /** @brief 쓴 칸을 연 때의 값으로 되돌리고 묶음을 놓습니다. 멱등입니다. */
        void close();
        /** @brief 쓸 묶음을 쥐고 있는지입니다. */
        bool isOpen() const { return _pCollection != nullptr; }

        /** @brief 칸 @p binding 의 새 값입니다 — 이름이 적힌 채널은 날씨 @p pWeather 의 그 값(날씨가 없으면 0)이고 빈 채널은 @p fileValue 의 그 채널입니다. */
        static float4 computeValue( const WeatherMaterialBinding& binding, const float4& fileValue, const WeatherSystem* pWeather );

    private:
        const WeatherCatalog*                   _pCatalog;       ///< 연 카탈로그(판 상태가 쥔 판 — 판 상태가 이 쓰개보다 오래 산다)
        shared_ptr<MaterialParameterCollection> _pCollection;    ///< 쓰는 묶음(열지 않았으면 nullptr)
        vector<float4>                          _listStartValue; ///< 바인딩과 길이가 같다 — 연 때의 칸 값(빈 채널 · 되돌리기)
        vector<float4>                          _listWritten;    ///< 바인딩과 길이가 같다 — 지난번에 쓴 값(바뀔 때만 쓴다)
        vector<uint8>                           _listUsable;     ///< 바인딩과 길이가 같다 — 묶음에 있는 칸이면 SW_TRUE
        vector<uint8>                           _listHasWritten; ///< 바인딩과 길이가 같다 — 한 번이라도 썼으면 SW_TRUE
    };
} // namespace sw
