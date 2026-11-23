#include "server.hpp"

#include "../../third-party-libs/QR-Code-generator/cpp/qrcodegen.hpp"
#include "../platform/elevation.hpp"
#include "../settings/settings_singleton.hpp"
#include "ui_server.h"

#include <QApplication>
#include <QClipboard>
#include <QDataStream>
#include <QHostAddress>
#include <QList>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QThread>
#include <QTimer>
#include <algorithm>

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
		const bool gamepadMode =
			SettingsSingleton::instance().executorType() == ExecutorType::GamepadExecutor;
		QString details =
			tr("Input: %1").arg(gamepadMode ? tr("Virtual Gamepad") : tr("Keyboard/Mouse Simulation"));
		if (isElevated())
			details += tr("   |   Administrator");
		auto *detailsLabel = new QLabel(details, this);
		detailsLabel->setProperty("role", "caption");
		auto *titleColumn = new QVBoxLayout();
		titleColumn->setSpacing(2);
		ui->headerLayout->removeWidget(ui->statusLabel);
		titleColumn->addWidget(ui->statusLabel);
		titleColumn->addWidget(detailsLabel);
		ui->headerLayout->insertLayout(0, titleColumn);
	}
	clientConnection = nullptr;
	tcpServer = new QTcpServer(this);
	isGamepadConnected = false;

	// Initialize the executor with try-catch for better error handling
	switch (ExecutorType executorType = SettingsSingleton::instance().executorType())
	{
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
	const bool multiPhone =
		SettingsSingleton::instance().executorType() == ExecutorType::GamepadExecutor;
	QTcpSocket *socket = tcpServer->nextPendingConnection();
	if (socket == nullptr)
		return;
	// disable Nagle's algorithm to avoid delay and bunching of small packages
	socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

	auto client = std::make_unique<Client>();
	client->socket = socket;
	if (multiPhone)
	{
		try
		{
			client->executor = std::make_unique<GamepadExecutor>();
		}
		catch (const std::exception &e)
		{
			qCritical() << "Failed to create a virtual gamepad for the new device:" << e.what();
			socket->close();
			socket->deleteLater();
			return;
		}
	}
	clients.push_back(std::move(client));
	clientConnection = socket;
	isGamepadConnected = true;
	qInfo() << "Device connected:" << socket->peerAddress().toString() << socket->peerPort()
			<< "total devices:" << clients.size();
	updateClientLabel();

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
