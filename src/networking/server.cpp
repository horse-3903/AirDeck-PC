#include "server.hpp"

#include "../../third-party-libs/QR-Code-generator/cpp/qrcodegen.hpp"
#include "../platform/elevation.hpp"
#include "../ui/slotcolors.hpp"
#include "../settings/settings_singleton.hpp"
#include "ui_server.h"

#include <QApplication>
#include <QButtonGroup>
#include <QGridLayout>
#include <QClipboard>
#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDataStream>
#include <QHostAddress>
#include <QList>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QThread>
#include <QTimer>
#include <algorithm>


#ifdef _WIN32
#include <windows.h>
// Creating the Windows virtual gamepad can hit a native access violation on some systems
// (see https://github.com/microsoft/microsoft-ui-xaml/issues/8639). C++ exceptions cannot catch
// it, so creation is wrapped in structured exception handling to keep the server alive.
static GamepadExecutor *createGamepadCpp()
{
	try
	{
		return new GamepadExecutor();
	}
	catch (...)
	{
		return nullptr;
	}
}

static GamepadExecutor *createGamepadGuarded(bool *crashed)
{
	GamepadExecutor *result = nullptr;
	__try
	{
		result = createGamepadCpp();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		*crashed = true;
		result = nullptr;
	}
	return result;
}
#else
static GamepadExecutor *createGamepadGuarded(bool *crashed)
{
	try
	{
		return new GamepadExecutor();
	}
	catch (...)
	{
		return nullptr;
	}
}
#endif

/**
 * @brief Creates a QR code from a string
 *
 * This function uses [Nayuki's QR Code Generator
 * library](https://github.com/nayuki/QR-Code-generator/tree/master/cpp).\n Guided by:
 * https://stackoverflow.com/a/39951669/8659747
 * @param data The string to encode
 * @param border The padding around the QR code (in pixels)
 * @param scalingFactor The scaling factor for the final image
 * @return QImage
 */
QImage createQR(const QString &data, const int border = 1, const uint scalingFactor = 8)
{
	char *str = data.toUtf8().data();
	qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(str, qrcodegen::QrCode::Ecc::HIGH);
	const int s = qr.getSize(); // s is the length of a side of the QR code
	qDebug() << "QR code generated with size: " << s;
	QImage image(s + 2 * border, s + 2 * border, QImage::Format_Mono);
	image.setColor(1, QColor("black").rgb());
	image.setColor(0, QColor("white").rgb());
	image.fill(0); // Whitewash
	for (int y = 0; y < s; y++)
	{
		for (int x = 0; x < s; x++)
		{
			const int color = qr.getModule(x, y); // 0 for white, 1 for black
			image.setPixel(x + border, y + border, color);
		}
	}
	return image.scaled(image.size() * scalingFactor);
}

Server::Server(QWidget *parent) : QWidget(parent), ui(new Ui::Server)
{
	qInfo() << "Initializing TCP server";

	ui->setupUi(this);
	ui->IPList->viewport()->setAutoFillBackground(false);
	// delete this when stop button is clicked
	connect(ui->stopButton, &QPushButton::clicked, this, &Server::destroyServer);
	auto *homeButton = new QPushButton(tr("Back to Home"), this);
	homeButton->setCursor(Qt::PointingHandCursor);
	homeButton->setMinimumWidth(120);
	ui->stopButton->setMinimumWidth(120);
	ui->headerLayout->insertWidget(ui->headerLayout->indexOf(ui->stopButton), homeButton);
	connect(homeButton, &QPushButton::clicked, this, &Server::homeRequested);
	{
		const ExecutorType mode = SettingsSingleton::instance().executorType();
		const QString modeName =
			mode == ExecutorType::VigemExecutor		? tr("Xbox 360 controller (ViGEm)")
			: mode == ExecutorType::GamepadExecutor ? tr("Windows virtual gamepad")
													: tr("Keyboard and mouse");
		const QString pillStyle =
			"QLabel { background: #161616; border: 1px solid %1; border-radius: 4px; "
			"padding: 4px 12px; color: %2; font-weight: 600; }";
		serverMode = mode;
		auto *pillRow = new QHBoxLayout();
		pillRow->setSpacing(8);
		countPill = new QLabel(this);
		countPill->setProperty("role", "caption");
		pillRow->addWidget(countPill);
		if (isElevated())
		{
			auto *adminPill = new QLabel(tr("Administrator"), this);
			adminPill->setStyleSheet(pillStyle.arg("#8A6A1F", "#F2C14E"));
			pillRow->addWidget(adminPill);
		}
		pillRow->addStretch();
		auto *titleColumn = new QVBoxLayout();
		titleColumn->setSpacing(6);
		ui->headerLayout->removeWidget(ui->statusLabel);
		auto *statusRow = new QHBoxLayout();
		statusRow->setSpacing(8);
		const qreal dpr = devicePixelRatioF();
		QPixmap tick(QSize(22, 22) * dpr);
		tick.setDevicePixelRatio(dpr);
		tick.fill(Qt::transparent);
		{
			QPainter tp(&tick);
			tp.setRenderHint(QPainter::Antialiasing);
			tp.setPen(QPen(QColor("#34C759"), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			QPainterPath path;
			path.moveTo(4.5, 11.5);
			path.lineTo(9.2, 16.2);
			path.lineTo(17.5, 5.8);
			tp.drawPath(path);
		}
		auto *tickLabel = new QLabel(this);
		tickLabel->setPixmap(tick);
		tickLabel->setFixedSize(22, 22);
		statusRow->addWidget(tickLabel);
		statusRow->addWidget(ui->statusLabel, 1);
		titleColumn->addLayout(statusRow);
		titleColumn->addLayout(pillRow);
		ui->headerLayout->insertLayout(0, titleColumn);
	}

	// Two views on one page: Controllers (who is connected, test inputs) and Connect device (QR, address)
	{
		auto *connectPage = new QWidget(this);
		auto *connectLayout = new QHBoxLayout(connectPage);
		connectLayout->setContentsMargins(0, 0, 0, 0);
		connectLayout->setSpacing(16);
		connectLayout->addWidget(ui->connectionCard, 1);
		connectLayout->addWidget(ui->qrCard);
		ui->verticalLayout->removeItem(ui->contentLayout);
		delete ui->contentLayout;

		// Controllers list: one card per slot
		auto *listPage = new QWidget(this);
		auto *listLayout = new QVBoxLayout(listPage);
		listLayout->setContentsMargins(0, 0, 0, 0);
		listLayout->setSpacing(12);
		summaryLabel = new QLabel(listPage);
		summaryLabel->setStyleSheet("font-weight: 600; font-size: 12pt;");
		listLayout->addWidget(summaryLabel);
		auto *cardsLayout = new QVBoxLayout();
		cardsLayout->setSpacing(10);
		for (int i = 0; i < 6; ++i)
		{
			SlotCard card;
			card.frame = new QFrame(listPage);
			card.frame->setProperty("role", "card");
			auto *cardLayout = new QHBoxLayout(card.frame);
			cardLayout->setContentsMargins(16, 14, 16, 14);
			cardLayout->setSpacing(14);
			card.badge = new QLabel(QString::number(i + 1), card.frame);
			card.badge->setFixedSize(44, 44);
			card.badge->setAlignment(Qt::AlignCenter);
			auto *textColumn = new QVBoxLayout();
			textColumn->setSpacing(4);
			card.title = new QLabel(card.frame);
			card.detail = new QLabel(card.frame);
			card.detail->setProperty("role", "caption");
			auto *metaRow = new QHBoxLayout();
			metaRow->setSpacing(8);
			card.ip = new QLabel(card.frame);
			card.ip->setStyleSheet("QLabel { background: #1F1F24; border-radius: 10px; padding: 2px 10px; "
								   "color: #E5E5E5; font-weight: 600; }");
			card.input = new QLabel(card.frame);
			card.input->setProperty("role", "caption");
			metaRow->addWidget(card.ip);
			metaRow->addWidget(card.input);
			metaRow->addStretch();
			textColumn->addWidget(card.title);
			textColumn->addWidget(card.detail);
			textColumn->addLayout(metaRow);
			auto *rightColumn = new QVBoxLayout();
			rightColumn->setSpacing(8);
			card.state = new QLabel(card.frame);
			card.state->setAlignment(Qt::AlignCenter);
			card.test = new QPushButton(tr("Test inputs"), card.frame);
			card.test->setCursor(Qt::PointingHandCursor);
			card.test->setMinimumHeight(34);
			connect(card.test, &QPushButton::clicked, this, [this, i]() { openTester(i + 1); });
			rightColumn->addWidget(card.state, 0, Qt::AlignRight);
			rightColumn->addStretch();
			rightColumn->addWidget(card.test, 0, Qt::AlignRight);
			cardLayout->addWidget(card.badge, 0, Qt::AlignTop);
			cardLayout->addLayout(textColumn, 1);
			cardLayout->addLayout(rightColumn);
			cardsLayout->addWidget(card.frame);
			slotCards.push_back(card);
		}
		listLayout->addLayout(cardsLayout);

		// Pointer to the pairing screen once at least one phone is connected
		addPhoneCard = new QFrame(listPage);
		addPhoneCard->setStyleSheet("QFrame { border: 1px dashed #3A3A42; border-radius: 4px; background: transparent; }"
									"QLabel { border: none; }");
		auto *addLayout = new QVBoxLayout(addPhoneCard);
		addLayout->setContentsMargins(16, 14, 16, 14);
		addLayout->setSpacing(8);
		auto *addLabel = new QLabel(tr("Connect another phone"), addPhoneCard);
		addLabel->setAlignment(Qt::AlignCenter);
		auto *addButton = new QPushButton(tr("Show connection QR"), addPhoneCard);
		addButton->setCursor(Qt::PointingHandCursor);
		addButton->setMinimumHeight(34);
		connect(addButton, &QPushButton::clicked, this, [this]() { userPickedTab = true; showTab(1); });
		addLayout->addWidget(addLabel);
		addLayout->addWidget(addButton, 0, Qt::AlignCenter);
		listLayout->addWidget(addPhoneCard);

		slotNote = new QLabel(listPage);
		slotNote->setProperty("role", "caption");
		slotNote->setWordWrap(true);
		listLayout->addWidget(slotNote);
		listLayout->addStretch();

		// Tester page
		auto *testerPage = new QWidget(this);
		auto *testerLayout = new QVBoxLayout(testerPage);
		testerLayout->setContentsMargins(0, 0, 0, 0);
		testerLayout->setSpacing(10);
		auto *testerHeader = new QHBoxLayout();
		auto *backToList = new QPushButton(tr("Back to controllers"), testerPage);
		backToList->setCursor(Qt::PointingHandCursor);
		testerTitle = new QLabel(testerPage);
		testerTitle->setProperty("role", "heading");
		testerHeader->addWidget(backToList);
		testerHeader->addSpacing(8);
		testerHeader->addWidget(testerTitle, 1);
		testerLayout->addLayout(testerHeader);
		tester = new ControllerTester(testerPage);
		testerLayout->addWidget(tester, 1);
		auto *testerNote = new QLabel(tr("Shows what this PC is receiving from the phone."), testerPage);
		testerNote->setProperty("role", "caption");
		testerLayout->addWidget(testerNote);
		connect(backToList, &QPushButton::clicked, this, [this]() { closeTester(); });

		controllersStack = new QStackedWidget(this);
		controllersStack->addWidget(listPage);
		controllersStack->addWidget(testerPage);

		pages = new QStackedWidget(this);
		// Wrapped labels report a very tall minimum at narrow widths, which forced the whole page to scroll
		pages->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
		pages->setMinimumHeight(480);
		pages->addWidget(controllersStack);
		pages->addWidget(connectPage);

		// Tab bar
		auto *tabRow = new QHBoxLayout();
		tabRow->setSpacing(6);
		tabControllers = new QPushButton(tr("Controllers"), this);
		tabConnect = new QPushButton(tr("Connect device"), this);
		auto *tabGroup = new QButtonGroup(this);
		tabGroup->setExclusive(true);
		for (QPushButton *tab : {tabControllers, tabConnect})
		{
			tab->setCheckable(true);
			tab->setCursor(Qt::PointingHandCursor);
			tab->setMinimumHeight(36);
			tab->setMinimumWidth(150);
			tab->setStyleSheet("QPushButton { padding: 4px 18px; min-height: 22px; } "
							   "QPushButton:checked { background-color: #F5F5F5; color: #000000; border-color: #FFFFFF; }");
			tabGroup->addButton(tab);
			tabRow->addWidget(tab);
		}
		tabRow->addStretch();
		connect(tabControllers, &QPushButton::clicked, this, [this]() { userPickedTab = true; showTab(0); });
		connect(tabConnect, &QPushButton::clicked, this, [this]() { userPickedTab = true; showTab(1); });
		ui->verticalLayout->insertLayout(1, tabRow);
		ui->verticalLayout->insertWidget(2, pages, 1);
		ui->clientLabel->clear();
		ui->clientLabel->hide();
		showTab(1);
		updateSlots();
	}
	clientConnection = nullptr;
	tcpServer = new QTcpServer(this);
	isGamepadConnected = false;

	// Initialize the executor with try-catch for better error handling
	switch (ExecutorType executorType = SettingsSingleton::instance().executorType())
	{
	case ExecutorType::VigemExecutor:
	case ExecutorType::GamepadExecutor:
		// A virtual gamepad is created for each phone when it connects
		qInfo() << "GamepadExecutor will be created per connection";
		break;
	case ExecutorType::KeyboardMouseExecutor:
		executor = std::make_unique<KeyboardMouseExecutor>();
		qInfo() << "KeyboardMouseExecutor initialized successfully";
		break;
	default:
		qCritical() << "Unknown executor type";
	}

	initServer();

	// The home screen connects to statusChanged after construction, so announce the state again
	QTimer::singleShot(0, this, [this]() { updateStatus(); });
	qDebug() << "Server widget initialized";
}

Server::~Server()
{
	// IMPORTANT: clientConnection should not be accessed when the server is closed
	for (auto &client : clients)
	{
		// Closing a socket fires its disconnected handler, which erases from this list while it is walked
		client->socket->disconnect(this);
		if (client->executor)
			client->executor->release_all();
	}
	if (executor)
		executor->release_all();
	for (auto &client : clients)
		client->socket->close(); // Close connections gracefully
	clients.clear();
	clientConnection = nullptr;
	isGamepadConnected = false;
	tcpServer->close(); // And then close the server
	qInfo() << "Server stopped.";
	tcpServer->deleteLater();
	delete ui;
}

void Server::initServer()
{
	qInfo() << "Starting TCP server initialization";

	tcpServer->setListenBacklogSize(0);
	int port = SettingsSingleton::instance().port();
	if (port < 1024 || port > 65535)
	{
		port = 0; // 0 means random port
	}

	if (!tcpServer->listen(QHostAddress::AnyIPv4, port))
	{
		QMessageBox::critical(this, tr("VGamepad Server"),
							  tr("Unable to start the server: %1.").arg(tcpServer->errorString()));
		close(); // Close the error dialog
		tcpServer->close();
		return;
	}
	ui->IPList->setFixedHeight(120);
	ui->QRViewer->setFixedHeight(290);
	// Same container size for the App and Browser codes
	ui->QRViewer->setFixedWidth(344);
	ui->qrCard->setFixedWidth(380);
	{
		const int twoLines = ui->hintLabel->fontMetrics().lineSpacing() * 2 + 6;
		ui->hintLabel->setMinimumHeight(twoLines);
		ui->hintLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
		ui->qrCaption->setMinimumHeight(twoLines);
		ui->qrCaption->setWordWrap(true);
		ui->qrCaption->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
		ui->connectionLayout->insertStretch(ui->connectionLayout->indexOf(ui->copyButton), 1);
	}
	ui->statusLabel->setText(tr("Server running"));
	ui->addressTitle->setText(tr("IP address"));
	ui->portLabel->setText(tr("Port: **%1**").arg(tcpServer->serverPort()));
	ui->portLabel->setTextFormat(Qt::MarkdownText);

	// Most phones join the same Wi-Fi as this PC, which is almost always a private range.
	// Show those first and mark the most likely one so virtual adapters are not mistaken for it.
	auto rank = [](const QHostAddress &address) {
		const QString text = address.toString();
		if (text.startsWith("192.168."))
			return 0;
		if (text.startsWith("10."))
			return 1;
		if (text.startsWith("172."))
			return 2;
		return 3;
	};
	QList<QHostAddress> candidates;
	for (const QHostAddress &entry : QNetworkInterface::allAddresses())
	{
		if (entry.isGlobal() && entry.protocol() == QAbstractSocket::IPv4Protocol)
			candidates.append(entry);
	}
	std::stable_sort(candidates.begin(), candidates.end(),
					 [&rank](const QHostAddress &a, const QHostAddress &b) {
						 return rank(a) < rank(b);
					 });
	for (int i = 0; i < candidates.size(); ++i)
	{
		const QHostAddress &entry = candidates.at(i);
		const QString address = entry.toString();
		auto *item = new QListWidgetItem(i == 0 ? tr("%1   (recommended)").arg(address)
												: tr("%1   (other adapter)").arg(address));
		item->setData(Qt::UserRole, address);
		ui->IPList->addItem(item);
		QLabel *QRWidget = new QLabel();
		QRWidget->setAlignment(Qt::AlignCenter);
		ui->QRViewer->addWidget(QRWidget);
		qrLabels.push_back(QRWidget);
		qrAddresses << address;
	}
	if (ui->IPList->count() > 0)
	{
		ui->IPList->setCurrentRow(0);
	}
	else
	{
		ui->hintLabel->setText(
			tr("No network address found. Connect this PC to a network and restart the server."));
		ui->copyButton->setEnabled(false);
	}
	// Two QR codes per address: one for the phone app, one that opens the browser controller
	{
		auto *toggleRow = new QHBoxLayout();
		toggleRow->setSpacing(6);
		auto *appButton = new QPushButton(tr("App"), this);
		auto *webButton = new QPushButton(tr("Browser"), this);
		for (QPushButton *button : {appButton, webButton})
		{
			button->setCheckable(true);
			button->setCursor(Qt::PointingHandCursor);
			button->setFixedHeight(30);
			button->setStyleSheet("QPushButton { padding: 2px 14px; min-height: 18px; } "
				"QPushButton:checked { background-color: #F5F5F5; color: #000000; border-color: #FFFFFF; }");
		}
		appButton->setChecked(true);
		toggleRow->addStretch();
		toggleRow->addWidget(appButton);
		toggleRow->addWidget(webButton);
		toggleRow->addStretch();
		ui->qrLayout->insertLayout(0, toggleRow);
		const QString appHint = ui->hintLabel->text();
		const auto choose = [this, appButton, webButton, appHint](bool web) {
			webQr = web;
			appButton->setChecked(!web);
			webButton->setChecked(web);
			updateQrPixmaps();
			if (webUrlLabel != nullptr)
			{
				if (QListWidgetItem *current = ui->IPList->currentItem())
					webUrlLabel->setText(tr("http://%1:%2/")
											 .arg(current->data(Qt::UserRole).toString())
											 .arg(tcpServer->serverPort()));
				webUrlLabel->setVisible(web);
			}
			if (ui->IPList->count() > 0)
				ui->hintLabel->setText(web ? tr("Open the address below in the phone's browser, or scan the QR code with its camera.")
										   : appHint);
		};
		connect(appButton, &QPushButton::clicked, this, [choose]() { choose(false); });
		connect(webButton, &QPushButton::clicked, this, [choose]() { choose(true); });

		webUrlLabel = new QLabel(this);
		webUrlLabel->setProperty("role", "caption");
		webUrlLabel->setWordWrap(true);
		webUrlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
		{
			// The web address row keeps its space in App mode so nothing below it shifts
			QSizePolicy keep = webUrlLabel->sizePolicy();
			keep.setRetainSizeWhenHidden(true);
			webUrlLabel->setSizePolicy(keep);
			webUrlLabel->setMinimumHeight(webUrlLabel->fontMetrics().lineSpacing() + 4);
		}
		webUrlLabel->setVisible(false);
		ui->connectionLayout->insertWidget(ui->connectionLayout->indexOf(ui->portLabel) + 1, webUrlLabel);
		updateQrPixmaps();
	}
	connect(ui->copyButton, &QPushButton::clicked, this, [this]() {
		QListWidgetItem *current = ui->IPList->currentItem();
		if (current == nullptr)
			return;
		const QString address = current->data(Qt::UserRole).toString();
		QApplication::clipboard()->setText(webQr ? tr("http://%1:%2/").arg(address).arg(tcpServer->serverPort())
												 : tr("%1:%2").arg(address).arg(tcpServer->serverPort()));
		ui->copyButton->setText(tr("Copied"));
		QTimer::singleShot(1500, ui->copyButton, [this]() { ui->copyButton->setText(tr("Copy address")); });
	});
	connect(ui->IPList, &QListWidget::currentItemChanged, this,
			[this](QListWidgetItem *current, QListWidgetItem *) {
				if (current != nullptr)
				{
						ui->QRViewer->setCurrentIndex(ui->IPList->row(current));
						if (webUrlLabel != nullptr)
							webUrlLabel->setText(tr("http://%1:%2/")
													 .arg(current->data(Qt::UserRole).toString())
													 .arg(tcpServer->serverPort()));
				}
			});
	connect(tcpServer, &QTcpServer::newConnection, this, &Server::handleConnection);
	auto *securityNote = new QLabel(
		tr("Anyone on your network who has this address can connect. The QR code is not a password."), this);
	securityNote->setProperty("role", "caption");
	securityNote->setWordWrap(true);
	securityNote->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
	securityNote->setMinimumHeight(securityNote->fontMetrics().lineSpacing() * 2 + 6);
	ui->qrLayout->addWidget(securityNote);
	ui->qrLayout->addStretch();
	updateStatus();
	qInfo() << "Server started successfully on port:" << tcpServer->serverPort();
}

int Server::pickSlot(const ExecutorInterface *executor) const
{
	const auto used = [this](int slot) {
		return std::any_of(clients.begin(), clients.end(),
						   [slot](const std::unique_ptr<Client> &c) { return c->slot == slot; });
	};
	const int preferred = executor != nullptr ? executor->slot() : -1;
	if (preferred >= 1 && preferred <= 6 && !used(preferred))
		return preferred;
	for (int slot = 1; slot <= 6; ++slot)
	{
		if (!used(slot))
			return slot;
	}
	return 0;
}

int Server::slotCapacity() const
{
	return serverMode == ExecutorType::KeyboardMouseExecutor ? 1 : static_cast<int>(MAX_PHONES);
}

void Server::updateStatus()
{
	const int active = static_cast<int>(std::count_if(
		clients.begin(), clients.end(), [](const std::unique_ptr<Client> &c) { return c->active; }));
	const int capacity = slotCapacity();
	const int port = tcpServer != nullptr ? tcpServer->serverPort() : 0;
	if (countPill != nullptr)
	{
		const QString modeName = serverMode == ExecutorType::VigemExecutor ? tr("Xbox 360 controller (ViGEm)")
								 : serverMode == ExecutorType::GamepadExecutor ? tr("Windows virtual gamepad")
																			   : tr("Keyboard and mouse");
		countPill->setText(tr("%1  \u00B7  Port %2").arg(modeName).arg(port));
	}
	if (summaryLabel != nullptr)
		summaryLabel->setText(active == 0 ? tr("Connected controllers")
										  : tr("Connected controllers  (%1 connected, %2 available)")
												.arg(active)
												.arg(capacity - active));
	if (tabControllers != nullptr)
		tabControllers->setText(active > 0 ? tr("Controllers (%1)").arg(active) : tr("Controllers"));
	if (!userPickedTab && pages != nullptr)
		showTab(active > 0 ? 0 : 1);
	emit statusChanged(tr("Server running  |  Port %1  |  %2")
						   .arg(port)
						   .arg(active == 0 ? tr("no device connected")
											: tr("%1 of %2 connected").arg(active).arg(capacity)));
}

void Server::showTab(int index)
{
	pages->setCurrentIndex(index);
	tabControllers->setChecked(index == 0);
	tabConnect->setChecked(index == 1);
}

void Server::openTester(int slot)
{
	testerSlot = slot;
	tester->setSlot(slot);
	for (const auto &client : clients)
	{
		if (client->slot == slot)
		{
			testerTitle->setText(tr("Controller %1: %2")
									 .arg(slot)
									 .arg(client->name.isEmpty() ? client->socket->peerAddress().toString() : client->name));
			tester->setState(client->held, client->lx, client->ly, client->rx, client->ry, client->lt, client->rt);
		}
	}
	controllersStack->setCurrentIndex(1);
}

void Server::closeTester()
{
	testerSlot = 0;
	controllersStack->setCurrentIndex(0);
}

void Server::noteInput(Client &client, const vgp_data_exchange_gamepad_reading &reading)
{
	client.held = (client.held | reading.buttons_down) & ~reading.buttons_up;
	client.lx = reading.left_thumbstick_x;
	client.ly = reading.left_thumbstick_y;
	client.rx = reading.right_thumbstick_x;
	client.ry = reading.right_thumbstick_y;
	client.lt = reading.left_trigger;
	client.rt = reading.right_trigger;
	if (testerSlot != 0 && client.slot == testerSlot)
		tester->setState(client.held, client.lx, client.ly, client.rx, client.ry, client.lt, client.rt);
}

void Server::updateSlots()
{
	std::vector<int> freeSlots;
	for (int i = 0; i < static_cast<int>(slotCards.size()); ++i)
	{
		const Client *owner = nullptr;
		for (const auto &client : clients)
		{
			if (client->active && client->slot == i + 1)
				owner = client.get();
		}
		SlotCard &card = slotCards[i];
		const QColor color = slotColor(i + 1);
		const bool active = owner != nullptr;
		// Only connected phones get a card, the free slots are summarised underneath
		card.frame->setVisible(active && i < slotCapacity());
		if (!active && i < slotCapacity())
			freeSlots.push_back(i + 1);
		if (!active)
			continue;
		card.badge->setStyleSheet(QString("QLabel { background: %1; color: #000000; border-radius: 22px; "
										  "font-weight: 800; font-size: 14pt; }")
									  .arg(color.name()));
		const bool web = owner->protocol == Client::Protocol::WebSocket;
		card.title->setText(!owner->name.isEmpty() ? owner->name : owner->socket->peerAddress().toString());
		card.title->setStyleSheet("font-weight: 700; font-size: 13pt; color: #F5F5F5;");
		QStringList parts;
		parts << tr("Controller %1").arg(i + 1) << (web ? tr("Browser") : tr("Android app"));
		if (!owner->model.isEmpty() && owner->model != owner->name)
			parts << owner->model;
		card.detail->setText(parts.join("  \u00B7  "));
		card.detail->setToolTip(parts.join("\n"));
		card.ip->setText(owner->socket->peerAddress().toString());
		card.input->setText(owner->ds4								  ? tr("DualShock 4 input")
							: serverMode == ExecutorType::VigemExecutor ? tr("Xbox 360 input")
							: serverMode == ExecutorType::GamepadExecutor ? tr("Windows virtual gamepad input")
																		 : tr("Keyboard and mouse input"));
		card.state->setText(tr("Connected"));
		card.state->setStyleSheet(QString("QLabel { color: %1; background: rgba(%2,%3,%4,40); border-radius: 10px; "
										  "padding: 3px 12px; font-weight: 600; }")
									  .arg(color.name())
									  .arg(color.red())
									  .arg(color.green())
									  .arg(color.blue()));
	}
	if (addPhoneCard != nullptr)
		addPhoneCard->setVisible(!freeSlots.empty());
	if (slotNote != nullptr)
	{
		QStringList numbers;
		for (int slot : freeSlots)
			numbers << QString::number(slot);
		QString note = freeSlots.empty() ? tr("All controller slots are in use.")
										 : tr("Slots %1 available").arg(numbers.join(", "));
		if (serverMode == ExecutorType::VigemExecutor)
			note += tr("  \u00B7  XInput games generally support up to 4 controllers. Controllers 5 and 6 appear "
					   "as PlayStation controllers.");
		slotNote->setText(note);
	}
	// The tester follows one controller; leave it when that controller goes away
	if (testerSlot != 0)
	{
		const bool still = std::any_of(clients.begin(), clients.end(), [this](const std::unique_ptr<Client> &c) {
			return c->active && c->slot == testerSlot;
		});
		if (!still)
			closeTester();
	}
}

void Server::updateClientLabel()
{
	updateStatus();
	// Only refusals use this line; connected phones are shown on their cards
	ui->clientLabel->clear();
}

ExecutorInterface *Server::executorFor(Client &client)
{
	return client.executor ? client.executor.get() : executor.get();
}

bool Server::activate(Client &client, QString *failure)
{
	// Keyboard/mouse input is shared, so only one phone may drive it.
	// Virtual gamepads can be created per phone.
	const ExecutorType mode = SettingsSingleton::instance().executorType();
	const bool multiPhone =
		mode == ExecutorType::GamepadExecutor || mode == ExecutorType::VigemExecutor;
	const auto slotUsed = [this](int slot) {
		return std::any_of(clients.begin(), clients.end(),
						   [slot](const std::unique_ptr<Client> &c) { return c->slot == slot; });
	};
	int chosenSlot = 0;
	if (!multiPhone)
	{
		for (const auto &other : clients)
		{
			if (other.get() != &client && other->active)
			{
				*failure = tr("Another phone is already connected. Keyboard/Mouse mode accepts one phone.");
				return false;
			}
		}
	}
	else if (mode == ExecutorType::VigemExecutor)
	{
#ifdef _WIN32
		const int xboxInUse = static_cast<int>(std::count_if(
			clients.begin(), clients.end(),
			[](const std::unique_ptr<Client> &c) { return c->active && !c->ds4; }));
		const bool useDs4 = xboxInUse >= 4;
		if (useDs4)
		{
			for (int candidate : {5, 6})
			{
				if (!slotUsed(candidate))
				{
					chosenSlot = candidate;
					break;
				}
			}
			if (chosenSlot == 0)
			{
				*failure = tr("All 6 controller slots are in use.");
				return false;
			}
		}
		try
		{
			client.executor = std::make_unique<ViGEmExecutor>(useDs4, chosenSlot);
		}
		catch (const std::exception &e)
		{
			*failure = QString::fromUtf8(e.what());
			return false;
		}
		client.ds4 = useDs4;
		if (!useDs4)
		{
			// Xbox controllers take the slot Windows gave them, so games and this app agree on the number
			const int preferred = client.executor->slot();
			if (preferred >= 1 && preferred <= 4 && !slotUsed(preferred))
				chosenSlot = preferred;
			else
			{
				for (int candidate = 1; candidate <= 4 && chosenSlot == 0; ++candidate)
				{
					if (!slotUsed(candidate))
						chosenSlot = candidate;
				}
			}
			if (chosenSlot == 0)
			{
				client.executor.reset();
				*failure = tr("All controller slots are in use.");
				return false;
			}
		}
#endif
	}
	else
	{
		bool crashed = false;
		client.executor.reset(createGamepadGuarded(&crashed));
		if (!client.executor)
		{
			*failure = tr("Could not create a virtual gamepad. Switch to Keyboard/Mouse "
						  "Simulation or the ViGEm controller in Preferences.");
			return false;
		}
	}
	client.slot = chosenSlot != 0 ? chosenSlot : pickSlot(client.executor.get());
	if (client.slot == 0)
	{
		*failure = tr("All 6 controller slots are in use.");
		return false;
	}
	client.active = true;
	clientConnection = client.socket;
	isGamepadConnected = true;
	qInfo() << "Device connected:" << client.socket->peerAddress().toString()
			<< "slot" << client.slot << "total devices:" << clients.size();
	updateClientLabel();
	updateSlots();
	return true;
}

void Server::refuse(Client &client, const QString &why)
{
	qWarning() << "Refusing a device:" << why;
	ui->clientLabel->setText(why);
	QTcpSocket *socket = client.socket;
	if (client.protocol == Client::Protocol::WebSocket)
	{
		sendWebJson(client, QJsonObject{{"t", "refused"}, {"why", why}});
		sendWebFrame(socket, QByteArray(), 8);
	}
	socket->disconnectFromHost();
}

void Server::handleConnection()
{
	QTcpSocket *socket = tcpServer->nextPendingConnection();
	if (socket == nullptr)
		return;
	// disable Nagle's algorithm to avoid delay and bunching of small packages
	socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

	auto client = std::make_unique<Client>();
	client->socket = socket;
	clients.push_back(std::move(client));

	connect(socket, &QAbstractSocket::disconnected, this, [this, socket]() {
		const auto it = std::find_if(clients.begin(), clients.end(),
									 [socket](const std::unique_ptr<Client> &c) { return c->socket == socket; });
		bool wasActive = false;
		if (it != clients.end())
		{
			wasActive = (*it)->active;
			if ((*it)->executor)
				(*it)->executor->release_all();
			else if (wasActive && executor)
				executor->release_all();
			clients.erase(it);
		}
		socket->deleteLater();
		isGamepadConnected = std::any_of(clients.begin(), clients.end(),
										 [](const std::unique_ptr<Client> &c) { return c->active; });
		if (!isGamepadConnected)
			clientConnection = nullptr;
		if (wasActive)
		{
			qInfo() << "Device disconnected.";
			qDebug() << "Average Request Interval" << averageRequestInterval
					 << "ms, Request Count:" << requestCount;
		}
		updateClientLabel();
		updateSlots();
	});
	connect(socket, &QAbstractSocket::readyRead, this, &Server::serveClient);
}

void Server::serveClient()
{
	auto *socket = qobject_cast<QTcpSocket *>(sender());
	const auto clientIt = std::find_if(clients.begin(), clients.end(),
									   [socket](const std::unique_ptr<Client> &c) { return c->socket == socket; });
	if (clientIt == clients.end())
		return;
	Client &client = **clientIt;
	client.buffer.append(socket->readAll());

	if (client.protocol == Client::Protocol::Unknown)
	{
		// Browsers speak HTTP and start with "GET ", the phone app starts with its own text or binary
		const QByteArray probe("GET ");
		if (client.buffer.size() < probe.size() && probe.startsWith(client.buffer))
			return;
		if (client.buffer.startsWith(probe))
		{
			client.protocol = Client::Protocol::Http;
		}
		else
		{
			client.protocol = Client::Protocol::Native;
			QString failure;
			if (!activate(client, &failure))
			{
				refuse(client, failure);
				return;
			}
			socket->write("VGPSLOT:" + QByteArray::number(client.slot) + "\n");
		}
	}

	switch (client.protocol)
	{
	case Client::Protocol::Native:
		serveNative(client);
		break;
	case Client::Protocol::Http:
		serveHttp(client);
		break;
	case Client::Protocol::WebSocket:
		serveWebSocket(client);
		break;
	default:
		break;
	}
}

void Server::serveNative(Client &client)
{
	ExecutorInterface *activeExecutor = executorFor(client);
	if (activeExecutor == nullptr)
		return;

	QTime currentTime = QTime::currentTime();

	// Process as many complete packets as we have in the buffer
	while (!client.buffer.isEmpty())
	{
		// A phone introduces itself first: VGPHELLO:<name>|<model>
		if (client.buffer.startsWith("VGPHELLO:"))
		{
			const int end = client.buffer.indexOf('\n');
			if (end < 0)
				break;
			const QStringList parts =
				QString::fromUtf8(client.buffer.mid(9, end - 9)).split('|');
			client.name = parts.value(0).trimmed();
			client.model = parts.value(1).trimmed();
			client.buffer.remove(0, end + 1);
			qInfo() << "Device introduced itself:" << client.name << client.model;
			updateSlots();
			continue;
		}
		ParseResult result = parse_gamepad_state(client.buffer.constData(), client.buffer.size());

		if (!result.success)
		{
			switch (result.failure_reason)
			{
				using enum ParseResult::FailureReason;
			[[likely]] case IncompleteData:
				// Wait for more data
				break;
			case SchemaMismatch:
				qWarning() << "Schema mismatch detected in client data";
				client.buffer.remove(0, result.bytes_consumed); // Remove the processed data
				break;
			case DataTooLarge:
				qWarning() << "Client sent data that is too large to process";
				client.buffer.remove(0, result.bytes_consumed); // Remove the processed data
				break;
			[[unlikely]] default:
				qWarning() << "Unknown error occurred while parsing client data";
				client.buffer.clear(); // Clear buffer to avoid cascading errors
				break;
			}
			break; // Exit the processing loop
		}

		requestCount++;
		if (lastRequestTime.isValid())
		{
			int elapsed = lastRequestTime.msecsTo(currentTime);
			averageRequestInterval += (elapsed - averageRequestInterval) / requestCount;
		}
		lastRequestTime = currentTime;

		noteInput(client, result.reading);
		activeExecutor->inject_gamepad_state(result.reading);
		client.buffer.remove(0, result.bytes_consumed);
	}
}

void Server::serveHttp(Client &client)
{
	const int end = client.buffer.indexOf("\r\n\r\n");
	if (end < 0)
	{
		if (client.buffer.size() > 16384)
			client.socket->disconnectFromHost();
		return;
	}
	const QByteArray head = client.buffer.left(end);
	client.buffer.remove(0, end + 4);

	const QList<QByteArray> lines = head.split('\n');
	const QByteArray path = lines.value(0).trimmed().split(' ').value(1);
	QHash<QByteArray, QByteArray> headers;
	for (int i = 1; i < lines.size(); ++i)
	{
		const int colon = lines[i].indexOf(':');
		if (colon > 0)
			headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
	}

	if (headers.value("upgrade").toLower() == "websocket" && headers.contains("sec-websocket-key"))
	{
		static const QByteArray magic("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
		const QByteArray accept =
			QCryptographicHash::hash(headers.value("sec-websocket-key") + magic, QCryptographicHash::Sha1)
				.toBase64();
		client.socket->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
							 "Connection: Upgrade\r\nSec-WebSocket-Accept: " +
							 accept + "\r\n\r\n");
		client.protocol = Client::Protocol::WebSocket;
		QString failure;
		if (!activate(client, &failure))
		{
			refuse(client, failure);
			return;
		}
		sendWebJson(client, QJsonObject{{"t", "slot"}, {"n", client.slot}});
		serveWebSocket(client);
		return;
	}

	QByteArray response;
	if (path == "/" || path.startsWith("/?") || path == "/index.html")
	{
		QFile page(":/web/controller.html");
		const QByteArray body = page.open(QIODevice::ReadOnly) ? page.readAll() : QByteArray();
		response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
				   QByteArray::number(body.size()) +
				   "\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n" + body;
	}
	else if (path == "/favicon.ico")
	{
		response = "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n";
	}
	else
	{
		response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	}
	client.socket->write(response);
	client.socket->disconnectFromHost();
}

void Server::sendWebFrame(QTcpSocket *socket, const QByteArray &payload, int opcode)
{
	QByteArray frame;
	frame.append(static_cast<char>(0x80 | opcode));
	if (payload.size() < 126)
	{
		frame.append(static_cast<char>(payload.size()));
	}
	else
	{
		frame.append(static_cast<char>(126));
		frame.append(static_cast<char>((payload.size() >> 8) & 0xFF));
		frame.append(static_cast<char>(payload.size() & 0xFF));
	}
	frame.append(payload);
	socket->write(frame);
}

void Server::sendWebJson(Client &client, const QJsonObject &object)
{
	sendWebFrame(client.socket, QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void Server::serveWebSocket(Client &client)
{
	while (client.buffer.size() >= 2)
	{
		const auto byteAt = [&client](int i) { return static_cast<quint8>(client.buffer[i]); };
		const int opcode = byteAt(0) & 0x0F;
		const bool masked = (byteAt(1) & 0x80) != 0;
		quint64 length = byteAt(1) & 0x7F;
		int position = 2;
		if (length == 126)
		{
			if (client.buffer.size() < 4)
				return;
			length = (static_cast<quint64>(byteAt(2)) << 8) | byteAt(3);
			position = 4;
		}
		else if (length == 127)
		{
			if (client.buffer.size() < 10)
				return;
			length = 0;
			for (int i = 0; i < 8; ++i)
				length = (length << 8) | byteAt(2 + i);
			position = 10;
		}
		if (!masked || length > 65536)
		{
			client.socket->disconnectFromHost();
			return;
		}
		if (static_cast<quint64>(client.buffer.size()) < static_cast<quint64>(position) + 4 + length)
			return;
		QByteArray payload = client.buffer.mid(position + 4, static_cast<int>(length));
		for (int i = 0; i < payload.size(); ++i)
			payload[i] = payload[i] ^ client.buffer[position + (i % 4)];
		client.buffer.remove(0, position + 4 + static_cast<int>(length));

		if (opcode == 0x1)
		{
			handleWebMessage(client, payload);
		}
		else if (opcode == 0x8)
		{
			sendWebFrame(client.socket, QByteArray(), 8);
			client.socket->disconnectFromHost();
			return;
		}
		else if (opcode == 0x9)
		{
			sendWebFrame(client.socket, payload, 0xA);
		}
	}
}

void Server::handleWebMessage(Client &client, const QByteArray &payload)
{
	const QJsonObject message = QJsonDocument::fromJson(payload).object();
	const QString type = message.value("t").toString();
	if (type == "hello")
	{
		client.name = message.value("name").toString().left(40).trimmed();
		client.model = message.value("model").toString().left(60).trimmed();
		qInfo().noquote() << "[web] hello from" << client.socket->peerAddress().toString() << "slot" << client.slot
						  << "name=" << client.name << "model=" << client.model;
		updateSlots();
		return;
	}
	if (type != "state")
		return;

	ExecutorInterface *activeExecutor = executorFor(client);
	if (activeExecutor == nullptr)
		return;
	const auto axis = [&message](const char *key) {
		return static_cast<float>(std::clamp(message.value(key).toDouble(), -1.0, 1.0));
	};
	const auto trigger = [&message](const char *key) {
		return static_cast<float>(std::clamp(message.value(key).toDouble(), 0.0, 1.0));
	};
	vgp_data_exchange_gamepad_reading reading{};
	reading.buttons_down = static_cast<uint32_t>(message.value("d").toDouble()) & 0x3FFF;
	reading.buttons_up = static_cast<uint32_t>(message.value("u").toDouble()) & 0x3FFF;
	reading.left_thumbstick_x = axis("lx");
	reading.left_thumbstick_y = axis("ly");
	reading.right_thumbstick_x = axis("rx");
	reading.right_thumbstick_y = axis("ry");
	reading.left_trigger = trigger("lt");
	reading.right_trigger = trigger("rt");
	requestCount++;
	{
		static const char *const names[] = {"Menu", "View", "A", "B", "X", "Y", "Up", "Down", "Left", "Right",
											"L1", "R1", "L3", "R3"};
		const auto decode = [](uint32_t bits) {
			QStringList list;
			for (int i = 0; i < 14; ++i)
				if (bits & (1u << i))
					list << names[i];
			return list.isEmpty() ? QStringLiteral("-") : list.join('+');
		};
		const bool axesChanged = reading.left_thumbstick_x != client.lx || reading.left_thumbstick_y != client.ly ||
								 reading.right_thumbstick_x != client.rx || reading.right_thumbstick_y != client.ry ||
								 reading.left_trigger != client.lt || reading.right_trigger != client.rt;
		// Only log when something happened, heartbeats would flood the file
		if (reading.buttons_down || reading.buttons_up || axesChanged)
			qInfo().noquote() << QString("[web] slot %1 down=%2 up=%3 held=0x%4 L=(%5,%6) R=(%7,%8) LT=%9 RT=%10 raw=%11")
									 .arg(client.slot)
									 .arg(decode(reading.buttons_down))
									 .arg(decode(reading.buttons_up))
									 .arg(client.held, 0, 16)
									 .arg(reading.left_thumbstick_x, 0, 'f', 2)
									 .arg(reading.left_thumbstick_y, 0, 'f', 2)
									 .arg(reading.right_thumbstick_x, 0, 'f', 2)
									 .arg(reading.right_thumbstick_y, 0, 'f', 2)
									 .arg(reading.left_trigger, 0, 'f', 2)
									 .arg(reading.right_trigger, 0, 'f', 2)
									 .arg(QString::fromUtf8(payload));
	}
	noteInput(client, reading);
	activeExecutor->inject_gamepad_state(reading);
}

void Server::updateQrPixmaps()
{
	for (size_t i = 0; i < qrLabels.size(); ++i)
	{
		const QString text = webQr ? tr("http://%1:%2/").arg(qrAddresses[i]).arg(tcpServer->serverPort())
								   : tr("%1:%2").arg(qrAddresses[i]).arg(tcpServer->serverPort());
		qrLabels[i]->setPixmap(QPixmap::fromImage(createQR(text)));
	}
	ui->qrCaption->setText(webQr ? tr("Scan with the phone camera to open the web controller")
								 : tr("Scan with the Virtual Gamepad app"));
}

void Server::destroyServer()
{
	emit navigateBack();
	this->deleteLater();
}
