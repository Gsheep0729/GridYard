/**
* @file    config_manager.h
* @version 7.19.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   应用配置管理器（QML 单例）
*
* 使用 QSettings 管理设备名、接收路径、TCP 端口、自动接收等配置。
* 首次启动生成 UUID 并持久化，确保设备标识跨会话稳定。
* 提供语义化方法（isMyDevice、fillHelloPayload 等）供其他模块调用。
*
* Change Log:
 * [v7.19.0] GY   2026-10-05
 * * 新增开发者模式实例号只读属性
 * [v7.18.0] GY   2026-10-05
 * * 新增关窗行为配置 closeWindowAction（ask / hide / exit，默认 ask）与
 *   resolveWindowCloseAction 决策函数，记住退出且有活动传输时拦截为警示确认
 * [v7.17.5] GY   2026-10-04
 * * 版本头对齐到 v7.17.5
* [v7.17.4] GY   2026-10-04
* * 版本头对齐到 v7.17.4
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 版本头对齐到 v7.15.18
* [v7.15.17] GY   2026-10-04
* * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
* [v7.15.13] GY   2026-10-04
* * 版本头对齐到 v7.15.13
* [v7.15.12] GY   2026-10-03
* 版本头对齐到 v7.15.12
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.14.0] GY   2026-10-03
* * 新增协调服务器访问令牌配置项
* [v7.13.0] GY   2026-10-02
* * 放开测试白名单以支持可达性控制器与发现视图模型的用例
* [v7.4.0] GY   2026-07-21
* * 新增 Reachability 配置分组：rendezvousEnabled、rendezvousHost、rendezvousPort、relayMode
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v4.16.1] GY   2026-06-21
* * 新增 isMyDevice()、fillHelloPayload()、fillSenderInfo() 语义化方法
* [v4.11.0] FengChunlin   2026-06-13
* * 新增自动接收并保存文件配置
* [v4.8.1] GY   2026-06-09
* * 修复设备名称更新不及时问题，单例模式实现
* [v0.3.0] FengChunlin   2026-05-19
* * 添加 localIp 属性、refreshLocalIp()、openFolder() 方法
* [v0.2.0] GY   2026-04-26
* * Stage 2：初始版本
*/

#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

// Relay 策略枚举
enum class RelayMode {
    AskBeforeRelay,  // 默认：询问后再中继（适合大文件）
    AutoRelay,       // 自动中继（适合小文件或已确认对方在线）
    NeverRelay       // 从不使用中继
};

// 关窗行为枚举（点击窗口关闭按钮时的默认动作）
enum class CloseWindowAction {
    Ask,   // 默认：每次关窗弹确认窗
    Hide,  // 记住隐藏到后台，关窗不再询问
    Exit   // 记住完全退出；仍有活动传输时仍弹警示确认，不豁免退出防护
};

class ConfigManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString  deviceId    READ deviceId    NOTIFY deviceIdChanged)
    Q_PROPERTY(QString  deviceName  READ deviceName  WRITE setDeviceName  NOTIFY deviceNameChanged)
    Q_PROPERTY(QString  localIp     READ localIp     NOTIFY localIpChanged)
    Q_PROPERTY(QString  receivePath READ receivePath WRITE setReceivePath NOTIFY receivePathChanged)
    Q_PROPERTY(bool     autoAcceptFiles READ autoAcceptFiles WRITE setAutoAcceptFiles NOTIFY autoAcceptFilesChanged)
    Q_PROPERTY(quint16  tcpPort     READ tcpPort     WRITE setTcpPort     NOTIFY tcpPortChanged)
    Q_PROPERTY(int retentionDays READ retentionDays WRITE setRetentionDays NOTIFY retentionDaysChanged)
    Q_PROPERTY(bool     rendezvousEnabled READ rendezvousEnabled WRITE setRendezvousEnabled NOTIFY rendezvousEnabledChanged)
    Q_PROPERTY(QString  rendezvousHost   READ rendezvousHost   WRITE setRendezvousHost   NOTIFY rendezvousHostChanged)
    Q_PROPERTY(int      rendezvousPort   READ rendezvousPort   WRITE setRendezvousPort   NOTIFY rendezvousPortChanged)
    Q_PROPERTY(QString  rendezvousToken  READ rendezvousToken  WRITE setRendezvousToken  NOTIFY rendezvousTokenChanged)
    Q_PROPERTY(RelayMode relayMode       READ relayMode       WRITE setRelayMode       NOTIFY relayModeChanged)
    Q_PROPERTY(CloseWindowAction closeWindowAction READ closeWindowAction WRITE setCloseWindowAction NOTIFY closeWindowActionChanged)
    Q_PROPERTY(int instanceNumber READ instanceNumber CONSTANT)

public:
    static ConfigManager *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    // 关窗动作决策：ask 弹确认窗，hide 直接隐藏到后台，exit 直接退出；
    // 记住完全退出但仍有活动传输时改为弹警示确认（退出防护不随记忆豁免），
    // 返回 ask / hide / exit / confirm 供表现层分发，纯函数便于单测
    static QString resolveWindowCloseAction(CloseWindowAction action, int activeSessionCount);

    // 析构函数
    virtual ~ConfigManager() override;

    // 获取设备 UUID
    QString  deviceId()    const;
    // 获取设备名称
    QString  deviceName()  const;
    // 获取本机 IP 地址
    QString  localIp()     const;
    // 获取文件接收路径
    QString  receivePath() const;
    // 获取自动接收文件配置
    bool     autoAcceptFiles() const;
    // 获取 TCP 端口
    quint16  tcpPort()     const;
    // 获取历史保留天数
    int retentionDays() const;
    // 获取协调服务器启用状态
    bool rendezvousEnabled() const;
    // 获取协调服务器地址
    QString rendezvousHost() const;
    // 获取协调服务器端口
    int rendezvousPort() const;
    // 获取协调服务器访问令牌
    QString rendezvousToken() const;
    // 获取 Relay 策略
    RelayMode relayMode() const;
    // 获取关窗行为配置
    CloseWindowAction closeWindowAction() const;
    // 获取开发者模式实例号（0 = 正常模式，启动时解析后不再变化）
    int instanceNumber() const;

    // 设置设备名称
    void setDeviceName(const QString &name);
    // 设置文件接收路径
    void setReceivePath(const QString &path);
    // 设置自动接收文件开关
    void setAutoAcceptFiles(bool enabled);
    // 设置 TCP 端口
    void setTcpPort(quint16 port);
    // 设置历史保留天数
    void setRetentionDays(int days);
    // 设置协调服务器启用状态
    void setRendezvousEnabled(bool enabled);
    // 设置协调服务器地址
    void setRendezvousHost(const QString &host);
    // 设置协调服务器端口
    void setRendezvousPort(int port);
    // 设置协调服务器访问令牌
    void setRendezvousToken(const QString &token);
    // 设置 Relay 策略
    void setRelayMode(RelayMode mode);
    // 设置关窗行为配置
    void setCloseWindowAction(CloseWindowAction action);

    Q_INVOKABLE void refreshLocalIp();
    Q_INVOKABLE void openFolder(const QString &path);

    // 语义化方法：判断是否是本机设备
    bool isMyDevice(const QString &deviceId) const;
    // 语义化方法：填充 Hello 包数据（委托模式）
    void fillHelloPayload(QJsonObject &json) const;
    // 语义化方法：填充发送方信息到会话
    void fillSenderInfo(QVariantMap &session) const;

signals:
    void deviceIdChanged();
    void deviceNameChanged();
    void localIpChanged();
    void receivePathChanged();
    void autoAcceptFilesChanged();
    void tcpPortChanged();
    void retentionDaysChanged();
    void rendezvousEnabledChanged();
    void rendezvousHostChanged();
    void rendezvousPortChanged();
    void rendezvousTokenChanged();
    void relayModeChanged();
    void closeWindowActionChanged();

private:
    explicit ConfigManager(QObject *parent = nullptr);
    ConfigManager(const ConfigManager &)            = delete;
    ConfigManager &operator=(const ConfigManager &) = delete;

    // 允许测试代码访问私有构造函数
    friend class TestDiscovery;
    friend class TestIntegration;
    friend class TestReachabilityController;
    friend class TestPeerDiscoveryViewModel;
    friend class TestConfigManager;

    void ensureDeviceId();

    // 全局实例指针（用于单例模式）
    static QPointer<ConfigManager> s_instance;

    QString _deviceId;                // 持久化的本机设备 UUID
    QString _deviceName;              // 用户设置的本机显示名称
    QString _localIp;                 // 当前选取的本机 IPv4 地址
    QString _receivePath;             // 接收文件的本地保存目录
    bool    _autoAcceptFiles = false; // 是否跳过接收确认直接保存
    quint16 _tcpPort = 0;             // TCP P2P 服务监听端口
    int _retentionDays = 0;           // 本地历史保留天数，0 表示永久保留
    bool _rendezvousEnabled = false;   // 是否启用协调服务器
    QString _rendezvousHost = "127.0.0.1"; // 协调服务器地址
    int _rendezvousPort = 45780;      // 协调服务器端口（避免与 UDP 发现 45678 混淆）
    QString _rendezvousToken;         // 协调服务器访问令牌，空表示服务器未启用认证
    RelayMode _relayMode = RelayMode::AskBeforeRelay; // Relay 策略
    CloseWindowAction _closeWindowAction = CloseWindowAction::Ask; // 关窗行为，默认每次询问
    int _instanceNumber = 0;          // 开发者模式实例号，0 表示正常模式（仅影响本机资源隔离）
};
