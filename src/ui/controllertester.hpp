#pragma once

#include <QWidget>

/**
 * Live picture of one controller as the PC receives it: buttons, both sticks and both triggers.
 */
class ControllerTester : public QWidget
{
	Q_OBJECT

  public:
	explicit ControllerTester(QWidget *parent = nullptr);

	void setSlot(int slot);
	void setState(quint32 held, float lx, float ly, float rx, float ry, float lt, float rt);
	QSize sizeHint() const override { return {760, 400}; }
	QSize minimumSizeHint() const override { return {420, 240}; }

  protected:
	void paintEvent(QPaintEvent *event) override;

  private:
	int m_slot = 1;
	quint32 m_held = 0;
	float m_lx = 0, m_ly = 0, m_rx = 0, m_ry = 0, m_lt = 0, m_rt = 0;
};
