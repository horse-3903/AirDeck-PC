#pragma once

#include "executor.hpp"

#include <QByteArray>
#include <QDialog>
#include <QFrame>
#include <QLabel>
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
	void updateSlots();
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
	};
	struct SlotCard
	{
		QLabel *badge = nullptr;
		QLabel *title = nullptr;
		QLabel *detail = nullptr;
		QFrame *frame = nullptr;
	};
	std::vector<SlotCard> slotCards;
	static constexpr size_t MAX_PHONES = 6;
	std::vector<std::unique_ptr<Client>> clients;
	QTcpSocket *clientConnection;
	bool isGamepadConnected;
	QTime lastRequestTime;
	double averageRequestInterval = 0.0;
	uint_fast32_t requestCount = 0;
	QByteArray dataBuffer; // Buffer to store incoming data
	std::unique_ptr<ExecutorInterface> executor = nullptr;
};
