/**
* @file    test_golden_baseline.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++ 重构基线行为固化测试（golden 对拍）
*
* 以当前 Qt 实现为参考实现（oracle），把 TLV 帧编码、JSON 协议编解码、
* SHA-256 哈希、INI 配置序列化与 UUID 格式的输出固化为硬编码字面量。
* Phase 1~4 的纯 C++ 替代实现必须逐字节复现这些字面量，以此证明
* 替换不改变任何对外可观察行为。所有期望值均为硬编码常量，
* 禁止运行时动态生成。
*/

#include <QtTest>

#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QUuid>

#include <vector>

#include "chat_message.h"
#include "frame_codec.h"
#include "protocol.h"
#include "rendezvous_limits.h"
#include "rendezvous_protocol.h"

#include "core/codec/chat_message_core.h"
#include "core/codec/frame_codec_core.h"
#include "core/utils/dir_hasher.h"

namespace {

// 手工按协议规格构造 TLV 帧（大端 Type + 大端 Length + 载荷），独立于被测实现
QByteArray buildFrameManually(quint32 type, const QByteArray &payload)
{
    QByteArray frame;
    frame.reserve(8 + payload.size());
    frame.append(char(type >> 24));
    frame.append(char(type >> 16));
    frame.append(char(type >> 8));
    frame.append(char(type));
    const quint32 length = quint32(payload.size());
    frame.append(char(length >> 24));
    frame.append(char(length >> 16));
    frame.append(char(length >> 8));
    frame.append(char(length));
    frame.append(payload);
    return frame;
}

// Hello 帧载荷样本：设备名 + 协议版本（0x0100 = 256）
QByteArray helloPayload()
{
    return QByteArray(R"({"name":"鸽邮设备","version":256})");
}

// 传输请求帧载荷样本：会话号 / 文件名 / 大小 / 校验和
QByteArray transferReqPayload()
{
    return QByteArray(R"({"session_id":"3f2504e0-4f89-11d3-9a0c-0305e82c3301",)"
                      R"("name":"测试 文件.txt","size":123456,)"
                      R"("sha256":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"})");
}

// 聊天帧载荷样本：中文、emoji 与转义引号混合
QByteArray chatTextPayload()
{
    return QByteArray("你好世界🐦 她说:\"鸽邮\"");
}

// 取消帧载荷样本：错误码 5001（用户拒绝）
QByteArray cancelPayload()
{
    return QByteArray(R"({"error_code":5001,"reason":"用户拒绝"})");
}

}  // namespace

class TestGoldenBaseline : public QObject {
    Q_OBJECT

private slots:
    void testGoldenFrameCodec();
    void testGoldenJson();
    void testGoldenSha256();
    void testGoldenQSettings();
    void testGoldenUuid();
    void testGoldenCoreFrameCodec();
    void testGoldenCoreSha256();
    void testGoldenCoreChatMessage();
};

// TLV 帧编码与解码的基线行为固化
void TestGoldenBaseline::testGoldenFrameCodec()
{
    // 场景一：Hello 帧（type=0x0001，载荷 37 字节）——帧头 8 字节硬编码
    const unsigned char helloHeader[8] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x25};
    const QByteArray helloFrame = FrameCodec::encode(gy::protocol::kTypeHello, helloPayload());
    QCOMPARE(helloFrame.size(), 8 + helloPayload().size());
    for (int i = 0; i < 8; ++i) {
        QCOMPARE(quint8(helloFrame.at(i)), helloHeader[i]);
    }
    QCOMPARE(helloFrame.mid(8), helloPayload());

    // 场景二：TransferRequest 帧（type=0x0101）
    const QByteArray reqFrame = FrameCodec::encode(gy::protocol::kTypeTransferReq, transferReqPayload());
    QCOMPARE(reqFrame, buildFrameManually(gy::protocol::kTypeTransferReq, transferReqPayload()));
    QCOMPARE(quint8(reqFrame.at(3)), 0x01);
    QCOMPARE(quint8(reqFrame.at(4)), 0x00);

    // 场景三：ChatText 帧（type=0x0501），载荷含 UTF-8 中文与 emoji
    const QByteArray chatFrame = FrameCodec::encode(gy::protocol::kTypeChatText, chatTextPayload());
    QCOMPARE(chatFrame.size(), 8 + chatTextPayload().size());
    QCOMPARE(chatFrame.mid(8), chatTextPayload());

    // 场景四：DataChunk 帧（type=0x0201）1MB 全零——长度与帧头硬编码、内容抽样验证
    const QByteArray megaZero(1024 * 1024, char(0));
    const QByteArray chunkFrame = FrameCodec::encode(gy::protocol::kTypeDataChunk, megaZero);
    const unsigned char chunkHeader[8] = {0x00, 0x00, 0x02, 0x01, 0x00, 0x10, 0x00, 0x00};
    QCOMPARE(chunkFrame.size(), 8 + 1024 * 1024);
    for (int i = 0; i < 8; ++i) {
        QCOMPARE(quint8(chunkFrame.at(i)), chunkHeader[i]);
    }
    QVERIFY(chunkFrame.mid(8) == megaZero);

    // 场景四补充：16 字节全零 DataChunk 的完整帧（24 字节）全量硬编码
    const QByteArray tinyZero(16, char(0));
    const QByteArray tinyFrame = FrameCodec::encode(gy::protocol::kTypeDataChunk, tinyZero);
    const unsigned char tinyExpected[24] = {0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x10,
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    QCOMPARE(tinyFrame.size(), 24);
    for (int i = 0; i < 24; ++i) {
        QCOMPARE(quint8(tinyFrame.at(i)), tinyExpected[i]);
    }

    // 场景五：Cancel 帧（type=0x0401，error_code=5001）
    const QByteArray cancelFrame = FrameCodec::encode(gy::protocol::kTypeCancel, cancelPayload());
    QCOMPARE(cancelFrame, buildFrameManually(gy::protocol::kTypeCancel, cancelPayload()));

    // 解码：喂入手工构造帧后 frameReady 交付的类型与载荷逐字节一致
    std::vector<std::pair<quint32, QByteArray>> decoded;
    FrameCodec codec;
    connect(&codec, &FrameCodec::frameReady, &codec,
            [&decoded](quint32 type, const QByteArray &payload) {
                decoded.emplace_back(type, payload);
            });
    codec.feed(helloFrame);
    QCOMPARE(decoded.size(), size_t(1));
    QCOMPARE(decoded[0].first, gy::protocol::kTypeHello);
    QCOMPARE(decoded[0].second, helloPayload());

    // 粘包：两个完整帧拼接后一次喂入，必须按序交付两帧
    decoded.clear();
    FrameCodec glued;
    connect(&glued, &FrameCodec::frameReady, &glued,
            [&decoded](quint32 type, const QByteArray &payload) {
                decoded.emplace_back(type, payload);
            });
    glued.feed(chatFrame + cancelFrame);
    QCOMPARE(decoded.size(), size_t(2));
    QCOMPARE(decoded[0].first, gy::protocol::kTypeChatText);
    QCOMPARE(decoded[0].second, chatTextPayload());
    QCOMPARE(decoded[1].first, gy::protocol::kTypeCancel);
    QCOMPARE(decoded[1].second, cancelPayload());

    // 半包：先喂前半帧头再补齐，帧不丢失
    decoded.clear();
    FrameCodec split;
    connect(&split, &FrameCodec::frameReady, &split,
            [&decoded](quint32 type, const QByteArray &payload) {
                decoded.emplace_back(type, payload);
            });
    split.feed(reqFrame.left(5));
    QCOMPARE(decoded.size(), size_t(0));
    split.feed(reqFrame.mid(5));
    QCOMPARE(decoded.size(), size_t(1));
    QCOMPARE(decoded[0].first, gy::protocol::kTypeTransferReq);
    QCOMPARE(decoded[0].second, transferReqPayload());

    // 超限：控制帧声明超长载荷触发 errorOccurred(FrameTooLarge)，缓冲复位后可继续解码
    std::vector<gy::protocol::ErrorCode> errors;
    FrameCodec overflow;
    connect(&overflow, &FrameCodec::errorOccurred, &overflow,
            [&errors](gy::protocol::ErrorCode code, const QString &) {
                errors.push_back(code);
            });
    connect(&overflow, &FrameCodec::frameReady, &overflow,
            [&decoded](quint32 type, const QByteArray &payload) {
                decoded.emplace_back(type, payload);
            });
    decoded.clear();
    QByteArray forgedHeader(8, char(0));
    forgedHeader[3] = char(0x01);        // type = 0x00000001（控制帧，上限 1MB）
    forgedHeader[4] = char(0x7F);        // length = 0x7F000000，远超上限
    overflow.feed(forgedHeader);
    QCOMPARE(errors.size(), size_t(1));
    QCOMPARE(errors[0], gy::protocol::ErrorCode::FrameTooLarge);
    QCOMPARE(decoded.size(), size_t(0));
    overflow.feed(chatFrame);  // 复位后正常帧不受污染
    QCOMPARE(decoded.size(), size_t(1));
    QCOMPARE(decoded[0].first, gy::protocol::kTypeChatText);
}

// JSON 协议编解码的基线行为固化（聊天消息 + 协调协议）
void TestGoldenBaseline::testGoldenJson()
{
    // —— 聊天消息编码：中文 + emoji + 引号转义，期望值为硬编码字面量 ——
    gy::ChatMessage message;
    message.messageId = QStringLiteral("3f2504e0-4f89-11d3-9a0c-0305e82c3301");
    message.fromDeviceId = QStringLiteral("a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d");
    message.fromName = QStringLiteral("鸽邮测试设备");
    message.content = QStringLiteral("你好\"世界\"🐦 \\路径/测试");
    message.sentAt = QDateTime::fromString(QStringLiteral("2026-10-24T12:34:56.789Z"), Qt::ISODateWithMs);

    QByteArray payload;
    gy::ChatMessageError error = gy::ChatMessageError::None;
    QString errorMessage;
    QVERIFY(gy::ChatMessageCodec::encode(message, &payload, &error, &errorMessage));
    QCOMPARE(error, gy::ChatMessageError::None);
    // Phase 1 纯 C++ 实现必须复现以下字面量（Qt 参考实现输出，已冻结）
    const QByteArray expectedChatJson = QByteArray(R"({"content":"你好\"世界\"🐦 \\路径/测试","from_device_id":"a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d","from_name":"鸽邮测试设备","message_id":"3f2504e0-4f89-11d3-9a0c-0305e82c3301","sent_at":"2026-10-24T12:34:56.789Z"})");
    QCOMPARE(payload, expectedChatJson);

    // 往返：decode(encode(m)) 字段与毫秒时间戳完全一致
    gy::ChatMessage roundTrip;
    QVERIFY(gy::ChatMessageCodec::decode(payload, &roundTrip, &error, &errorMessage));
    QCOMPARE(roundTrip.messageId, message.messageId);
    QCOMPARE(roundTrip.fromDeviceId, message.fromDeviceId);
    QCOMPARE(roundTrip.fromName, message.fromName);
    QCOMPARE(roundTrip.content, message.content);
    QCOMPARE(roundTrip.sentAt.toMSecsSinceEpoch(), message.sentAt.toMSecsSinceEpoch());

    // —— 协调协议 peers 响应：固定 registeredAt 后输出完全确定 ——
    OnlineRegistry::PeerInfo peer1;
    peer1.deviceId = "d1e2f3a4-0b1c-4d2e-9f3a-4b5c6d7e8f90";
    peer1.deviceName = "鸽邮测试端";
    peer1.addresses = {"192.168.1.10", "10.0.0.8"};
    peer1.tcpPort = 35100;
    peer1.discoveryPort = 45678;
    peer1.registeredAtMs = QDateTime::fromString(QStringLiteral("2026-10-24T08:00:00Z"), Qt::ISODate)
                               .toMSecsSinceEpoch();
    OnlineRegistry::PeerInfo peer2;
    peer2.deviceId = "99a8b7c6-d5e4-4f3a-2b1c-0d9e8f7a6b5c";
    peer2.deviceName = "Lab PC";
    peer2.addresses = {"172.16.0.3"};
    peer2.tcpPort = 35110;
    peer2.discoveryPort = 45698;
    peer2.registeredAtMs = QDateTime::fromString(QStringLiteral("2026-10-24T08:05:30Z"), Qt::ISODate)
                               .toMSecsSinceEpoch();

    const nlohmann::json peersObject = RendezvousProtocol::buildPeersResponse({peer1, peer2});
    const QByteArray peersJson = QByteArray(peersObject.dump().c_str());
    // 纯 C++ 实现必须复现以下字面量（Qt 参考实现输出，已冻结）
    const QByteArray expectedPeersJson = QByteArray(R"({"items":[{"addresses":["192.168.1.10","10.0.0.8"],"device_id":"d1e2f3a4-0b1c-4d2e-9f3a-4b5c6d7e8f90","device_name":"鸽邮测试端","discovery_port":45678,"tcp_port":35100,"updated_at":"2026-10-24T08:00:00Z"},{"addresses":["172.16.0.3"],"device_id":"99a8b7c6-d5e4-4f3a-2b1c-0d9e8f7a6b5c","device_name":"Lab PC","discovery_port":45698,"tcp_port":35110,"updated_at":"2026-10-24T08:05:30Z"}],"type":"peers"})");
    QCOMPARE(peersJson, expectedPeersJson);

    // —— register 请求样本（golden 输入）：解析与字段提取行为固化 ——
    const QByteArray registerRequest = QByteArrayLiteral(
        R"({"type":"register","room":"default","token":"测试-令牌","device_id":)"
        R"("d1e2f3a4-0b1c-4d2e-9f3a-4b5c6d7e8f90","device_name":"鸽邮测试端",)"
        R"("addresses":["192.168.1.10","10.0.0.8"],"tcp_port":35100,)"
        R"("discovery_port":45678,"ttl_seconds":30})");
    const nlohmann::json registerObject = nlohmann::json::parse(
        registerRequest.constData(), registerRequest.constData() + registerRequest.size(), nullptr, false);
    std::string parseError;
    QCOMPARE(RendezvousProtocol::parseRequest(registerObject, &parseError),
             RendezvousProtocol::MessageType::Register);
    QCOMPARE(RendezvousProtocol::extractRoom(registerObject), std::string("default"));
    QCOMPARE(RendezvousProtocol::extractToken(registerObject), std::string("测试-令牌"));
    const OnlineRegistry::PeerInfo extracted = RendezvousProtocol::extractPeerInfo(registerObject);
    QCOMPARE(extracted.deviceId, peer1.deviceId);
    QCOMPARE(extracted.deviceName, std::string("鸽邮测试端"));
    QCOMPARE(extracted.addresses, peer1.addresses);
    QCOMPARE(extracted.tcpPort, quint16(35100));
    QCOMPARE(extracted.discoveryPort, quint16(45678));
    QCOMPARE(extracted.ttlSeconds, 30);

    // 服务端宽松取值与夹紧行为：discovery_port 缺省回落 45678、ttl 缺省 30、自报超限夹紧到 300
    const QByteArray looseRegister = QByteArrayLiteral(
        R"({"type":"register","room":"r2","device_id":"x","device_name":"y",)"
        R"("addresses":["1.2.3.4"],"tcp_port":40000,"ttl_seconds":99999})");
    const nlohmann::json looseObject = nlohmann::json::parse(
        looseRegister.constData(), looseRegister.constData() + looseRegister.size(), nullptr, false);
    const OnlineRegistry::PeerInfo loose = RendezvousProtocol::extractPeerInfo(looseObject);
    QCOMPARE(loose.discoveryPort, quint16(45678));
    QCOMPARE(loose.ttlSeconds, gy::rendezvous::kMaxPeerTtlSeconds);

    // —— list_peers 请求样本：类型识别固化 ——
    const QByteArray listRequest = QByteArrayLiteral(
        R"({"type":"list_peers","room":"default","device_id":"d1e2f3a4-0b1c-4d2e-9f3a-4b5c6d7e8f90"})");
    const nlohmann::json listObject = nlohmann::json::parse(
        listRequest.constData(), listRequest.constData() + listRequest.size(), nullptr, false);
    QCOMPARE(RendezvousProtocol::parseRequest(listObject, &parseError),
             RendezvousProtocol::MessageType::ListPeers);
}

// SHA-256 哈希基线固化：期望值为硬编码十六进制串（小写）
void TestGoldenBaseline::testGoldenSha256()
{
    struct Case {
        const char *name;
        QByteArray data;
        const char *expectedHex;
    };
    const QList<Case> cases = {
        {"empty", QByteArray(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"ascii", QByteArray("GridYard"), "853ab8d2d4e3d824e096f4bce27a3da812dbc46ccdf072770a48e74870227666"},
        {"4096x0xFF", QByteArray(4096, char(0xFF)), "f47a8ec3e9aff2318d896942282ad4fe37d6391c82914f54a5da8a37de1300c6"},
        // QByteArray(const char*, int) 保留嵌入 NUL，验证字节容器 NUL 语义
        {"embedded-nul", QByteArray("ab\0cd", 5), "1bd95cf6379b94fd3b6ceb1390b70b822c76442c4bfb8273b941e09d8dfd9b56"},
    };
    for (const Case &c : cases) {
        const QByteArray hex = QCryptographicHash::hash(c.data, QCryptographicHash::Sha256).toHex();
        const QString failMessage = QString::fromUtf8("SHA-256 场景 %1 输出与基线不符").arg(QString::fromUtf8(c.name));
        QVERIFY2(hex == QByteArray(c.expectedHex), qPrintable(failMessage));
    }

    // 64 字节递增序列（0x00..0x3F）：模拟小文件内容的流式哈希基线
    QByteArray fileBytes;
    for (int i = 0; i < 64; ++i) {
        fileBytes.append(char(i));
    }
    const QByteArray fileHex = QCryptographicHash::hash(fileBytes, QCryptographicHash::Sha256).toHex();
    const QByteArray expectedFileHex = QByteArrayLiteral("fdeab9acf3710362bd2658cdc9a29e8f9c757fcf9811603a8c447cd1d9151108");
    QCOMPARE(fileHex, expectedFileHex);
    QCOMPARE(fileHex, fileHex.toLower());  // toHex 输出必须是小写
}

// QSettings INI 序列化基线固化：写入后原始文本硬编码，供纯 C++ INI 解析器对拍
void TestGoldenBaseline::testGoldenQSettings()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString iniPath = tempDir.path() + "/golden.ini";

    {
        QSettings settings(iniPath, QSettings::IniFormat);
        settings.setValue("device/name", QStringLiteral("鸽邮 设备 名"));
        settings.setValue("flag", true);
        settings.setValue("count", 42);
        settings.setValue("ratio", 3.14);
        settings.setValue("special", QStringLiteral("a=b\\c;d"));
        settings.setValue("targets", QStringList{QStringLiteral("uuid-a111"), QStringLiteral("uuid-b222")});
        settings.setValue("emptyValue", QString());
        settings.sync();
    }

    QFile file(iniPath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString iniText = QString::fromUtf8(file.readAll());
    file.close();
    // Phase 3 纯 C++ ConfigStore 必须能读回该文本并得到相同值（文本内容已冻结）。
    // 关键行为：无 group 键进 [General] 且键按字典序；空串写裸空；含特殊字符的值
    // 以双引号包裹并把反斜杠翻倍；QStringList 以"逗号+空格"连接；中文原样 UTF-8。
    const QString expectedIni = QStringLiteral(R"([General]
count=42
emptyValue=
flag=true
ratio=3.14
special="a=b\\c;d"
targets=uuid-a111, uuid-b222

[device]
name=鸽邮 设备 名
)");
    QCOMPARE(iniText.trimmed(), expectedIni.trimmed());

    // 读回语义：新实例读到的值与写入完全一致
    QSettings reload(iniPath, QSettings::IniFormat);
    QCOMPARE(reload.value("device/name").toString(), QStringLiteral("鸽邮 设备 名"));
    QCOMPARE(reload.value("flag").toBool(), true);
    QCOMPARE(reload.value("count").toInt(), 42);
    QVERIFY(qFuzzyCompare(reload.value("ratio").toDouble(), 3.14));
    QCOMPARE(reload.value("special").toString(), QStringLiteral("a=b\\c;d"));
    const QStringList expectedTargets = QStringList{QStringLiteral("uuid-a111"), QStringLiteral("uuid-b222")};
    QCOMPARE(reload.value("targets").toStringList(), expectedTargets);
}

// UUID 生成与校验格式基线固化
void TestGoldenBaseline::testGoldenUuid()
{
    // createUuid 输出：无花括号 36 字符、全小写、v4 版本位与变体位
    const QRegularExpression v4Pattern(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"));
    for (int i = 0; i < 100; ++i) {
        const QUuid uuid = QUuid::createUuid();
        const QString plain = uuid.toString(QUuid::WithoutBraces);
        QCOMPARE(plain.size(), 36);
        QVERIFY(v4Pattern.match(plain).hasMatch());
        // 默认 toString 带花括号共 38 字符
        QCOMPARE(uuid.toString().size(), 38);
    }

    // 全零 UUID 与非法串按 NULL 处理（chat_message 校验依赖该语义）
    QVERIFY(QUuid(QStringLiteral("00000000-0000-0000-0000-000000000000")).isNull());
    QVERIFY(QUuid(QStringLiteral("not-a-uuid")).isNull());
    // 大写输入可识别（协议比对不区分大小写）
    QVERIFY(!QUuid(QStringLiteral("3F2504E0-4F89-11D3-9A0C-0305E82C3301")).isNull());
    QVERIFY(!QUuid(QStringLiteral("3f2504e0-4f89-11d3-9a0c-0305e82c3301")).isNull());
}

// 纯 C++ FrameCodecCore 与 Qt 版输出的双向对拍（Phase 1 影子层验收）
void TestGoldenBaseline::testGoldenCoreFrameCodec()
{
    // 帧格式常量与 protocol.h 锁定一致（编译期锚点）
    static_assert(gy::core::frame::kHeaderSize == gy::protocol::kHeaderBytes,
                  "帧头长度与 protocol.h 不一致");
    static_assert(gy::core::frame::kTypeDataChunk == gy::protocol::kTypeDataChunk,
                  "DataChunk 类型码与 protocol.h 不一致");
    static_assert(gy::core::frame::kMaxControlPayload == gy::protocol::kMaxControlPayloadBytes,
                  "控制帧上限与 protocol.h 不一致");
    static_assert(gy::core::frame::kMaxDataPayload == gy::protocol::kMaxDataPayloadBytes,
                  "数据帧上限与 protocol.h 不一致");

    // 场景一：五类帧的编码输出与 Qt 版逐字节一致
    const QList<QPair<quint32, QByteArray>> cases = {
        {gy::protocol::kTypeHello, helloPayload()},
        {gy::protocol::kTypeTransferReq, transferReqPayload()},
        {gy::protocol::kTypeChatText, chatTextPayload()},
        {gy::protocol::kTypeCancel, cancelPayload()},
        {gy::protocol::kTypeDataChunk, QByteArray(16, char(0))},
    };
    for (const auto &item : cases) {
        const QByteArray qtFrame = FrameCodec::encode(item.first, item.second);
        const std::vector<uint8_t> qtBytes(qtFrame.cbegin(), qtFrame.cend());
        const std::vector<uint8_t> coreBytes = gy::core::FrameCodecCore::build(
            item.first, std::vector<uint8_t>(item.second.cbegin(), item.second.cend()));
        QVERIFY(qtBytes == coreBytes);
    }

    // 场景二：core 解码 Qt 编码的粘包帧流，类型与载荷逐字节一致
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> decoded;
    gy::core::FrameCodecCore codec(
        [&decoded](uint32_t type, std::vector<uint8_t> payload) {
            decoded.emplace_back(type, std::move(payload));
        },
        [](std::string) {});
    const QByteArray qtChat = FrameCodec::encode(gy::protocol::kTypeChatText, chatTextPayload());
    const QByteArray qtCancel = FrameCodec::encode(gy::protocol::kTypeCancel, cancelPayload());
    const QByteArray gluedStream = qtChat + qtCancel;
    codec.feed(reinterpret_cast<const uint8_t *>(gluedStream.constData()),
               static_cast<size_t>(gluedStream.size()));
    QCOMPARE(decoded.size(), size_t(2));
    QCOMPARE(decoded[0].first, gy::protocol::kTypeChatText);
    // 先取局部副本再取迭代器：直接链式调用两个临时对象的 cbegin/cend 属未定义行为
    const QByteArray expectedChat = chatTextPayload();
    QVERIFY(decoded[0].second == std::vector<uint8_t>(expectedChat.cbegin(), expectedChat.cend()));
    QCOMPARE(decoded[1].first, gy::protocol::kTypeCancel);
    const QByteArray expectedCancel = cancelPayload();
    QVERIFY(decoded[1].second == std::vector<uint8_t>(expectedCancel.cbegin(), expectedCancel.cend()));

    // 场景三：超限触发错误回调并复位，与 Qt 版行为一致（复位后正常帧可解）
    int errorCount = 0;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> recovered;
    gy::core::FrameCodecCore overflow(
        [&recovered](uint32_t type, std::vector<uint8_t> payload) {
            recovered.emplace_back(type, std::move(payload));
        },
        [&errorCount](std::string) { ++errorCount; });
    QByteArray forgedHeader(8, char(0));
    forgedHeader[3] = char(0x01);
    forgedHeader[4] = char(0x7F);
    overflow.feed(reinterpret_cast<const uint8_t *>(forgedHeader.constData()), 8);
    QCOMPARE(errorCount, 1);
    QCOMPARE(recovered.size(), size_t(0));
    overflow.feed(reinterpret_cast<const uint8_t *>(qtChat.constData()),
                  static_cast<size_t>(qtChat.size()));
    QCOMPARE(recovered.size(), size_t(1));
    QCOMPARE(recovered[0].first, gy::protocol::kTypeChatText);
}

// 纯 C++ 哈希工具与 QCryptographicHash 的对照（picosha2 与流式实现双路验证）
void TestGoldenBaseline::testGoldenCoreSha256()
{
    // 字节串（picosha2 一次性接口）五向量，期望值与 testGoldenSha256 完全相同
    QByteArray fileBytes;
    for (int i = 0; i < 64; ++i) {
        fileBytes.append(char(i));
    }
    const QList<QPair<QByteArray, QByteArray>> cases = {
        {QByteArray(""), QByteArray("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")},
        {QByteArray("GridYard"), QByteArray("853ab8d2d4e3d824e096f4bce27a3da812dbc46ccdf072770a48e74870227666")},
        {QByteArray(4096, char(0xFF)), QByteArray("f47a8ec3e9aff2318d896942282ad4fe37d6391c82914f54a5da8a37de1300c6")},
        {QByteArray("ab\0cd", 5), QByteArray("1bd95cf6379b94fd3b6ceb1390b70b822c76442c4bfb8273b941e09d8dfd9b56")},
        {fileBytes, QByteArray("fdeab9acf3710362bd2658cdc9a29e8f9c757fcf9811603a8c447cd1d9151108")},
    };
    for (const auto &item : cases) {
        const std::string hex = gy::core::sha256Hex(
            reinterpret_cast<const uint8_t *>(item.first.constData()),
            static_cast<size_t>(item.first.size()));
        QCOMPARE(QByteArray::fromStdString(hex), item.second);
    }

    // 文件流式哈希（内置增量上下文）：64 字节递增与 4096×0xFF 两组临时文件
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto writeTempFile = [&tempDir](const QString &name, const QByteArray &data) -> QString {
        const QString path = tempDir.path() + "/" + name;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != qint64(data.size())) {
            return {};  // 写失败返回空串，由调用方断言捕获
        }
        file.close();
        return path;
    };
    const QString smallPath = writeTempFile(QStringLiteral("small.bin"), fileBytes);
    QVERIFY(!smallPath.isEmpty());
    QCOMPARE(QByteArray::fromStdString(gy::core::sha256HexOfFile(smallPath.toStdString())),
             QByteArray("fdeab9acf3710362bd2658cdc9a29e8f9c757fcf9811603a8c447cd1d9151108"));
    const QString bigPath = writeTempFile(QStringLiteral("big.bin"), QByteArray(4096, char(0xFF)));
    QVERIFY(!bigPath.isEmpty());
    QCOMPARE(QByteArray::fromStdString(gy::core::sha256HexOfFile(bigPath.toStdString())),
             QByteArray("f47a8ec3e9aff2318d896942282ad4fe37d6391c82914f54a5da8a37de1300c6"));
    // 分块尺寸小到 1 字节也必须得到相同摘要（验证流式拼接正确性）
    QCOMPARE(QByteArray::fromStdString(gy::core::sha256HexOfFile(smallPath.toStdString(), 1)),
             QByteArray("fdeab9acf3710362bd2658cdc9a29e8f9c757fcf9811603a8c447cd1d9151108"));
    // 打不开的路径返回空串（与旧实现失败语义一致）
    QVERIFY(gy::core::sha256HexOfFile("/nonexistent/golden/path.bin").empty());
}

// 纯 C++ ChatMessageCore 与 golden JSON 字面量的往返对拍
void TestGoldenBaseline::testGoldenCoreChatMessage()
{
    // golden 字面量与 testGoldenJson 中冻结的完全一致
    const QByteArray goldenJson = QByteArray(R"({"content":"你好\"世界\"🐦 \\路径/测试","from_device_id":"a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d","from_name":"鸽邮测试设备","message_id":"3f2504e0-4f89-11d3-9a0c-0305e82c3301","sent_at":"2026-10-24T12:34:56.789Z"})");

    // 解码 golden JSON：无错误且字段逐项一致
    const gy::core::DecodeResult decoded =
        gy::core::ChatMessageCore::decode(goldenJson.toStdString());
    QCOMPARE(decoded.error, gy::core::ChatCodecError::kNone);
    QCOMPARE(QString::fromStdString(decoded.msg.message_id),
             QStringLiteral("3f2504e0-4f89-11d3-9a0c-0305e82c3301"));
    QCOMPARE(QString::fromStdString(decoded.msg.from_device_id),
             QStringLiteral("a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d"));
    QCOMPARE(QString::fromStdString(decoded.msg.from_name), QStringLiteral("鸽邮测试设备"));
    QCOMPARE(QString::fromStdString(decoded.msg.content), QStringLiteral("你好\"世界\"🐦 \\路径/测试"));
    const int64_t expectedMs = QDateTime::fromString(QStringLiteral("2026-10-24T12:34:56.789Z"),
                                                     Qt::ISODateWithMs)
                                   .toMSecsSinceEpoch();
    QCOMPARE(decoded.msg.sent_at_ms, expectedMs);

    // message_id 第三段为 11d3（非 v4 版本位）：QUuid 语义接受，纯 C++ 校验同样接受
    QVERIFY(gy::core::ChatMessageCore::isValidMessageId(decoded.msg.message_id));

    // 往返：解码结果再编码，输出与 golden 字面量逐字节一致
    const gy::core::EncodeResult encoded = gy::core::ChatMessageCore::encode(decoded.msg);
    QCOMPARE(encoded.error, gy::core::ChatCodecError::kNone);
    QCOMPARE(QByteArray::fromStdString(encoded.json), goldenJson);

    // ISO 时间解析宽松性：省略毫秒与时区后缀的输入可解析且秒级一致
    const gy::core::DecodeResult loose = gy::core::ChatMessageCore::decode(
        std::string(R"({"content":"ok","from_device_id":"d","from_name":"n",)")
                   + R"("message_id":"3f2504e0-4f89-11d3-9a0c-0305e82c3301","sent_at":"2026-10-24T12:34:56"})");
    QCOMPARE(loose.error, gy::core::ChatCodecError::kNone);
    QCOMPARE(loose.msg.sent_at_ms, expectedMs - 789);

    // 错误路径：非法 JSON / 缺字段 / 全零 UUID / 空正文
    const gy::core::DecodeResult badJson = gy::core::ChatMessageCore::decode("{not json");
    QCOMPARE(badJson.error, gy::core::ChatCodecError::kInvalidJson);
    const gy::core::DecodeResult missingField = gy::core::ChatMessageCore::decode(
        R"({"message_id":"3f2504e0-4f89-11d3-9a0c-0305e82c3301"})");
    QCOMPARE(missingField.error, gy::core::ChatCodecError::kMissingField);
    const gy::core::DecodeResult zeroUuid = gy::core::ChatMessageCore::decode(
        R"({"content":"你好","from_device_id":"d","from_name":"n",)"
        R"("message_id":"00000000-0000-0000-0000-000000000000","sent_at":"2026-10-24T12:34:56.789Z"})");
    QCOMPARE(zeroUuid.error, gy::core::ChatCodecError::kInvalidMessageId);
    const gy::core::DecodeResult emptyText = gy::core::ChatMessageCore::decode(
        R"({"content":"  ","from_device_id":"d","from_name":"n",)"
        R"("message_id":"3f2504e0-4f89-11d3-9a0c-0305e82c3301","sent_at":"2026-10-24T12:34:56.789Z"})");
    QCOMPARE(emptyText.error, gy::core::ChatCodecError::kEmptyText);

    // UUID 校验语义对齐 QUuid：全零与非法串无效，大写合法输入有效
    QVERIFY(!gy::core::ChatMessageCore::isValidMessageId("00000000-0000-0000-0000-000000000000"));
    QVERIFY(!gy::core::ChatMessageCore::isValidMessageId("not-a-uuid"));
    QVERIFY(gy::core::ChatMessageCore::isValidMessageId("3F2504E0-4F89-11D3-9A0C-0305E82C3301"));
}

QTEST_GUILESS_MAIN(TestGoldenBaseline)
#include "test_golden_baseline.moc"
