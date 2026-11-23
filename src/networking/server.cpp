#include "server.hpp"

#include "../../third-party-libs/QR-Code-generator/cpp/qrcodegen.hpp"
#include "../platform/elevation.hpp"
#include "../settings/settings_singleton.hpp"
#include "ui_server.h"

#include <QApplication>
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
		auto *modePill = new QLabel(tr("Input mode  |  %1").arg(modeName), this);
		modePill->setStyleSheet(pillStyle.arg("#5A5A5A", "#F5F5F5"));
		auto *pillRow = new QHBoxLayout();
		pillRow->setSpacing(8);
		pillRow->addWidget(modePill);
		serverMode = mode;
		portPill = new QLabel(this);
		portPill->setStyleSheet(pillStyle.arg("#3A3A3A", "#D4D4D4"));
		countPill = new QLabel(this);
		countPill->setStyleSheet(pillStyle.arg("#3A3A3A", "#D4D4D4"));
		pillRow->addWidget(portPill);
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
		titleColumn->addWidget(ui->statusLabel);
		titleColumn->addLayout(pillRow);
		ui->headerLayout->insertLayout(0, titleColumn);
	}

	// One card per controller slot, filled in as phones connect
	{
		auto *slotRow = new QGridLayout();
		slotRow->setSpacing(12);
		for (int i = 0; i < 6; ++i)
		{
			SlotCard card;
			card.frame = new QFrame(this);
			card.frame->setProperty("role", "card");
			auto *cardLayout = new QHBoxLayout(card.frame);
			cardLayout->setContentsMargins(12, 10, 12, 10);
			cardLayout->setSpacing(10);
			card.badge = new QLabel(QString::number(i + 1), card.frame);
			card.badge->setFixedSize(34, 34);
			card.badge->setAlignment(Qt::AlignCenter);
			auto *textColumn = new QVBoxLayout();
			textColumn->setSpacing(1);
			card.title = new QLabel(card.frame);
			card.detail = new QLabel(card.frame);
			card.detail->setProperty("role", "caption");
			textColumn->addWidget(card.title);
			textColumn->addWidget(card.detail);
			cardLayout->addWidget(card.badge);
			cardLayout->addLayout(textColumn, 1);
			slotRow->addWidget(card.frame, i / 3, i % 3);
			slotCards.push_back(card);
		}
		ui->verticalLayout->insertLayout(2, slotRow);
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
	ui->IPList->setMaximumHeight(120);
	ui->QRViewer->setMaximumHeight(290);
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
	securityNote->setAlignment(Qt::AlignCenter);
	ui->qrLayout->addWidget(securityNote);
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
	if (portPill != nullptr)
		portPill->setText(tr("Port %1").arg(port));
	if (countPill != nullptr)
	{
		countPill->setText(tr("%1 of %2 phones").arg(active).arg(capacity));
		countPill->setToolTip(serverMode == ExecutorType::VigemExecutor
								  ? tr("Games that use XInput can only see the first 4 controllers.")
								  : QString());
	}
	emit statusChanged(tr("Server running  |  Port %1  |  %2")
						   .arg(port)
						   .arg(active == 0 ? tr("no device connected")
											: tr("%1 of %2 connected").arg(active).arg(capacity)));
}

void Server::updateSlots()
{
	for (int i = 0; i < static_cast<int>(slotCards.size()); ++i)
	{
		const Client *owner = nullptr;
		for (const auto &client : clients)
		{
			if (client->slot == i + 1)
				owner = client.get();
		}
		SlotCard &card = slotCards[i];
		card.frame->setVisible(i < slotCapacity());
		const bool active = owner != nullptr;
		card.badge->setStyleSheet(
			active ? "QLabel { background: #F5F5F5; color: #000000; border-radius: 17px; "
					 "font-weight: 700; font-size: 11pt; }"
				   : "QLabel { background: transparent; color: #6B6B6B; border: 2px solid #3A3A3A; "
					 "border-radius: 17px; font-weight: 700; font-size: 11pt; }");
		if (active)
		{
			const QString name =
				!owner->name.isEmpty() ? owner->name : owner->socket->peerAddress().toString();
			card.title->setText(name);
			card.title->setStyleSheet("font-weight: 600; color: #F5F5F5;");
			QStringList parts;
			if (!owner->model.isEmpty() && owner->model != owner->name)
				parts << owner->model;
			parts << owner->socket->peerAddress().toString();
			card.detail->setText(parts.join("  |  "));
		}
		else
		{
			card.title->setText(tr("Controller %1").arg(i + 1));
			card.title->setStyleSheet("color: #8A8A8A;");
			card.detail->setText(i >= 4 && serverMode == ExecutorType::VigemExecutor
									 ? tr("Waiting (games will not see this one)")
									 : tr("Waiting for a phone"));
		}
	}
}

void Server::updateClientLabel()
{
	updateStatus();
	QStringList names;
	for (const auto &client : clients)
	{
		if (client->active)
			names << (client->name.isEmpty() ? client->socket->peerAddress().toString() : client->name);
	}
	if (names.isEmpty())
	{
		ui->clientLabel->setText(tr("○ No device connected"));
		return;
	}
	ui->clientLabel->setText(names.size() == 1
								 ? tr("● Connected to %1").arg(names.first())
								 : tr("● %1 devices connected: %2").arg(names.size()).arg(names.join(", ")));
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
		try
		{
			client.executor = std::make_unique<ViGEmExecutor>();
		}
		catch (const std::exception &e)
		{
			*failure = QString::fromUtf8(e.what());
			return false;
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
	client.slot = pickSlot(client.executor.get());
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
