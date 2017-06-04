#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "serialport.h"
#include "generalutilities.h"
#include "eventtimer.h"
#include "tscriptreader.h"
#include "tscriptexecutor.h"
#include "customaction.h"
#include "custommenu.h"
#include "qserialterminallineedit.h"
#include "qserialterminalstrings.h"
#include "qserialterminalicons.h"

#include <QString>
#include <QColor>
#include <QCloseEvent>
#include <QDesktopWidget>
#include <QMessageBox>
#include <QFileDialog>
#include <QTimer>
#include <QMenu>
#include <QAction>

const int MainWindow::s_SUCCESSFULLY_OPENED_SERIAL_PORT_MESSAGE_TIMEOUT{5000};
const int MainWindow::s_SERIAL_TIMEOUT{0};
const int MainWindow::s_TASKBAR_HEIGHT{15};
const int MainWindow::s_CHECK_PORT_DISCONNECT_TIMEOUT{750};
const int MainWindow::s_CHECK_PORT_RECEIVE_TIMEOUT{25};
const int MainWindow::s_NO_SERIAL_PORTS_CONNECTED_MESSAGE_TIMEOUT{5000};
const int MainWindow::s_SCRIPT_INDENT{0};
const int MainWindow::s_SERIAL_READ_TIMEOUT{500};
const std::string MainWindow::s_CARRIAGE_RETURN_LINE_ENDING{"\\\\r"};
const std::string MainWindow::s_NEW_LINE_LINE_ENDING{"\\\\n"};
const std::string MainWindow::s_CARRIAGE_RETURN_NEW_LINE_LINE_ENDING{"\\\\r\\\\n"};
const std::list<std::string> MainWindow::s_AVAILABLE_LINE_ENDINGS{s_CARRIAGE_RETURN_LINE_ENDING,
                                                                  s_NEW_LINE_LINE_ENDING,
                                                                  s_CARRIAGE_RETURN_NEW_LINE_LINE_ENDING};
const std::string MainWindow::s_DEFAULT_LINE_ENDING{MainWindow::s_CARRIAGE_RETURN_LINE_ENDING};

MainWindow::MainWindow(std::shared_ptr<QDesktopWidget> qDesktopWidget,
                       std::shared_ptr<QSerialTerminalIcons> programIcons,
                       QWidget *parent) :
    QMainWindow{parent},
    m_ui{std::make_shared<Ui::MainWindow>()},
    m_checkPortDisconnectTimer{new QTimer{}},
    m_checkSerialPortReceiveTimer{new QTimer{}},
    m_programIcons{programIcons},
    m_qDesktopWidget{qDesktopWidget},
    m_serialPortNames{SerialPort::availableSerialPorts()},
    m_packagedRxResultTask{MainWindow::staticPrintRxResult},
    m_packagedTxResultTask{MainWindow::staticPrintTxResult},
    m_packagedDelayResultTask{MainWindow::staticPrintDelayResult},
    m_packagedFlushResultTask{MainWindow::staticPrintFlushResult},
    m_packagedLoopResultTask{MainWindow::staticPrintLoopResult},
    m_currentLinePushedIntoCommandHistory{false},
    m_currentHistoryIndex{0},
    m_cancelScript{false},
    m_xPlacement{0},
    m_yPlacement{0}
{
    using namespace QSerialTerminalStrings;
    this->m_ui->setupUi(this);

    setupAdditionalUiComponents();

    connect(this->m_ui->actionAboutQt, SIGNAL(triggered(bool)), qApp, SLOT(aboutQt()));
    connect(this->m_ui->actionQuit, SIGNAL(triggered(bool)), this, SLOT(close()));
    connect(qApp, SIGNAL(aboutToQuit()), this, SLOT(onApplicationAboutToClose()));

    connect(this->m_ui->sendBox, SIGNAL(commandHistoryContextMenuRequested(const QPoint &)), this, SLOT(onCommandHistoryContextMenuRequested(const QPoint &)));
    connect(this->m_ui->actionConnect, SIGNAL(triggered(bool)), this, SLOT(onActionConnectTriggered(bool)));
    connect(this->m_ui->connectButton, SIGNAL(clicked(bool)), this, SLOT(onConnectButtonClicked(bool)));
    connect(this->m_ui->actionDisconnect, SIGNAL(triggered(bool)), this, SLOT(onActionDisconnectTriggered(bool)));
    connect(this->m_ui->actionLoadScript, SIGNAL(triggered(bool)), this, SLOT(onActionLoadScriptTriggered(bool)));

    connect(this->m_ui->sendButton, SIGNAL(clicked(bool)), this, SLOT(onSendButtonClicked()));
    connect(this->m_ui->sendBox, SIGNAL(returnPressed(QSerialTerminalLineEdit*)), this, SLOT(onReturnKeyPressed(QSerialTerminalLineEdit*)));
    connect(this->m_ui->sendBox, SIGNAL(upArrowPressed(QSerialTerminalLineEdit*)), this, SLOT(onUpArrowPressed(QSerialTerminalLineEdit*)));
    connect(this->m_ui->sendBox, SIGNAL(downArrowPressed(QSerialTerminalLineEdit*)), this, SLOT(onDownArrowPressed(QSerialTerminalLineEdit*)));
    connect(this->m_ui->sendBox, SIGNAL(escapePressed(QSerialTerminalLineEdit*)), this, SLOT(onEscapeKeyPressed(QSerialTerminalLineEdit*)));
    connect(this->m_ui->sendBox, SIGNAL(altPressed(QSerialTerminalLineEdit*)), this, SLOT(onAltKeyPressed(QSerialTerminalLineEdit*)));
    connect(this->m_ui->sendBox, SIGNAL(ctrlAPressed(QSerialTerminalLineEdit*)), this, SLOT(onCtrlAPressed(QSerialTerminalLineEdit*)));
    connect(this->m_ui->sendBox, SIGNAL(ctrlEPressed(QSerialTerminalLineEdit*)), this, SLOT(onCtrlEPressed(QSerialTerminalLineEdit*)));
    connect(this->m_ui->sendBox, SIGNAL(ctrlUPressed(QSerialTerminalLineEdit*)), this, SLOT(onCtrlUPressed(QSerialTerminalLineEdit*)));
    connect(this->m_ui->sendBox, SIGNAL(ctrlGPressed(QSerialTerminalLineEdit*)), this, SLOT(onCtrlGPressed(QSerialTerminalLineEdit*)));

    connect(this->m_ui->menuAbout, SIGNAL(aboutToShow(CustomMenu *)), this, SLOT(onContextMenuActive(CustomMenu *)));
    connect(this->m_ui->menuBaudRate, SIGNAL(aboutToShow(CustomMenu *)), this, SLOT(onContextMenuActive(CustomMenu *)));
    connect(this->m_ui->menuDataBits, SIGNAL(aboutToShow(CustomMenu *)), this, SLOT(onContextMenuActive(CustomMenu *)));
    connect(this->m_ui->menuFile, SIGNAL(aboutToShow(CustomMenu *)), this, SLOT(onContextMenuActive(CustomMenu *)));
    connect(this->m_ui->menuLineEndings, SIGNAL(aboutToShow(CustomMenu *)), this, SLOT(onContextMenuActive(CustomMenu *)));
    connect(this->m_ui->menuParity, SIGNAL(aboutToShow(CustomMenu *)), this, SLOT(onContextMenuActive(CustomMenu *)));
    connect(this->m_ui->menuPortNames, SIGNAL(aboutToShow(CustomMenu *)), this, SLOT(onContextMenuActive(CustomMenu *)));
    connect(this->m_ui->menuStopBits, SIGNAL(aboutToShow(CustomMenu *)), this, SLOT(onContextMenuActive(CustomMenu *)));

    connect(this->m_ui->menuAbout, SIGNAL(aboutToHide(CustomMenu *)), this, SLOT(onContextMenuInactive(CustomMenu *)));
    connect(this->m_ui->menuBaudRate, SIGNAL(aboutToHide(CustomMenu *)), this, SLOT(onContextMenuInactive(CustomMenu *)));
    connect(this->m_ui->menuDataBits, SIGNAL(aboutToHide(CustomMenu *)), this, SLOT(onContextMenuInactive(CustomMenu *)));
    connect(this->m_ui->menuFile, SIGNAL(aboutToHide(CustomMenu *)), this, SLOT(onContextMenuInactive(CustomMenu *)));
    connect(this->m_ui->menuLineEndings, SIGNAL(aboutToHide(CustomMenu *)), this, SLOT(onContextMenuInactive(CustomMenu *)));
    connect(this->m_ui->menuParity, SIGNAL(aboutToHide(CustomMenu *)), this, SLOT(onContextMenuInactive(CustomMenu *)));
    connect(this->m_ui->menuPortNames, SIGNAL(aboutToHide(CustomMenu *)), this, SLOT(onContextMenuInactive(CustomMenu *)));
    connect(this->m_ui->menuStopBits, SIGNAL(aboutToShow(CustomMenu *)), this, SLOT(onContextMenuInactive(CustomMenu *)));

    this->m_checkPortDisconnectTimer->setInterval(MainWindow::s_CHECK_PORT_DISCONNECT_TIMEOUT);
    this->m_checkSerialPortReceiveTimer->setInterval(MainWindow::s_CHECK_PORT_RECEIVE_TIMEOUT);
#if defined(_WIN32) || defined(__CYGWIN__)

#else
    connect(this->m_checkPortDisconnectTimer.get(), SIGNAL(timeout()), this, SLOT(checkDisconnectedSerialPorts()));
#endif
    connect(this->m_checkSerialPortReceiveTimer.get(), SIGNAL(timeout()), this, SLOT(launchSerialReceiveAsync()));
}

void MainWindow::setLineEnding(const std::string &lineEnding)
{
    using namespace QSerialTerminalStrings;
    if (lineEnding.find(MainWindow::s_CARRIAGE_RETURN_LINE_ENDING) != std::string::npos) {
        if (lineEnding.find(MainWindow::s_NEW_LINE_LINE_ENDING) != std::string::npos) {
            this->m_lineEnding = "\r\n";
        } else {
            this->m_lineEnding = "\r";
        }
    } else if (lineEnding.find(MainWindow::s_NEW_LINE_LINE_ENDING) != std::string::npos)  {
        this->m_lineEnding = "\n";
    } else {
        throw std::runtime_error(INVALID_LINE_ENDING_PASSED_TO_SET_LINE_ENDING_STRING);
    }
}

void MainWindow::autoSetLineEnding()
{
    for (auto &it : this->m_availableLineEndingActions) {
        if (it->isChecked()) {
            if (this->m_serialPort) {
                this->setLineEnding(it->text().toStdString());
                this->m_serialPort->setLineEnding(this->m_lineEnding);
            }
        }
    }
}

void MainWindow::onContextMenuActive(CustomMenu *customMenu)
{
    using namespace QSerialTerminalStrings;
    if (customMenu) {
        if (customMenu == this->m_ui->menuPortNames) {
            if (customMenu->actions().empty()) {
                customMenu->close();
                std::unique_ptr<QMessageBox> warningBox{new QMessageBox{}};
                warningBox->setText(NO_AVAILABLE_SERIAL_PORTS_STRING);
                warningBox->setWindowTitle(NO_AVAILABLE_SERIAL_PORTS_WINDOW_TITLE_STRING);
                warningBox->setWindowIcon(this->m_programIcons->MAIN_WINDOW_ICON);
                warningBox->exec();
            }
        }
    }
}

void MainWindow::onContextMenuInactive(CustomMenu *customMenu)
{
    (void) customMenu;
}

void MainWindow::appendReceivedString(const std::string &str)
{
    using namespace QSerialTerminalStrings;
    using namespace GeneralUtilities;
    if ((str != "") && (!isWhitespace(str)) && (!isLineEnding(str))) {
        this->printRxResult(str);
    }
}

void MainWindow::appendTransmittedString(const QString &str)
{
    using namespace QSerialTerminalStrings;
    if (this->m_serialPort) {
        if (!this->m_serialPort->isOpen()) {
            this->openSerialPort();
        }
        if (str.toStdString() != "") {
            this->m_commandHistory.insert(this->m_commandHistory.begin(), this->m_ui->sendBox->text());
            resetCommandHistory();
        }
        this->m_serialPort->writeLine(str.toStdString());
        this->printTxResult(str.toStdString());
        this->m_ui->sendBox->clear();
    }
}

void MainWindow::bindQDesktopWidget(std::shared_ptr<QDesktopWidget> qDesktopWidget)
{
    this->m_qDesktopWidget.reset();
    this->m_qDesktopWidget = qDesktopWidget;
}

void MainWindow::bindQSerialTerminalIcons(std::shared_ptr<QSerialTerminalIcons> qstiPtr)
{
    this->m_programIcons.reset();
    this->m_programIcons = qstiPtr;
}

void MainWindow::checkDisconnectedSerialPorts()
{
    using namespace GeneralUtilities;
    auto names = SerialPort::availableSerialPorts();
    auto maybeUpdate = std::vector<std::string>{};
    auto currents = std::vector<std::string>{};

    for (auto &it : names) {
        maybeUpdate.push_back(it);
    }
    for (auto &it : this->m_availablePortNamesActions) {
        currents.push_back(it->text().toStdString());
    }
    for (auto &it : maybeUpdate) {
        if (!itemExists(currents, it)) {
            this->addNewSerialPortInfoItem(SerialPortItemType::PORT_NAME, it);
        }
    }
    for (auto &it : currents) {
        if (!itemExists(maybeUpdate, it)) {
            this->removeOldSerialPortInfoItem(SerialPortItemType::PORT_NAME, it);
        }
    }
}

void MainWindow::addNewSerialPortInfoItem(SerialPortItemType serialPortItemType, const std::string &actionName)
{
    CustomAction *tempAction{new CustomAction{toQString(actionName), 0, this}};
    tempAction->setCheckable(true);

    if (serialPortItemType == SerialPortItemType::PORT_NAME) {
        connect(tempAction, SIGNAL(triggered(CustomAction*, bool)), this, SLOT(onActionPortNamesChecked(CustomAction*, bool)));
        this->m_availablePortNamesActions.push_back(tempAction);
        this->m_ui->menuPortNames->addAction(tempAction);
    } else if (serialPortItemType == SerialPortItemType::BAUD_RATE) {
        if (tempAction->text().toStdString() == SerialPort::DEFAULT_BAUD_RATE_STRING) {
            tempAction->setChecked(true);
        }
        connect(tempAction, SIGNAL(triggered(CustomAction*, bool)), this, SLOT(onActionBaudRateChecked(CustomAction*, bool)));
        this->m_availableBaudRateActions.push_back(tempAction);
        this->m_ui->menuBaudRate->addAction(tempAction);
    } else if (serialPortItemType == SerialPortItemType::PARITY) {
        if (tempAction->text().toStdString() == SerialPort::DEFAULT_PARITY_STRING) {
            tempAction->setChecked(true);
        }
        connect(tempAction, SIGNAL(triggered(CustomAction*, bool)), this, SLOT(onActionParityChecked(CustomAction*, bool)));
        this->m_availableParityActions.push_back(tempAction);
        this->m_ui->menuParity->addAction(tempAction);
    } else if (serialPortItemType == SerialPortItemType::DATA_BITS) {
        if (tempAction->text().toStdString() == SerialPort::DEFAULT_DATA_BITS_STRING) {
            tempAction->setChecked(true);
        }
        connect(tempAction, SIGNAL(triggered(CustomAction*, bool)), this, SLOT(onActionDataBitsChecked(CustomAction*, bool)));
        this->m_availableDataBitsActions.push_back(tempAction);
        this->m_ui->menuDataBits->addAction(tempAction);
    } else if (serialPortItemType == SerialPortItemType::STOP_BITS) {
        if (tempAction->text().toStdString() == SerialPort::DEFAULT_STOP_BITS_STRING) {
            tempAction->setChecked(true);
        }
        connect(tempAction, SIGNAL(triggered(CustomAction*, bool)), this, SLOT(onActionStopBitsChecked(CustomAction*, bool)));
        this->m_availableStopBitsActions.push_back(tempAction);
        this->m_ui->menuStopBits->addAction(tempAction);
    } else if (serialPortItemType == SerialPortItemType::LINE_ENDING) {
        if (tempAction->text().toStdString() == MainWindow::s_DEFAULT_LINE_ENDING) {
            tempAction->setChecked(true);
        }
        connect(tempAction, SIGNAL(triggered(CustomAction*, bool)), this, SLOT(onActionLineEndingsChecked(CustomAction*, bool)));
        this->m_availableLineEndingActions.push_back(tempAction);
        this->m_ui->menuLineEndings->addAction(tempAction);
    } else {
        delete tempAction;
    }
}

void MainWindow::removeOldSerialPortInfoItem(SerialPortItemType serialPortItemType, const std::string &actionName)
{
    using namespace QSerialTerminalStrings;
    int i{0};
    if (serialPortItemType == SerialPortItemType::PORT_NAME) {
        for (auto &it : this->m_availablePortNamesActions) {
            if (it->text().toStdString() == actionName) {
                this->m_ui->statusBar->showMessage(SERIAL_PORT_DISCONNECTED_STRING + it->text());
                if (it->isChecked()) {
                    this->m_ui->actionConnect->setChecked(false);
                    this->m_ui->connectButton->setChecked(false);
                    this->m_ui->actionConnect->setEnabled(false);
                    this->m_ui->connectButton->setEnabled(false);
                    this->m_ui->actionLoadScript->setEnabled(false);
                    this->m_ui->sendBox->setEnabled(false);
                    this->m_ui->sendButton->setEnabled(false);
                }
                it->deleteLater();
                this->m_availablePortNamesActions.erase(this->m_availablePortNamesActions.begin() + i);
                return;
            }
            i++;
        }
    } else if (serialPortItemType == SerialPortItemType::BAUD_RATE) {
        for (auto &it : this->m_availableBaudRateActions) {
            if (it->text().toStdString() == actionName) {
                it->deleteLater();
                this->m_availableBaudRateActions.erase(this->m_availableBaudRateActions.begin() + i);
                return;
            }
            i++;
        }
    } else if (serialPortItemType == SerialPortItemType::PARITY) {
        for (auto &it : this->m_availableParityActions) {
            if (it->text().toStdString() == actionName) {
                it->deleteLater();
                this->m_availableParityActions.erase(this->m_availableParityActions.begin() + i);
                return;
            }
            i++;
        }
    } else if (serialPortItemType == SerialPortItemType::DATA_BITS) {
        for (auto &it : this->m_availableDataBitsActions) {
            if (it->text().toStdString() == actionName) {
                it->deleteLater();
                this->m_availableDataBitsActions.erase(this->m_availableDataBitsActions.begin() + i);
                return;
            }
            i++;
        }
    } else if (serialPortItemType == SerialPortItemType::STOP_BITS) {
        for (auto &it : this->m_availableStopBitsActions) {
            if (it->text().toStdString() == actionName) {
                it->deleteLater();
                this->m_availableStopBitsActions.erase(this->m_availableStopBitsActions.begin() + i);
                return;
            }
            i++;
        }
    }
}

void MainWindow::launchSerialReceiveAsync()
{
    try {
        if (!this->m_serialReceiveAsyncHandle) {
           this->m_serialReceiveAsyncHandle = std::unique_ptr< std::future<std::string> >{new std::future<std::string>{std::async(std::launch::async,
                                                                                        &MainWindow::checkSerialReceive,
                                                                                        this)}};
        }
        if (this->m_serialReceiveAsyncHandle->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            appendReceivedString(this->m_serialReceiveAsyncHandle->get());
            this->m_serialReceiveAsyncHandle = std::unique_ptr<std::future<std::string>>{new std::future<std::string>{std::async(std::launch::async,
                                                                                          &MainWindow::checkSerialReceive,
                                                                                          this)}};
        }
    } catch (std::exception &e) {
        std::cout << e.what() << std::endl;
    }
}

std::string MainWindow::checkSerialReceive()
{
    using namespace GeneralUtilities;
    try {
        if (this->m_serialPort) {
            if (!this->m_serialPort->isOpen()) {
                this->m_serialPort->openPort();
            }
            this->autoSetLineEnding();
            this->m_serialPort->setLineEnding(this->m_lineEnding);
            std::string returnString{this->m_serialPort->readLine()};
            if (!returnString.empty()) {
                return returnString;
            }
        }
    } catch (std::exception &e) {
        std::cout << e.what() << std::endl;
        return "";
    }
    return "";
}

void MainWindow::centerAndFitWindow()
{
    calculateXYPlacement();
    this->move(this->m_xPlacement, this->m_yPlacement);
}

int MainWindow::xPlacement() const
{
    return this->m_xPlacement;
}

int MainWindow::yPlacement() const
{
    return this->m_yPlacement;
}

void MainWindow::calculateXYPlacement()
{
    std::unique_ptr<QRect> avail{new QRect{(this->m_qDesktopWidget->availableGeometry())}};
    this->m_xPlacement = (avail->width()/2)-(this->width()/2);
    this->m_yPlacement = (avail->height()/2)-(this->height()/2) - MainWindow::s_TASKBAR_HEIGHT;
}

void MainWindow::resetCommandHistory()
{
    this->m_currentHistoryIndex = 0;
    this->m_currentLinePushedIntoCommandHistory = false;
    clearEmptyStringsFromCommandHistory();
}

void MainWindow::clearEmptyStringsFromCommandHistory()
{
    using namespace GeneralUtilities;
    while (true) {
        int i{0};
        for (auto &it : this->m_commandHistory) {
            if ((isWhitespace(it.toStdString())) || (it.toStdString() == "")) {
                this->m_commandHistory.erase(this->m_commandHistory.begin() + i);
                break;
            }
            i++;
        }
        break;
    }
}

void MainWindow::onSendButtonClicked()
{
    using namespace QSerialTerminalStrings;
    using namespace GeneralUtilities;
    if (this->m_ui->sendButton->text() == toQString(SEND_STRING)) {
        if (this->m_serialPort) {
            appendTransmittedString(toQString(stripLineEndings(this->m_ui->sendBox->text().toStdString())));
        } else {
            if (this->m_availablePortNamesActions.size() != 0) {
                onActionConnectTriggered(false);
                onSendButtonClicked();
            } else {
                this->m_ui->statusBar->showMessage(NO_SERIAL_PORTS_CONNECTED_STRING);
            }
        }
    } else {
        this->m_cancelScript = true;
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event);
    this->m_cancelScript = true;
    event->accept();
    /*
    using namespace QSerialTerminalStrings;
    QMessageBox::StandardButton userReply;
    userReply = QMessageBox::question(this, QUIT_PROMPT_WINDOW_TITLE_STRING, QUIT_PROMPT_STRING, QMessageBox::Yes|QMessageBox::No);
    if (userReply == QMessageBox::Yes) {
        event->accept();
        QMainWindow::closeEvent(event);
    } else {
        event->ignore();
    }
    */
}

void MainWindow::onCommandHistoryContextMenuRequested(const QPoint &point)
{
    using namespace QSerialTerminalStrings;
    using namespace GeneralUtilities;
    QMenu commandHistoryContextMenu(COMMAND_HISTORY_CONTEXT_MENU_STRING, this);
    int i{0};
    for (auto &it : this->m_commandHistory) {
        if ((isWhitespace(it.toStdString())) || (it.toStdString() == "")) {
            i++;
            continue;
        }
        CustomAction *tempAction{new CustomAction{it, i++, this}};
        commandHistoryContextMenu.addAction(tempAction);
        connect(tempAction, SIGNAL(triggered(CustomAction *,bool)), this, SLOT(onCommandHistoryContextMenuActionTriggered(CustomAction *, bool)));
    }
    commandHistoryContextMenu.exec(mapToGlobal(point));
}

void MainWindow::onCommandHistoryContextMenuActionTriggered(CustomAction *action, bool checked)
{
    (void)checked;
    if (action) {
        this->m_currentHistoryIndex = action->index();
        this->m_ui->sendBox->setText(action->text());
    }
}

void MainWindow::setupAdditionalUiComponents()
{
    using namespace QSerialTerminalStrings;

    for (auto &it : MainWindow::s_AVAILABLE_LINE_ENDINGS) {
        this->addNewSerialPortInfoItem(SerialPortItemType::LINE_ENDING, it);
    }

    for (auto &it : SerialPort::availableDataBits()) {
        this->addNewSerialPortInfoItem(SerialPortItemType::DATA_BITS, it);
    }

    for (auto &it : SerialPort::availableStopBits()) {
        this->addNewSerialPortInfoItem(SerialPortItemType::STOP_BITS, it);
    }

    for (auto &it : SerialPort::availableParity()) {
        this->addNewSerialPortInfoItem(SerialPortItemType::PARITY, it);
    }

    for (auto &it : SerialPort::availableBaudRates()) {
        this->addNewSerialPortInfoItem(SerialPortItemType::BAUD_RATE, it);
    }

    for (auto &it : SerialPort::availableSerialPorts()) {
        this->addNewSerialPortInfoItem(SerialPortItemType::PORT_NAME, it);
    }

    if (!this->m_availablePortNamesActions.empty()) {
        (*std::begin(this->m_availablePortNamesActions))->setChecked(true);
        this->m_ui->connectButton->setEnabled(true);
        this->m_ui->actionConnect->setEnabled(true);
    } else {
        this->m_ui->connectButton->setEnabled(false);
        this->m_ui->actionConnect->setEnabled(false);
    }
    this->m_ui->sendBox->setEnabled(false);
    this->m_ui->sendButton->setEnabled(false);
    this->m_ui->sendBox->setToolTip(SEND_BOX_DISABLED_TOOLTIP);
    this->m_ui->actionLoadScript->setEnabled(false);
    this->m_ui->actionLoadScript->setToolTip(ACTION_LOAD_SCRIPT_DISABLED_TOOLTIP);
    this->m_ui->sendBox->setTabOrder(this->m_ui->sendBox, this->m_ui->sendButton);
    this->m_ui->statusBar->showMessage(CONNECT_TO_SERIAL_PORT_TO_BEGIN_STRING);
}

void MainWindow::onActionBaudRateChecked(CustomAction *action, bool checked)
{
    (void)checked;
    for (auto &it : this->m_availableBaudRateActions) {
        if (it == action) {
            action->setChecked(true);
        } else {
            it->setChecked(false);
        }
    }
}

void MainWindow::onActionParityChecked(CustomAction *action, bool checked)
{
    (void)checked;
    for (auto &it : this->m_availableParityActions) {
        if (it == action) {
            action->setChecked(true);
        } else {
            it->setChecked(false);
        }
    }
}

void MainWindow::onActionDataBitsChecked(CustomAction *action, bool checked)
{
    (void)checked;
    for (auto &it : this->m_availableDataBitsActions) {
        if (it == action) {
            action->setChecked(true);
        } else {
            it->setChecked(false);
        }
    }
}

void MainWindow::onActionStopBitsChecked(CustomAction *action, bool checked)
{
    (void)checked;
    for (auto &it : this->m_availableStopBitsActions) {
        if (it == action) {
            action->setChecked(true);
        } else {
            it->setChecked(false);
        }
    }
}

void MainWindow::onActionLineEndingsChecked(CustomAction *action, bool checked)
{
    (void)checked;
    for (auto &it : this->m_availableLineEndingActions) {
        if (it == action) {
            action->setChecked(true);
            if (this->m_serialPort) {
                this->setLineEnding(action->text().toStdString());
                this->m_serialPort->setLineEnding(this->m_lineEnding);
            }
        } else {
            it->setChecked(false);
        }
    }
}

void MainWindow::onActionPortNamesChecked(CustomAction *action, bool checked)
{
    (void)checked;
    if (!action->isChecked()) {
        this->m_ui->connectButton->setEnabled(false);
    } else {
        this->m_ui->connectButton->setEnabled(true);
    }
    for (auto &it : this->m_availablePortNamesActions) {
        if (it == action) {
            action->setChecked(true);
        } else {
            it->setChecked(false);
        }
    }
}

std::string MainWindow::getSerialPortItemFromActions(SerialPortItemType serialPortItemType)
{
    if (serialPortItemType == SerialPortItemType::PORT_NAME) {
        for (auto &it : this->m_availablePortNamesActions) {
            if (it->isChecked()) {
                return it->text().toStdString();
            }
        }
    } else if (serialPortItemType == SerialPortItemType::BAUD_RATE) {
        for (auto &it : this->m_availableBaudRateActions) {
            if (it->isChecked()) {
                return it->text().toStdString();
            }
        }
    } else if (serialPortItemType == SerialPortItemType::PARITY) {
        for (auto &it : this->m_availableParityActions) {
            if (it->isChecked()) {
                return it->text().toStdString();
            }
        }
    } else if (serialPortItemType == SerialPortItemType::DATA_BITS) {
        for (auto &it : this->m_availableDataBitsActions) {
            if (it->isChecked()) {
                return it->text().toStdString();
            }
        }
    } else if (serialPortItemType == SerialPortItemType::STOP_BITS) {
        for (auto &it : this->m_availableStopBitsActions) {
            if (it->isChecked()) {
                return it->text().toStdString();
            }
        }
    }
    return "";
}

void MainWindow::onActionConnectTriggered(bool checked)
{
    if (!checked) {
        this->onActionDisconnectTriggered(checked);
        return;
    }
    using namespace QSerialTerminalStrings;
    this->pauseCommunication();

    BaudRate baudRate{SerialPort::parseBaudRateFromRaw(this->getSerialPortItemFromActions(SerialPortItemType::BAUD_RATE))};
    Parity parity{SerialPort::parseParityFromRaw(this->getSerialPortItemFromActions(SerialPortItemType::PARITY))};
    StopBits stopBits{SerialPort::parseStopBitsFromRaw(this->getSerialPortItemFromActions(SerialPortItemType::STOP_BITS))};
    DataBits dataBits{SerialPort::parseDataBitsFromRaw(this->getSerialPortItemFromActions(SerialPortItemType::DATA_BITS))};
    std::string portName{this->getSerialPortItemFromActions(SerialPortItemType::PORT_NAME)};
    if (this->m_serialPort) {
        if (baudRate == this->m_serialPort->baudRate() &&
                parity == this->m_serialPort->parity() &&
                stopBits == this->m_serialPort->stopBits() &&
                dataBits == this->m_serialPort->dataBits() &&
                portName == this->m_serialPort->portName()) {
            if (!this->m_serialPort->isOpen()) {
                try {
                    openSerialPort();
                } catch (std::exception &e) {
                    (void)e;
                }
            }
            return;
        } else {
            try {
                closeSerialPort();
                this->m_serialPort.reset();
                this->m_serialPort = std::make_shared<SerialPort>(portName, baudRate, dataBits, stopBits, parity);
                openSerialPort();
            } catch (std::exception &e) {
                (void)e;
                std::unique_ptr<QMessageBox> warningBox{new QMessageBox{}};
                warningBox->setText(INVALID_SETTINGS_DETECTED_STRING + toQString(e.what()));
                warningBox->setWindowTitle(INVALID_SETTINGS_DETECTED_WINDOW_TITLE_STRING);
                warningBox->setWindowIcon(this->m_programIcons->MAIN_WINDOW_ICON);
                warningBox->exec();
                if (this->m_serialPort) {
                    closeSerialPort();
                }
            }
        }
    } else {
        try {
            this->m_serialPort = std::make_shared<SerialPort>(portName, baudRate, dataBits, stopBits, parity);
            openSerialPort();
        } catch (std::exception &e) {
            std::unique_ptr<QMessageBox> warningBox{new QMessageBox{}};
            warningBox->setText(INVALID_SETTINGS_DETECTED_STRING + toQString(e.what()));
            warningBox->setWindowTitle(INVALID_SETTINGS_DETECTED_WINDOW_TITLE_STRING);
            warningBox->setWindowIcon(this->m_programIcons->MAIN_WINDOW_ICON);
            warningBox->exec();
            if (this->m_serialPort) {
                closeSerialPort();
            }
        }
   }
}

void MainWindow::openSerialPort()
{
    using namespace QSerialTerminalStrings;
#if defined(__ANDROID__)
    //SystemCommand systemCommand{ANDROID_PERMISSION_BASE_STRING + this->m_serialPort->portName() + "\""};
    //systemCommand.execute();
#endif

    try {
        this->m_serialPort->openPort();
        this->m_ui->terminal->clear();
        this->m_ui->connectButton->setChecked(true);
        this->m_ui->sendButton->setEnabled(true);
        this->m_ui->actionDisconnect->setEnabled(true);
        this->m_ui->sendBox->setEnabled(true);
        this->m_ui->sendBox->setToolTip(SEND_BOX_ENABLED_TOOLTIP);
        this->m_ui->actionLoadScript->setEnabled(true);
        this->m_ui->actionLoadScript->setToolTip(ACTION_LOAD_SCRIPT_ENABLED_TOOLTIP);
        this->m_ui->sendBox->setFocus();
        this->m_ui->statusBar->showMessage(SUCCESSFULLY_OPENED_SERIAL_PORT_STRING + toQString(this->m_serialPort->portName()));
        this->m_serialPort->setTimeout(MainWindow::s_SERIAL_READ_TIMEOUT);
        beginCommunication();
    } catch (std::exception &e) {
        std::unique_ptr<QMessageBox> warningBox{new QMessageBox{}};
        warningBox->setText(COULD_NOT_OPEN_SERIAL_PORT_STRING + toQString(e.what()));
        warningBox->setWindowTitle(COULD_NOT_OPEN_SERIAL_PORT_WINDOW_TITLE_STRING);
        warningBox->setWindowIcon(this->m_programIcons->MAIN_WINDOW_ICON);
        warningBox->exec();
        if (this->m_serialPort) {
            closeSerialPort();
        }
    }
}

void MainWindow::closeSerialPort()
{
    using namespace QSerialTerminalStrings;
    this->m_serialPort->closePort();
    this->m_ui->connectButton->setChecked(false);
    this->m_ui->actionDisconnect->setEnabled(false);
    this->m_ui->sendBox->setEnabled(false);
    this->m_ui->sendBox->setToolTip(SEND_BOX_DISABLED_TOOLTIP);
    this->m_ui->actionLoadScript->setEnabled(false);
    this->m_ui->actionLoadScript->setToolTip(ACTION_LOAD_SCRIPT_DISABLED_TOOLTIP);
    this->m_ui->statusBar->showMessage(SUCCESSFULLY_CLOSED_SERIAL_PORT_STRING + toQString(this->m_serialPort->portName()));
    this->setWindowTitle(MAIN_WINDOW_TITLE);
}

void MainWindow::begin()
{
    this->show();
    this->centerAndFitWindow();
    this->m_checkPortDisconnectTimer->start();
    this->m_checkSerialPortReceiveTimer->start();
}

void MainWindow::beginCommunication()
{
    using namespace GeneralUtilities;
    using namespace QSerialTerminalStrings;
    if (this->m_serialPort) {
        try {
            if (!this->m_serialPort->isOpen()) {
                openSerialPort();
                return;
            }
            this->setWindowTitle(this->windowTitle() + " - " + toQString(this->m_serialPort->portName()));
            this->m_ui->statusBar->showMessage(toQString(SUCCESSFULLY_OPENED_SERIAL_PORT_STRING) + toQString(this->m_serialPort->portName()));
            this->m_checkSerialPortReceiveTimer->start();
        } catch (std::exception &e) {
            std::unique_ptr<QMessageBox> warningBox{new QMessageBox{}};
            warningBox->setText(INVALID_SETTINGS_DETECTED_STRING + toQString(e.what()));
            warningBox->setWindowTitle(INVALID_SETTINGS_DETECTED_WINDOW_TITLE_STRING);
            warningBox->setWindowIcon(this->m_programIcons->MAIN_WINDOW_ICON);
            warningBox->exec();
        }
    } else {
        BaudRate baudRate{SerialPort::parseBaudRateFromRaw(this->getSerialPortItemFromActions(SerialPortItemType::BAUD_RATE))};
        Parity parity{SerialPort::parseParityFromRaw(this->getSerialPortItemFromActions(SerialPortItemType::PARITY))};
        StopBits stopBits{SerialPort::parseStopBitsFromRaw(this->getSerialPortItemFromActions(SerialPortItemType::STOP_BITS))};
        DataBits dataBits{SerialPort::parseDataBitsFromRaw(this->getSerialPortItemFromActions(SerialPortItemType::DATA_BITS))};
        std::string portName{this->getSerialPortItemFromActions(SerialPortItemType::PORT_NAME)};
        try {
            this->m_serialPort = std::make_shared<SerialPort>(portName, baudRate, dataBits, stopBits, parity);
            beginCommunication();
        } catch (std::exception &e) {
            (void)e;
        }
    }
}

void MainWindow::pauseCommunication()
{

}

void MainWindow::stopCommunication()
{
    using namespace QSerialTerminalStrings;
    this->m_checkSerialPortReceiveTimer->stop();
    if (this->m_serialPort) {
        closeSerialPort();
    }
}

void MainWindow::onActionDisconnectTriggered(bool checked)
{
    (void)checked;
    using namespace QSerialTerminalStrings;
    if (this->m_serialPort) {
        closeSerialPort();
        this->m_ui->connectButton->setChecked(false);
        std::cout << "Waiting for async read of serial port " << this->m_serialPort->portName() << " to finish..." << std::endl;
        while (this->m_serialReceiveAsyncHandle->wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            std::this_thread::yield();
        }
        std::cout << "Completed, resetting serial port shared_ptr" << std::endl;
        this->m_serialPort.reset();
    }
}

void MainWindow::printRxResult(const std::string &str)
{
    using namespace QSerialTerminalStrings;
    using namespace GeneralUtilities;
    if ((str != "") && (!isWhitespace(str)) && (!isLineEnding(str))) {
        std::lock_guard<std::mutex> ioLock{this->m_printToTerminalMutex};
        this->m_ui->terminal->setTextColor(QColor(RED_COLOR_STRING));
        this->m_ui->terminal->append(toQString(tWhitespace(MainWindow::s_SCRIPT_INDENT)) + toQString(TERMINAL_RECEIVE_BASE_STRING) + toQString(stripLineEndings(stripNonAsciiCharacters(str))));
    }
}

void MainWindow::printTxResult(const std::string &str)
{
    using namespace QSerialTerminalStrings;
    using namespace GeneralUtilities;
    std::lock_guard<std::mutex> ioLock{this->m_printToTerminalMutex};
    this->m_ui->terminal->setTextColor(QColor(BLUE_COLOR_STRING));
    this->m_ui->terminal->append(toQString(tWhitespace(MainWindow::s_SCRIPT_INDENT)) + toQString(TERMINAL_TRANSMIT_BASE_STRING) + toQString(str));
}

void MainWindow::printDelayResult(DelayType delayType, int howLong)
{
    using namespace QSerialTerminalStrings;
    using namespace GeneralUtilities;
    std::lock_guard<std::mutex> ioLock{this->m_printToTerminalMutex};
    this->m_ui->terminal->setTextColor(QColor(GREEN_COLOR_STRING));
    QString stringToAppend{toQString(tWhitespace(MainWindow::s_SCRIPT_INDENT)) + toQString(TERMINAL_DELAY_BASE_STRING) + toQString(howLong)};
    if (delayType == DelayType::SECONDS) {
        stringToAppend.append(SECONDS_SUFFIX_STRING);
    } else if (delayType == DelayType::MILLISECONDS) {
        stringToAppend.append(MILLISECONDS_SUFFIX_STRING);
    } else if (delayType == DelayType::MICROSECONDS) {
        stringToAppend.append(MICROSECONDS_SUFFIX_STRING);
    }
    this->m_ui->terminal->append(stringToAppend);
}

void MainWindow::printFlushResult(FlushType flushType)
{
    using namespace GeneralUtilities;
    using namespace QSerialTerminalStrings;
    std::lock_guard<std::mutex> ioLock{this->m_printToTerminalMutex};
    QString stringToAppend{""};
    if (flushType == FlushType::RX) {
        stringToAppend.append(TERMINAL_FLUSH_RX_BASE_STRING);
    } else if (flushType == FlushType::TX) {
        stringToAppend.append(TERMINAL_FLUSH_TX_BASE_STRING);
    } else if (flushType == FlushType::RX_TX) {
        stringToAppend.append(TERMINAL_FLUSH_RX_TX_BASE_STRING);
    }
    this->m_ui->terminal->setTextColor(QColor(GRAY_COLOR_STRING));
    this->m_ui->terminal->append(toQString(tWhitespace(MainWindow::s_SCRIPT_INDENT)) + stringToAppend);
    this->m_ui->terminal->setTextColor(QColor());
}

QApplication *MainWindow::application()
{
    return qApp;
}


void MainWindow::printLoopResult(LoopType loopType, int currentLoop, int loopCount)
{
    using namespace GeneralUtilities;
    using namespace QSerialTerminalStrings;
    std::lock_guard<std::mutex> ioLock{this->m_printToTerminalMutex};
    QString stringToAppend{""};
    this->m_ui->terminal->setTextColor(QColor(ORANGE_COLOR_STRING));
    if (loopCount == -1) {
        if (loopType == LoopType::START) {
            if (currentLoop == 0) {
                this->m_ui->terminal->append(toQString(tWhitespace(MainWindow::s_SCRIPT_INDENT)) + BEGINNING_INFINITE_LOOP_STRING);
            }
            this->m_ui->terminal->append(toQString(tWhitespace(MainWindow::s_SCRIPT_INDENT)) + toQString(BEGIN_LOOP_BASE_STRING) + toQString(currentLoop + 1) + toQString(INFINITE_LOOP_COUNT_TAIL_STRING));
        } else if (loopType == LoopType::END) {
            this->m_ui->terminal->append(toQString(tWhitespace(MainWindow::s_SCRIPT_INDENT)) + toQString(END_LOOP_BASE_STRING) + toQString(currentLoop + 1) + toQString(INFINITE_LOOP_COUNT_TAIL_STRING));
        }
    } else {
        if (loopType == LoopType::START) {
            if (currentLoop == 0) {
                this->m_ui->terminal->append(toQString(tWhitespace(MainWindow::s_SCRIPT_INDENT)) + toQString(BEGINNING_LOOPS_BASE_STRING) + toQString(loopCount) + toQString(LOOPS_TAIL_STRING));
            }
            this->m_ui->terminal->append(toQString(tWhitespace(MainWindow::s_SCRIPT_INDENT)) + toQString(BEGIN_LOOP_BASE_STRING) + toQString(currentLoop + 1) + toQString("/") + toQString(loopCount) + toQString(")"));
        } else if (loopType == LoopType::END) {
            this->m_ui->terminal->append(toQString(tWhitespace(MainWindow::s_SCRIPT_INDENT)) + toQString(END_LOOP_BASE_STRING) + toQString(currentLoop + 1) + toQString("/") + toQString(loopCount) + toQString(")"));
            if ((currentLoop+1) == loopCount) {
                this->m_ui->terminal->append(toQString(tWhitespace(MainWindow::s_SCRIPT_INDENT)) + toQString(ENDING_LOOPS_BASE_STRING) + toQString(loopCount) + toQString(LOOPS_TAIL_STRING));
            }
        }
        this->m_ui->terminal->setTextColor(QColor());
    }
}

void MainWindow::staticPrintRxResult(MainWindow *mainWindow, const std::string &str)
{
    if (mainWindow) {
        mainWindow->printRxResult(str);
    }
}

void MainWindow::staticPrintTxResult(MainWindow *mainWindow, const std::string &str)
{
    if (mainWindow) {
        mainWindow->printTxResult(str);
    }
}

void MainWindow::staticPrintDelayResult(MainWindow *mainWindow,  DelayType delayType, int howLong)
{
    if (mainWindow) {
        mainWindow->printDelayResult(delayType, howLong);
    }
}

void MainWindow::staticPrintFlushResult(MainWindow *mainWindow, FlushType flushType)
{
    if (mainWindow) {
        mainWindow->printFlushResult(flushType);
    }
}

void MainWindow::staticPrintLoopResult(MainWindow *mainWindow, LoopType loopType, int currentLoop, int loopCount)
{
    if (mainWindow) {
        mainWindow->printLoopResult(loopType, currentLoop, loopCount);
    }
}


void MainWindow::onActionLoadScriptTriggered(bool checked)
{
    (void)checked;
    using namespace QSerialTerminalStrings;
    this->m_checkSerialPortReceiveTimer->stop();
    this->m_checkPortDisconnectTimer->stop();
    QFile homeCheck{OPEN_SCRIPT_FILE_DEFAULT_DIRECTORY};
    QString defaultScriptFileDirectory{""};
    if (homeCheck.exists()) {
        defaultScriptFileDirectory = OPEN_SCRIPT_FILE_DEFAULT_DIRECTORY;
    }
    QFile file{QFileDialog::getOpenFileName(this, OPEN_SCRIPT_FILE_CAPTION, defaultScriptFileDirectory)};
    if (file.fileName() == "") {
        return;
    }
    if (file.exists()) {
        std::unique_ptr<TScriptExecutor> scriptExecutor{new TScriptExecutor{file.fileName().toStdString()}};
        if (scriptExecutor->hasCommands()) {
            std::unique_ptr<QMessageBox> warningBox{new QMessageBox{}};
            warningBox->setText(EMPTY_SCRIPT_STRING + file.fileName());
            warningBox->setWindowTitle(EMPTY_SCRIPT_WINDOW_TITLE_STRING);
            warningBox->setWindowIcon(this->m_programIcons->MAIN_WINDOW_ICON);
            warningBox->exec();
        } else {
            this->m_ui->terminal->append("");
            this->m_ui->terminal->append(EXECUTING_SCRIPT_STRING + file.fileName());
            this->m_ui->sendBox->setEnabled(false);
            this->m_ui->sendButton->setText(CANCEL_SCRIPT_STRING);
            scriptExecutor->execute(this,
                                    this->m_serialPort,
                                    this->m_packagedRxResultTask,
                                    this->m_packagedTxResultTask,
                                    this->m_packagedDelayResultTask,
                                    this->m_packagedFlushResultTask,
                                    this->m_packagedLoopResultTask);
           this->m_ui->terminal->setTextColor(QColor());
            if (this->m_cancelScript) {
                this->m_ui->terminal->append(CANCELED_EXECUTING_SCRIPT_STRING + file.fileName());
                this->m_cancelScript = false;
            } else {
                this->m_ui->terminal->append(FINISHED_EXECUTING_SCRIPT_STRING + file.fileName());
            }
            this->m_serialPort->flushRXTX();
            printFlushResult(FlushType::RX_TX);
            this->m_ui->sendBox->setEnabled(false);
            this->m_ui->sendBox->setFocus();
            this->m_ui->sendButton->setText(SEND_STRING);
        }
    } else {
        std::unique_ptr<QMessageBox> warningBox{new QMessageBox{}};
        warningBox->setText(FILE_DOES_NOT_EXIST_STRING + file.fileName());
        warningBox->setWindowTitle(FILE_DOES_NOT_EXIST_WINDOW_TITLE_STRING);
        warningBox->setWindowIcon(this->m_programIcons->MAIN_WINDOW_ICON);
        warningBox->exec();
    }
    this->m_checkSerialPortReceiveTimer->start();
    this->m_checkPortDisconnectTimer->start();
}

void MainWindow::processEvents()
{
    this->application()->processEvents();
}

bool MainWindow::cancelScript() const
{
    return this->m_cancelScript;
}

void MainWindow::keyPressEvent(QKeyEvent *qke)
{
    if (qke) {
        if ((qke->key() == Qt::Key_Enter) || (qke->key() == Qt::Key_Return)) {
            this->m_ui->sendBox->clearFocus();
            this->onReturnKeyPressed();
        } else if (qke->key() == Qt::Key_Up) {
            this->onUpArrowPressed();
        } else if (qke->key() == Qt::Key_Down) {
            this->onDownArrowPressed();
        } else if (qke->key() == Qt::Key_Escape) {
            this->onEscapeKeyPressed();
        } else if (qke->key() == Qt::Key_Alt) {
            this->onAltKeyPressed();
        } else if ((qke->key() == Qt::Key_A) && (qke->modifiers().testFlag(Qt::ControlModifier))) {
            this->onCtrlAPressed();
        } else if ((qke->key() == Qt::Key_E) && (qke->modifiers().testFlag(Qt::ControlModifier))) {
            this->onCtrlEPressed();
        } else if ((qke->key() == Qt::Key_U) && (qke->modifiers().testFlag(Qt::ControlModifier))) {
            this->onCtrlUPressed();
        } else if ((qke->key() == Qt::Key_G) && (qke->modifiers().testFlag(Qt::ControlModifier))) {
            this->onCtrlGPressed();
        } else if ((qke->key() == Qt::Key_C) && (qke->modifiers().testFlag(Qt::ControlModifier))) {
            this->onCtrlCPressed();
        }
        else {
            return QWidget::keyPressEvent(qke);
        }
    }
}


void MainWindow::onReturnKeyPressed(QSerialTerminalLineEdit *stle)
{
    (void)stle;
    this->onReturnKeyPressed();
}

void MainWindow::onUpArrowPressed(QSerialTerminalLineEdit *stle)
{
    (void)stle;
    this->onUpArrowPressed();
}

void MainWindow::onDownArrowPressed(QSerialTerminalLineEdit *stle)
{
    (void)stle;
    this->onDownArrowPressed();
}

void MainWindow::onEscapeKeyPressed(QSerialTerminalLineEdit *stle)
{
    (void)stle;
    this->onEscapeKeyPressed();
}

void MainWindow::onAltKeyPressed(QSerialTerminalLineEdit *stle)
{
    (void)stle;
    this->onAltKeyPressed();
}

void MainWindow::onCtrlAPressed(QSerialTerminalLineEdit *stle)
{

    (void)stle;
    this->onCtrlAPressed();
}

void MainWindow::onCtrlEPressed(QSerialTerminalLineEdit *stle)
{

    (void)stle;
    this->onCtrlEPressed();
}

void MainWindow::onCtrlUPressed(QSerialTerminalLineEdit *stle)
{

    (void)stle;
    this->onCtrlUPressed();
}

void MainWindow::onCtrlGPressed(QSerialTerminalLineEdit *stle)
{
    (void)stle;
    this->onCtrlGPressed();
}

void MainWindow::onCtrlCPressed(QSerialTerminalLineEdit *stle)
{
    (void)stle;
    this->onCtrlCPressed();
}

void MainWindow::onReturnKeyPressed()
{
    using namespace GeneralUtilities;
    this->m_ui->sendButton->click();
}

void MainWindow::onUpArrowPressed()
{
    if ((this->m_currentHistoryIndex >= this->m_commandHistory.size()-1) || (this->m_commandHistory.empty())) {
        return;
    }
    if (this->m_currentHistoryIndex == 0) {
        if (!this->m_currentLinePushedIntoCommandHistory) {
            this->m_commandHistory.insert(this->m_commandHistory.begin(), this->m_ui->sendBox->text());
            this->m_currentLinePushedIntoCommandHistory = true;
        }
    }
    this->m_currentHistoryIndex++;
    this->m_ui->sendBox->setText(this->m_commandHistory.at(this->m_currentHistoryIndex));
}

void MainWindow::onDownArrowPressed()
{
    if (this->m_currentHistoryIndex == 0) {
        return;
    }
    this->m_ui->sendBox->setText(this->m_commandHistory.at(--this->m_currentHistoryIndex));
}

void MainWindow::onEscapeKeyPressed()
{

}

void MainWindow::onAltKeyPressed()
{

}

void MainWindow::onCtrlAPressed()
{
     this->m_ui->sendBox->setCursorPosition(0);
}

void MainWindow::onCtrlEPressed()
{
    this->m_ui->sendBox->setCursorPosition(this->m_ui->sendBox->text().size());
}

void MainWindow::onCtrlUPressed()
{
    this->m_ui->sendBox->clear();
}

void MainWindow::onCtrlGPressed()
{
    this->m_ui->terminal->clear();
}

void MainWindow::onCtrlCPressed()
{
    using namespace QSerialTerminalStrings;
    if (this->m_ui->sendButton->text() == toQString(CANCEL_SCRIPT_STRING)) {
        this->m_cancelScript = true;
    }
}

void MainWindow::onConnectButtonClicked(bool checked)
{
    if ((this->m_ui->connectButton->isChecked()) || (checked)) {
        this->onActionConnectTriggered(checked);
    } else {
        this->onActionDisconnectTriggered(checked);
    }
}

void MainWindow::onDisconnectButtonClicked(bool checked)
{
    (void)checked;
    this->onActionDisconnectTriggered(checked);
}

void MainWindow::onApplicationAboutToClose()
{
    if (this->m_serialPort && this->m_serialPort->isOpen()) {
        this->m_serialPort->closePort();
    }
}

MainWindow::~MainWindow()
{

}
