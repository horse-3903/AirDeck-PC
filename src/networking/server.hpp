#pragma once

#include "../settings/settings_singleton.hpp"
#include "executor.hpp"

#include <QByteArray>
#include <QDialog>
#include <QFrame>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QSpacerItem>
#include <QEvent>
#include "../ui/controllertester.hpp"
#include <QTcpServer>
#include <QTcpSocket>
#include <QTime>
#include <memory>
#include <functional>
#include <vector>

namespace Ui
{
class Server;
}

class Server : public QWidget
{
	Q_OBJECT

  signals:
	void navigateBack();
	void homeRequested();
	void statusChanged(const QString &text);

  public:
	explicit Server(QWidget *parent = nullptr);
	~Server() override;
	QTcpServer *tcpServer = nullptr;
	Q_INVOKABLE bool isServerRunning() const { return !stopped; }
	// Called after Preferences are saved: a running server restarts so the new settings apply
	Q_INVOKABLE void applySettings();

  private slots:
	void handleConnection();
	void destroyServer();

  private:
	void initServer();
	void updateClientLabel();
	void updateSlots();
	void updateStatus();
	int pickSlot(const ExecutorInterface *executor) const;
	void serveClient();

	Ui::Server *ui;
	struct Client
	{
		QTcpSocket *socket = nullptr;
		QByteArray buffer;
		std::unique_ptr<ExecutorInterface> executor;
		QString name;
		QString model;
		int slot = 0;
		enum class Protocol
		{
			Unknown,
			Native,
			Http,
			WebSocket
		};
		Protocol protocol = Protocol::Unknown;
		// An Xbox pad that belongs to the server, not to this phone, so it stays plugged in between connections
		ExecutorInterface *pad = nullptr;
		bool active = false;
		bool ds4 = false;
		quint32 held = 0;
		float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
	};
	struct SlotCard
	{
		QLabel *badge = nullptr;
		QLabel *title = nullptr;
		QLabel *detail = nullptr;
		QLabel *state = nullptr;
		QPushButton *test = nullptr;
		QPushButton *disconnect = nullptr;
		QLabel *ip = nullptr;
		QLabel *input = nullptr;
		QLabel *deviceIcon = nullptr;
		QLabel *srcIcon = nullptr;
		QLabel *srcText = nullptr;
		QLabel *outIcon = nullptr;
		QLabel *outText = nullptr;
		QFrame *frame = nullptr;
		QWidget *details = nullptr;
		QWidget *chips = nullptr;
		ControllerTester *live = nullptr;
		QSpacerItem *padSpacer = nullptr;
		bool occupied = false;
		bool iconPs4 = false;
		QColor iconColor;
	};
	std::vector<SlotCard> slotCards;
	ExecutorInterface *executorFor(Client &client);
	bool activate(Client &client, QString *failure);
	void refuse(Client &client, const QString &why);
	void serveNative(Client &client);
	void serveHttp(Client &client);
	void serveWebSocket(Client &client);
	void handleWebMessage(Client &client, const QByteArray &payload);
	void sendWebFrame(QTcpSocket *socket, const QByteArray &payload, int opcode = 1);
	void sendWebJson(Client &client, const QJsonObject &object);
	void updateQrPixmaps();
	void flattenLabels();
	void showTab(int index);
	void setTestMode(bool on);
	void refreshIcon(SlotCard &card);
	QString deviceTitle(const Client &client) const;
	QString outputName(const Client &client) const;
	void noteInput(Client &client, const vgp_data_exchange_gamepad_reading &reading);
	QStackedWidget *pages = nullptr;
	QWidget *pagesHolder = nullptr;
	bool eventFilter(QObject *watched, QEvent *event) override;
	QPushButton *tabControllers = nullptr;
	QPushButton *tabConnect = nullptr;
	QLabel *summaryLabel = nullptr;
	QLabel *slotNote = nullptr;
	QFrame *addPhoneCard = nullptr;
	class QGridLayout *cardsGrid = nullptr;
	QPushButton *viewStatus = nullptr;
	QPushButton *viewInput = nullptr;
	QPushButton *homeBtn = nullptr;
	QPushButton *restartBtn = nullptr;
	QPushButton *fixPadsBtn = nullptr;
	// One virtual Xbox pad per slot in XInput order, kept plugged in until the server stops or restarts
	std::vector<std::unique_ptr<ExecutorInterface>> xboxPads;
	void checkLeftoverPads();
	QLabel *tabIcon[2] = {nullptr, nullptr};
	QLabel *tabText[2] = {nullptr, nullptr};
	QLabel *tabCount = nullptr;
	std::function<QPixmap(const QColor &)> tabPix[2];
	bool restarting = false;
	void restartServer();
	void restartNow();
	void disconnectSlot(int slot);
	void stopServer();
	void dropClients();
	bool stopped = false;
	void setServerState(int state);
	QLabel *serverIcon = nullptr;
	bool listenServer();
	void styleTabs();
	bool debugEnabled() const;
	QString debugLogPath() const;
	static QString webPageVersion();
	void debugLog(const QString &line, bool force = false);
	QString deviceKey(const Client &client) const;
	bool testMode = false;
	bool userPickedTab = false;
	std::vector<QLabel *> qrLabels;
	QStringList qrAddresses;
	bool webQr = false;
	QLabel *webUrlLabel = nullptr;
	QLabel *portPill = nullptr;
	QLabel *countPill = nullptr;
	ExecutorType serverMode = ExecutorType::KeyboardMouseExecutor;
	int slotCapacity() const;
	static constexpr size_t MAX_PHONES = 8;
	std::vector<std::unique_ptr<Client>> clients;
	QTcpSocket *clientConnection;
	bool isGamepadConnected;
	QTime lastRequestTime;
	double averageRequestInterval = 0.0;
	uint_fast32_t requestCount = 0;
	QByteArray dataBuffer; // Buffer to store incoming data
	std::unique_ptr<ExecutorInterface> executor = nullptr;
};
