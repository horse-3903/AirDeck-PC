#include "server.hpp"

#include "../../third-party-libs/QR-Code-generator/cpp/qrcodegen.hpp"
#include "../platform/elevation.hpp"
#include "../settings/settings_singleton.hpp"
#include "ui_server.h"

#include <QApplication>
#include <QGridLayout>
#include <QClipboard>
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
	homeButton->setMinimumWidth(150);
	ui->headerLayout->insertWidget(ui->headerLayout->indexOf(ui->stopButton), homeButton);
	connect(homeButton, &QPushButton::clicked, this, &Server::homeRequested);
	{
		const ExecutorType mode = SettingsSingleton::instance().executorType();
		const QString modeName =
			mode == ExecutorType::VigemExecutor		? tr("Virtual Xbox 360 Controller (ViGEm)")
			: mode == ExecutorType::GamepadExecutor ? tr("Virtual Gamepad (InputInjector)")
													: tr("Keyboard/Mouse Simulation");
		const QString pillStyle =
			"QLabel { background: #161616; border: 1px solid %1; border-radius: 4px; "
			"padding: 4px 12px; color: %2; font-weight: 600; }";
		auto *modePill = new QLabel(tr("Input mode  |  %1").arg(modeName), this);
		modePill->setStyleSheet(pillStyle.arg("#5A5A5A", "#F5F5F5"));
		auto *pillRow = new QHBoxLayout();
		pillRow->setSpacing(8);
		pillRow->addWidget(modePill);
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
		auto *item = new QListWidgetItem(i == 0 ? tr("%1   (recommended)").arg(address) : address);
		item->setData(Qt::UserRole, address);
		ui->IPList->addItem(item);
		QLabel *QRWidget = new QLabel();
		QRWidget->setAlignment(Qt::AlignCenter);
		QRWidget->setPixmap(QPixmap::fromImage(
			createQR(tr("%1:%2").arg(address).arg(tcpServer->serverPort()))));
		ui->QRViewer->addWidget(QRWidget);
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
	connect(ui->copyButton, &QPushButton::clicked, this, [this]() {
		QListWidgetItem *current = ui->IPList->currentItem();
		if (current == nullptr)
			return;
		QApplication::clipboard()->setText(
			tr("%1:%2").arg(current->data(Qt::UserRole).toString()).arg(tcpServer->serverPort()));
		ui->copyButton->setText(tr("Copied"));
		QTimer::singleShot(1500, ui->copyButton, [this]() { ui->copyButton->setText(tr("Copy address")); });
	});
	connect(ui->IPList, &QListWidget::currentItemChanged, this,
			[this](QListWidgetItem *current, QListWidgetItem *) {
				if (current != nullptr)
				{
					ui->QRViewer->setCurrentIndex(ui->IPList->row(current));
				}
			});
	connect(tcpServer, &QTcpServer::newConnection, this, &Server::handleConnection);
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
			card.detail->setText(tr("Waiting for a phone"));
		}
	}
}

void Server::updateClientLabel()
{
	if (clients.empty())
	{
		ui->clientLabel->setText(tr("○ No device connected"));
		return;
	}
	QStringList names;
	for (const auto &client : clients)
		names << client->socket->peerAddress().toString();
	ui->clientLabel->setText(clients.size() == 1
								 ? tr("● Connected to %1").arg(names.first())
								 : tr("● %1 devices connected: %2").arg(clients.size()).arg(names.join(", ")));
}

void Server::handleConnection()
{
	// Keyboard/mouse input is shared, so only one phone may drive it.
	// Virtual gamepads can be created per phone.
	const ExecutorType mode = SettingsSingleton::instance().executorType();
	const bool multiPhone =
		mode == ExecutorType::GamepadExecutor || mode == ExecutorType::VigemExecutor;
	QTcpSocket *socket = tcpServer->nextPendingConnection();
	if (socket == nullptr)
		return;
	// disable Nagle's algorithm to avoid delay and bunching of small packages
	socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

	auto client = std::make_unique<Client>();
	client->socket = socket;
	if (multiPhone)
	{
		QString failure;
		if (mode == ExecutorType::VigemExecutor)
		{
#ifdef _WIN32
			try
			{
				client->executor = std::make_unique<ViGEmExecutor>();
			}
			catch (const std::exception &e)
			{
				failure = QString::fromUtf8(e.what());
			}
#endif
		}
		else
		{
			bool crashed = false;
			client->executor.reset(createGamepadGuarded(&crashed));
			if (!client->executor)
				failure = tr("Could not create a virtual gamepad. Switch to Keyboard/Mouse "
							 "Simulation or the ViGEm controller in Preferences.");
		}
		if (!client->executor)
		{
			qCritical() << "Failed to create a virtual gamepad for the new device:" << failure;
			ui->clientLabel->setText(failure.isEmpty() ? tr("Could not create a virtual gamepad.")
													   : failure);
			socket->close();
			socket->deleteLater();
			return;
		}
	}
	client->slot = pickSlot(client->executor.get());
	if (client->slot == 0)
	{
		qWarning() << "All controller slots are in use, refusing the new device";
		ui->clientLabel->setText(tr("All 6 controller slots are in use."));
		socket->close();
		socket->deleteLater();
		return;
	}
	socket->write("VGPSLOT:" + QByteArray::number(client->slot) + "\n");
	clients.push_back(std::move(client));
	clientConnection = socket;
	isGamepadConnected = true;
	qInfo() << "Device connected:" << socket->peerAddress().toString() << socket->peerPort()
			<< "total devices:" << clients.size();
	updateClientLabel();
	updateSlots();

	if (!multiPhone || clients.size() >= MAX_PHONES)
		tcpServer->pauseAccepting();

	connect(socket, &QAbstractSocket::disconnected, this, [this, socket]() {
		qInfo() << "Device disconnected.";
		qDebug() << "Average Request Interval" << averageRequestInterval
				 << "ms, Request Count:" << requestCount;
		if (executor)
			executor->release_all();
		std::erase_if(clients, [socket](const std::unique_ptr<Client> &c) {
			return c->socket == socket;
		});
		socket->deleteLater();
		isGamepadConnected = !clients.empty();
		if (clients.empty())
			clientConnection = nullptr;
		updateClientLabel();
		updateSlots();
		tcpServer->resumeAccepting();
	});
	connect(socket, &QAbstractSocket::readyRead, this, &Server::serveClient);

	qInfo() << "New client connection received";
}

void Server::serveClient()
{
	auto *socket = qobject_cast<QTcpSocket *>(sender());
	const auto clientIt = std::find_if(clients.begin(), clients.end(),
									   [socket](const std::unique_ptr<Client> &c) { return c->socket == socket; });
	if (clientIt == clients.end())
		return;
	Client &client = **clientIt;
	ExecutorInterface *activeExecutor = client.executor ? client.executor.get() : executor.get();
	if (activeExecutor == nullptr)
		return;

#ifdef QT_DEBUG
	qDebug() << "Received: " << socket->bytesAvailable() << "bytes";
#endif

	// Append new data to our buffer
	client.buffer.append(socket->readAll());

#ifdef QT_DEBUG
	qDebug() << "Buffer contents: " << client.buffer;
#endif

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

		// Process the gamepad reading
		requestCount++;
		// Calculate performance metrics
		if (lastRequestTime.isValid())
		{
			int elapsed = lastRequestTime.msecsTo(currentTime);
			// Update average request interval, using running average
			averageRequestInterval += (elapsed - averageRequestInterval) / requestCount;
		}
		lastRequestTime = currentTime;

		{
			const auto &r = result.reading;
			static QString lastSummary;
			const QString summary =
				QStringLiteral("down=%1 up=%2 LS=(%3,%4) RS=(%5,%6) LT=%7 RT=%8")
					.arg(r.buttons_down).arg(r.buttons_up)
					.arg(r.left_thumbstick_x, 0, 'f', 2).arg(r.left_thumbstick_y, 0, 'f', 2)
					.arg(r.right_thumbstick_x, 0, 'f', 2).arg(r.right_thumbstick_y, 0, 'f', 2)
					.arg(r.left_trigger, 0, 'f', 2).arg(r.right_trigger, 0, 'f', 2);
			if (summary != lastSummary)
			{
				diagLog(QStringLiteral("Frame %1 %2").arg(requestCount).arg(summary));
				lastSummary = summary;
			}
		}

		activeExecutor->inject_gamepad_state(result.reading);

		// Remove the processed data from the buffer
		client.buffer.remove(0, result.bytes_consumed);

#ifdef QT_DEBUG
		qDebug() << "Consumed" << result.bytes_consumed
				 << "bytes, remaining buffer size:" << client.buffer.size();
#endif
	}
}

void Server::destroyServer()
{
	emit navigateBack();
	this->deleteLater();
}
