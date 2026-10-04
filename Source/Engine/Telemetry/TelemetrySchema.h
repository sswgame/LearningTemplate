/**
 * @file TelemetrySchema.h
 * @brief 텔레메트리 사건의 스키마(`*.telemetry.xml`) — 사건 id · 분류 · 필드(이름 · 타입 · 필수) · 표본 비율, 그리고 파이프라인 설정(묶음 · 파일 상한 · 회전)입니다.
 * @details 엔진 스키마(`EngineDefaultAssets::_telemetrySchema`)에 게임 스키마(`GameConfig::_telemetrySchema`)를 덧붙입니다. 스키마에 없는 사건 · 필드는
 *          기록되지 않습니다(언리얼 Analytics 의 자유 형식 속성과 달리, 받는 쪽 테이블과 맞는 사건만 나간다 — Unity Analytics 의 사건 스키마와 같은 쪽).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class XmlNode;

    /** @brief 필드 값의 타입입니다. */
    enum class TelemetryFieldType : uint8
    {
        Bool = 0,
        Int,
        Float,
        String
    };

    SW_API const utf8* toString( TelemetryFieldType type );
} // namespace sw

namespace sw
{
    /** @brief 사건 필드 하나입니다. */
    struct TelemetryFieldDef
    {
        hashed_string      _name{};
        TelemetryFieldType _type{ TelemetryFieldType::String };
        uint8              _bRequired{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 사건 종류 하나입니다. */
    struct SW_API TelemetryEventDef
    {
        vector<TelemetryFieldDef> _listField{};
        hashed_string             _id{};
        hashed_string             _category{};
        float32                   _sampleRate{ 1.0f }; ///< 이 사건을 남길 비율(0..1) — 줄에 `sample` 로 적혀 받는 쪽이 1 / 비율로 무게를 준다

        const TelemetryFieldDef* findField( const hashed_string& name ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 파이프라인 설정입니다(`<Pipeline>`). */
    struct TelemetryPipelineSettings
    {
        uint64  _maxTotalBytes{ 2u * 1024u * 1024u }; ///< 보내지 못한 스풀 전체 상한 — 넘으면 가장 오래된 닫힌 파일부터 지운다
        float32 _flushSeconds{ 30.0f };               ///< 이만큼 지나면 모인 사건을 쓴다
        float32 _sessionSampleRate{ 1.0f };           ///< 이 비율의 세션만 무엇이든 남긴다(세션 id 해시로 정한다)
        uint32  _batchEvents{ 32 };                   ///< 이만큼 모이면 바로 쓴다
        uint32  _maxFileBytes{ 256u * 1024u };        ///< 파일 하나의 상한 — 넘으면 닫고(올릴 대상) 새 파일로 회전
        uint32  _maxFiles{ 16 };                      ///< 스풀 파일 수 상한
        uint32  _breadcrumbCount{ 32 };               ///< 크래시 보고에 붙일 최근 사건 수
    };
} // namespace sw

namespace sw
{
    /**
     * @class TelemetrySchema
     * @brief 읽은 스키마 파일들의 합입니다. 파일 하나를 읽다 모르는 원소 · 속성 · 타입, 겹친 사건 · 필드, 범위 밖 값이 나오면 그 파일 전체를 버리고
     *        false 입니다(이미 읽은 것은 그대로).
     * @code
     *     <TelemetrySchema version="1">
     *       <Pipeline batchEvents="32" flushSeconds="30" maxFileBytes="262144" maxFiles="16" maxTotalBytes="2097152" sessionSample="1" breadcrumbs="32"/>
     *       <Event id="progression.waveReached" category="progression" sample="1">
     *         <Field name="wave" type="int" required="true"/>
     *         <Field name="kills" type="int"/>
     *       </Event>
     *     </TelemetrySchema>
     * @endcode
     */
    class SW_API TelemetrySchema
    {
    public:
        TelemetrySchema();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );
        void               clear();

        const TelemetryEventDef*         findEvent( const hashed_string& eventId ) const;
        const vector<TelemetryEventDef>& getEvents() const { return _listEvent; }
        const TelemetryPipelineSettings& getSettings() const { return _settings; }
        int32                            getVersion() const { return _version; }

    private:
        [[nodiscard]] bool loadRoot( const XmlNode& root, string_view sourceName );

        vector<TelemetryEventDef> _listEvent;
        TelemetryPipelineSettings _settings;
        int32                     _version;
    };
} // namespace sw
