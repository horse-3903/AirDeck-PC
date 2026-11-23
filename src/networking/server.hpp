#pragma once

#include "executor.hpp"

#include <QByteArray>
#include <QDialog>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTime>
#include <memory>
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

  public:
	explicit Server(QWidget *parent = nullptr);
	~Server() override;
	QTcpServer *tcpServer = nullptr;

  private slots:
	void handleConnection();
	void destroyServer();

  private:
	void initServer();
	void updateClientLabel();
	void serveClient();

	Ui::Server *ui;
	struct Client
	{
		QTcpSocket *socket = nullptr;
		QByteArray buffer;
		std::unique_ptr<ExecutorInterface> executor;
	};
	static constexpr size_t MAX_PHONES = 4;
	std::vector<std::unique_ptr<Client>> clients;
	QTcpSocket *clientConnection;
	bool isGamepadConnected;
	QTime lastRequestTime;
	double averageRequestInterval = 0.0;
	uint_fast32_t requestCount = 0;
	QByteArray dataBuffer; // Buffer to store incoming data
	std::unique_ptr<ExecutorInterface> executor = nullptr;
};
