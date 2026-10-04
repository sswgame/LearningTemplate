/**
 * @file AnimNotifyTable.h
 * @brief 애니메이션 알림 표(`*.notifies.xml`) — 클립의 알림 이름 → 처리기(이름으로 고름) + 인자입니다. 처리기 등록부와 처리기 창구도 여기 있습니다.
 * @details 클립은 알림의 **이름 · 시각 · 길이**만 듭니다(임포트 곁 데이터 `<모델>.clips.json`). 그 이름이 무엇을 하는지(발소리 · 이펙트 · 칼 궤적 판정 ·
 *          카메라 흔들림 · 게임플레이 이벤트 · 모션 워핑 창)는 캐릭터마다 이 표 한 줄이고, 코드에는 처리기 **종류**만 있습니다(언리얼 AnimNotify 클래스 ·
 *          유니티 Animation Event 의 함수 이름 자리). 모르는 처리기 · 인자 · 숫자가 아닌 숫자 · 겹친 알림 이름은 읽기 오류입니다.
 *          @code
 *          <AnimNotifies>
 *            <Notify name="FootL" handler="Footstep" socket="foot.l" distance="0.4" sound="common/audio/footstep_{surface}.wav"/>
 *            <Notify name="Swing" handler="HitWindow" socketA="handslot.r" socketB="SwordTip" radius="0.05" damage="10" impulse="40"/>
 *            <Notify name="Impact" handler="CameraShake" amplitude="0.05" duration="0.25"/>
 *            <Notify name="Taunt" handler="GameplayEvent" event="Taunted"/>
 *          </AnimNotifies>
 *          @endcode
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/AnimPlayback.h"

namespace sw
{
    class AnimNotifyComponent;
    class AnimNotifyHandlerRegistry;
    class CharacterDataReader;
    class GameObject;
    class IAnimNotifyHandler;
    class XmlNode;

    /** @brief 처리기 인자의 종류입니다. 읽을 때 이 종류로 검사합니다(숫자 칸에 글이면 오류). */
    enum class AnimNotifyParamKind : uint8
    {
        Name = 0, ///< 이름(소켓 · 이벤트 · 레이어) — `hashed_string`
        Text,     ///< 글(사운드 · 프리팹 경로)
        Float,    ///< 실수
        Vector,   ///< 세 수 "x y z"
        Bool,     ///< 참거짓
    };

    /** @brief 처리기가 받는 인자 하나의 선언입니다. */
    struct AnimNotifyParamDef
    {
        const utf8*         _pName{ nullptr };
        AnimNotifyParamKind _kind{ AnimNotifyParamKind::Name };
        bool                _bRequired{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 표 한 줄에 적힌 인자 값 하나입니다(종류에 맞는 칸만 씁니다). */
    struct AnimNotifyParam
    {
        hashed_string       _name{};
        hashed_string       _nameValue{};
        string              _text{};
        float3              _vector{};
        float32             _number{ 0.0f };
        AnimNotifyParamKind _kind{ AnimNotifyParamKind::Name };
        bool                _bValue{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 표 한 줄 — 알림 이름 → 처리기 + 인자입니다. 처리기 포인터는 등록부(엔진 수명)의 것입니다. */
    struct SW_API AnimNotifyEntry
    {
        hashed_string             _notify{};
        hashed_string             _handlerName{};
        vector<AnimNotifyParam>   _listParam{};
        const IAnimNotifyHandler* _pHandler{ nullptr };

        /** @brief 이름의 인자입니다. 없으면 nullptr 입니다. */
        const AnimNotifyParam* findParam( const hashed_string& name ) const;
        /** @brief 이름 인자의 값입니다. 없으면 @p fallback 입니다. */
        hashed_string getNameParam( const hashed_string& name, const hashed_string& fallback = hashed_string{} ) const;
        /** @brief 글 인자의 값입니다. 없으면 빈 글입니다. */
        string getTextParam( const hashed_string& name ) const;
        /** @brief 실수 인자의 값입니다. 없으면 @p fallback 입니다. */
        float32 getFloatParam( const hashed_string& name, float32 fallback ) const;
        /** @brief 벡터 인자의 값입니다. 없으면 @p fallback 입니다. */
        float3 getVectorParam( const hashed_string& name, const float3& fallback ) const;
        /** @brief 참거짓 인자의 값입니다. 없으면 @p fallback 입니다. */
        bool getBoolParam( const hashed_string& name, bool fallback ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 알림 표 하나입니다. 파일 머리말 참고. */
    class SW_API AnimNotifyTable
    {
    public:
        /** @brief XML 텍스트에서 읽습니다(지금 내용을 비우고). 처리기는 @p registry 에서 이름으로 찾습니다. 오류가 있으면 모두 로그로 내고 false 입니다. */
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName, const AnimNotifyHandlerRegistry& registry );
        /** @brief 리소스 파일에서 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path, const AnimNotifyHandlerRegistry& registry );

        /** @brief 알림 이름의 줄입니다. 없으면 nullptr 입니다(표에 없는 알림은 처리하지 않는다 — 게임 코드가 목록을 직접 읽을 수 있다). */
        const AnimNotifyEntry* findEntry( const hashed_string& notify ) const;
        /** @brief 줄들입니다. */
        const vector<AnimNotifyEntry>& getEntries() const { return _listEntry; }

    private:
        void readRoot( const XmlNode& root, const AnimNotifyHandlerRegistry& registry, CharacterDataReader& reader );
        void readEntry( const XmlNode& node, const AnimNotifyHandlerRegistry& registry, CharacterDataReader& reader );

        vector<AnimNotifyEntry> _listEntry;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 열린 구간 알림 하나의 상태 칸입니다(처리기가 시작에서 채우고 틱마다 읽습니다). 처리기는 상태를 갖지 않으므로 여기에 둡니다.
     * @details 칼 궤적 판정은 지난 소켓 자리 · 이미 맞힌 오브젝트를, 모션 워핑은 창의 끝 시각을 적습니다.
     */
    struct AnimNotifyStateData
    {
        vector<float3> _listPreviousPoint{};
        vector<uint64> _listHitObjectId{};
        float32        _elapsed{ 0.0f };
        float32        _endTime{ 0.0f };
        uint8          _bHasPrevious{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 처리기 한 번의 호출 문맥입니다. */
    struct AnimNotifyContext
    {
        AnimNotifyComponent&   _component;
        GameObject&            _owner;
        const AnimNotifyEntry& _entry;
        const AnimFiredNotify& _fired;
        AnimNotifyStateData*   _pState{ nullptr }; ///< 구간 알림이면 그 상태 칸, 아니면 nullptr
        float32                _deltaSeconds{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAnimNotifyHandler
     * @brief 알림 처리기 종류 하나입니다. 상태를 갖지 않습니다(구간의 상태는 `AnimNotifyStateData`). 게임 스레드에서 불립니다(틱 밖).
     * @details 길이 없는 알림은 `onNotify`, 구간 알림은 `onNotifyBegin` → 프레임마다 `onNotifyTick` → `onNotifyEnd` 입니다. 구간을 받지 않는 처리기
     *          (`supportsState` 거짓)에 구간 알림이 오면 시작에서 `onNotify` 한 번입니다. 구간 처리기도 길이 없는 알림은 `onNotify` 로 받습니다
     *          (칼 궤적은 한 순간의 칼날 판정, 이벤트는 순간 이벤트).
     */
    class SW_API IAnimNotifyHandler
    {
    public:
        IAnimNotifyHandler()                                       = default;
        virtual ~IAnimNotifyHandler()                              = default;
        IAnimNotifyHandler( const IAnimNotifyHandler& )            = delete;
        IAnimNotifyHandler& operator=( const IAnimNotifyHandler& ) = delete;

        /** @brief 받는 인자 선언입니다. 표의 줄이 이 밖의 인자를 쓰면 읽기 오류입니다. */
        virtual vector_reference<const AnimNotifyParamDef> getParams() const = 0;
        /** @brief 구간 알림(시작 · 틱 · 끝)을 받는지입니다. */
        virtual bool supportsState() const { return false; }
        /** @brief 길이 없는 알림이 울렸습니다. */
        virtual void onNotify( AnimNotifyContext& context ) const { (void)context; }
        /** @brief 구간이 열렸습니다. */
        virtual void onNotifyBegin( AnimNotifyContext& context ) const { (void)context; }
        /** @brief 구간이 열린 채 한 프레임이 지났습니다. */
        virtual void onNotifyTick( AnimNotifyContext& context ) const { (void)context; }
        /** @brief 구간이 닫혔습니다(끝 시각을 지남 · 클립이 빠짐 · 컴포넌트가 끝남). */
        virtual void onNotifyEnd( AnimNotifyContext& context ) const { (void)context; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimNotifyHandlerRegistry
     * @brief 이름 → 처리기 표입니다. `getDefault` 가 엔진 내장 처리기(PlaySound · SpawnPrefab · Footstep · HitWindow · CameraShake · GameplayEvent)
     *        를 듭니다. 더한 처리기는 엔진이 끝날 때까지 삽니다 — 핫 리로드되는 모듈의 처리기는 더하지 말고 이벤트(`GameplayEvent`)로 받습니다.
     */
    class SW_API AnimNotifyHandlerRegistry
    {
    public:
        AnimNotifyHandlerRegistry();
        ~AnimNotifyHandlerRegistry();

        AnimNotifyHandlerRegistry( const AnimNotifyHandlerRegistry& )            = delete;
        AnimNotifyHandlerRegistry& operator=( const AnimNotifyHandlerRegistry& ) = delete;

        /** @brief 내장 처리기를 든 표입니다(엔진 수명). */
        static AnimNotifyHandlerRegistry& getDefault();

        /** @brief 처리기를 이름으로 더합니다. 같은 이름이 있으면 바꿉니다(그 처리기를 가리키던 표는 다시 읽어야 한다). */
        void registerHandler( const hashed_string& name, unique_ptr<IAnimNotifyHandler> handler );
        /** @brief 이름의 처리기입니다. 없으면 nullptr 입니다. */
        const IAnimNotifyHandler* findHandler( const hashed_string& name ) const;
        /** @brief 등록된 이름들입니다(표 순서). */
        void collectHandlerNames( vector<hashed_string>& outListName ) const;

    private:
        struct Entry
        {
            hashed_string                  _name;
            unique_ptr<IAnimNotifyHandler> _handler;
        };
        vector<Entry> _listEntry;
    };
} // namespace sw
