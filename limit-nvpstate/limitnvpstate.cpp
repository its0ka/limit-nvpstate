#include <limitnvpstate.h>
#include <QSystemTrayIcon>
#include <nvidia.h>
#include <QMessageBox>
#include <config.h>
#include <utils.h>
#include <rules.h>
#include <ruledialog.h>
#include <QFileDialog>
#include <QCloseEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <about.h>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

NvPhysicalGpuHandle hPhysicalGpus[NVAPI_MAX_PHYSICAL_GPUS];
HWINEVENTHOOK eventHook;
std::string unlimitTriggers[] = { "Process Running", "Process Foreground" };

// mirrors of config values that are read from other threads / the hook callback
std::atomic<int> activeGpuIndex(0);
std::atomic<int> globalPStateLimit(8);

// process running polling thread
// heap allocated and never destroyed implicitly, so exit() while the thread runs cannot terminate the process
std::thread* pollThread = nullptr;
std::mutex pollMutex;
std::condition_variable pollCondition;
bool pollStop = false;
bool pollWakeUp = false;

// applies the P-State of the matched rule, or the global limit if no rule matched (rulePState < 0)
void applyPState(int rulePState) {
    unsigned int limit = rulePState >= 0 ? rulePState : globalPStateLimit.load();

    if (setPState(hPhysicalGpus[activeGpuIndex], limit) != 0) {
        QMessageBox::critical(nullptr, "limit-nvpstate", "Error: Failed to set P-State");
        exit(1);
    }
}

void applyForegroundState() {
    HWND hwnd = GetForegroundWindow();

    // during window transitions (alt-tab, fullscreen switches, UAC) there can be no foreground window for a moment
    // keep the current state instead of dropping to the global limit while a game is about to regain focus
    if (!hwnd) {
        return;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    std::string processName = pid != 0 ? getProcessNameByPID(pid) : "";

    // same if the process can't be identified right now
    if (processName.empty()) {
        return;
    }

    int rulePState = findRulePState(processName, pid);

    std::cout << "info: " << processName << " is fg window (rule P-State: " << rulePState << ")\n";

    applyPState(rulePState);
}

void WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    // the foreground window is queried again because it can differ from the event's hwnd
    applyForegroundState();
}

void pollProcesses() {
    const int pollingInterval = (std::max)(100, config["process_running_polling"].get<int>());

    std::unique_lock<std::mutex> lock(pollMutex);

    while (!pollStop) {
        lock.unlock();

        // the lowest P-State number of all running processes with a matching rule wins
        int bestPState = -1;

        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

        if (hSnapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 processEntry;
            processEntry.dwSize = sizeof(PROCESSENTRY32);

            if (Process32First(hSnapshot, &processEntry)) {
                do {
                    int rulePState = findRulePState(wStringToString(processEntry.szExeFile), processEntry.th32ProcessID);

                    if (rulePState >= 0 && (bestPState < 0 || rulePState < bestPState)) {
                        bestPState = rulePState;
                    }

                    // nothing is faster than unlimited
                    if (bestPState == 0) {
                        break;
                    }
                } while (Process32Next(hSnapshot, &processEntry));
            }

            CloseHandle(hSnapshot);
            applyPState(bestPState);
        } else {
            std::cerr << "error: CreateToolhelp32Snapshot failed\n";
        }

        lock.lock();

        // sleep until the next poll, rule/setting changes and stopping wake the thread up early
        pollCondition.wait_for(lock, std::chrono::milliseconds(pollingInterval), []() { return pollStop || pollWakeUp; });
        pollWakeUp = false;
    }
}

limitnvpstate::limitnvpstate(QWidget* parent) : QMainWindow(parent) {
    ui.setupUi(this);
    createTrayIcon();

    if (initNvAPI() != 0) {
        QMessageBox::critical(nullptr, "limit-nvpstate", "Error: Failed to initialize NVAPI");
        exit(1);
    }

    unsigned long gpuCount = 0;
    if (NvAPI_EnumPhysicalGPUs(hPhysicalGpus, &gpuCount) != 0) {
        QMessageBox::critical(nullptr, "limit-nvpstate", "Error: Failed to enumerate physical GPUs");
        exit(1);
    }

    if (gpuCount == 0) {
        QMessageBox::critical(nullptr, "limit-nvpstate", "Error: No GPUs found");
        exit(1);
    }

    // file -> start minimized
    ui.actionStartMinimized->setChecked(config["start_minimized"]);

    connect(ui.actionStartMinimized, &QAction::triggered, [this]() {
        config["start_minimized"] = ui.actionStartMinimized->isChecked();
        saveConfig();
        });

    // file -> add to startup
    ui.actionAddToStartup->setChecked(isAddedToStartup());

    connect(ui.actionAddToStartup, &QAction::triggered, [this]() {
        addToStartup(ui.actionAddToStartup->isChecked());
        });

    // file -> exit
    connect(ui.actionExit, &QAction::triggered, [this]() {
        exitApp(0);
        });

    // help -> about
    connect(ui.actionAbout, &QAction::triggered, [this]() {
        about about;
        about.exec();
        });

    // add GPUs to combobox

    for (unsigned int i = 0; i < gpuCount; i++) {
        char gpuFullName[NVAPI_SHORT_STRING_MAX];

        if (NvAPI_GPU_GetFullName(hPhysicalGpus[i], gpuFullName) != 0) {
            QMessageBox::critical(nullptr, "limit-nvpstate", "Error: Failed to obtain GPU name");
            exit(1);
        }

        ui.selectedGPU->addItem(gpuFullName);
    }

    if (config["gpu_index"] < 0 || config["gpu_index"] >= (int)gpuCount) {
        config["gpu_index"] = 0;
        saveConfig();
    }

    activeGpuIndex = config["gpu_index"].get<int>();
    ui.selectedGPU->setCurrentIndex(activeGpuIndex);
    connect(ui.selectedGPU, SIGNAL(currentIndexChanged(int)), this, SLOT(selectedGPUChanged(int)));

    // add P-States to combobox
    getAvailablePStates();
    connect(ui.selectedPState, SIGNAL(currentIndexChanged(int)), this, SLOT(selectedPStateChanged(int)));

    // add states to combobox
    for (std::string unlimitTrigger : unlimitTriggers) {
        ui.unlimitTrigger->addItem(QString::fromStdString(unlimitTrigger));
    }

    int triggerCount = sizeof(unlimitTriggers) / sizeof(unlimitTriggers[0]);
    if (config["unlimit_trigger"] < 0 || config["unlimit_trigger"] > triggerCount) {
        QMessageBox::critical(nullptr, "limit-nvpstate", "Error: Invalid unlimit_trigger index in config");
        exit(1);
    }

    ui.unlimitTrigger->setCurrentIndex(config["unlimit_trigger"]);
    connect(ui.unlimitTrigger, SIGNAL(currentIndexChanged(int)), this, SLOT(unlimitTriggerChanged(int)));

    // process rules (list of exceptions with their own P-State)
    ui.processExceptionsList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui.processExceptionsList->setTextElideMode(Qt::ElideMiddle);

    bool isConfigMigrated = false;
    processRules = loadRulesFromConfig(isConfigMigrated);

    // a P-State that isn't available for the GPU can't be applied, fall back to unlimited
    for (ProcessRule& rule : processRules) {
        if (rule.pstate != 0 && std::find(availablePStates.begin(), availablePStates.end(), rule.pstate) == availablePStates.end()) {
            std::cerr << "info: P" << rule.pstate << " of " << rule.pattern << " is not available, using P0\n";
            rule.pstate = 0;
            isConfigMigrated = true;
        }
    }

    if (isConfigMigrated) {
        // old config format or invalid entries
        storeRulesToConfig(processRules);
        saveConfig();
    }

    setActiveRules(processRules);
    refreshRuleList();

    // add process button
    connect(ui.addProcess, &QPushButton::clicked, [this]() {
        addProcess();
        });

    // edit process button / double click
    connect(ui.editProcess, &QPushButton::clicked, [this]() {
        editProcess();
        });

    connect(ui.processExceptionsList, &QListWidget::itemDoubleClicked, [this](QListWidgetItem*) {
        editProcess();
        });

    // remove process button
    connect(ui.removeProcess, &QPushButton::clicked, [this]() {
        std::vector<int> rows;

        for (QListWidgetItem* processException : ui.processExceptionsList->selectedItems()) {
            rows.push_back(ui.processExceptionsList->row(processException));
        }

        // erase from the back so the remaining indices stay valid
        std::sort(rows.begin(), rows.end(), std::greater<int>());

        for (int row : rows) {
            processRules.erase(processRules.begin() + row);
        }

        saveProcessRules();
        });

    // limit p-state initially
    applyPState(-1);

    if (config["unlimit_trigger"] == 0) {
        setupProcessRunningTrigger();
    } else if (config["unlimit_trigger"] == 1) {
        setupProcessForegroundTrigger();
    }
}

void limitnvpstate::setupProcessRunningTrigger() {
    {
        std::lock_guard<std::mutex> lock(pollMutex);
        pollStop = false;
        pollWakeUp = false;
    }

    pollThread = new std::thread(pollProcesses);
}

void limitnvpstate::stopProcessRunningTrigger() {
    {
        std::lock_guard<std::mutex> lock(pollMutex);
        pollStop = true;
    }

    pollCondition.notify_all();

    if (pollThread) {
        pollThread->join();
        delete pollThread;
        pollThread = nullptr;
    }
}

void limitnvpstate::setupProcessForegroundTrigger() {
    eventHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

    if (!eventHook) {
        QMessageBox::critical(nullptr, "limit-nvpstate", "Error: Failed to configure global hook");
        exit(1);
    }

    // safety net: foreground events can be missed or arrive while the foreground window is in transition,
    // so the state is re-checked periodically (cheap, the applied P-State is cached)
    if (!foregroundTimer) {
        foregroundTimer = new QTimer(this);
        connect(foregroundTimer, &QTimer::timeout, []() {
            applyForegroundState();
            });
    }

    foregroundTimer->start(1000);
}

void limitnvpstate::stopProcessForegroundTrigger() {
    if (foregroundTimer) {
        foregroundTimer->stop();
    }

    if (!UnhookWinEvent(eventHook)) {
        QMessageBox::critical(nullptr, "limit-nvpstate", "Error: Failed to unhook global hook");
        exit(1);
    }
}

void limitnvpstate::createTrayIcon() {
    QMenu* trayMenu = new QMenu(this);

    QAction* trayActionExit = new QAction("Exit", this);
    connect(trayActionExit, &QAction::triggered, [this]() {
        exitApp(0);
        });
    trayMenu->addAction(trayActionExit);

    // create icon
    auto trayIcon = new QSystemTrayIcon(this);
    trayIcon->setContextMenu(trayMenu);
    trayIcon->setIcon(QIcon(":/limitnvpstate/icon.ico"));
    trayIcon->show();

    connect(trayIcon, &QSystemTrayIcon::activated, [this](QSystemTrayIcon::ActivationReason  reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            if (isVisible()) {
                hide();
            } else {
                showNormal();
                activateWindow();
            }
        }
        });
}

void limitnvpstate::unlimitTriggerChanged(int index) {
    // clean up previous trigger method
    int prevUnlimitTriggerIndex = config["unlimit_trigger"];

    if (prevUnlimitTriggerIndex == 0) {
        stopProcessRunningTrigger();
    } else if (prevUnlimitTriggerIndex == 1) {
        stopProcessForegroundTrigger();
    }

    // setup new trigger method
    if (index == 0) {
        setupProcessRunningTrigger();
    } else if (index == 1) {
        setupProcessForegroundTrigger();
    }

    config["unlimit_trigger"] = index;
    saveConfig();

    // apply the state for the current situation right away
    reevaluatePState();
}

void limitnvpstate::reevaluatePState() {
    if (config["unlimit_trigger"] == 0) {
        // wake up the polling thread so it doesn't wait for the next interval
        {
            std::lock_guard<std::mutex> lock(pollMutex);
            pollWakeUp = true;
        }

        pollCondition.notify_all();
    } else {
        applyForegroundState();
    }
}

void limitnvpstate::selectedGPUChanged(int index) {
    // unlimit P-State for current GPU
    int prevGpuIndex = activeGpuIndex;

    config["gpu_index"] = index;
    saveConfig();
    activeGpuIndex = index;

    if (setPState(hPhysicalGpus[prevGpuIndex], 0) != 0) {
        QMessageBox::critical(nullptr, "limit-nvpstate", "Error: Failed to set P-State");
        exit(1);
    }

    // get available P-States for new GPU selection
    getAvailablePStates();

    // limit P-State for the new GPU
    reevaluatePState();
}

void limitnvpstate::selectedPStateChanged(int index) {
    // when changing GPU, clear() is called on the combobox which triggers this with index -1 and causes a crash
    if (index < 0) {
        return;
    }

    std::string selectedPstate = ui.selectedPState->currentText().toStdString().substr(1);
    config["pstate_limit"] = std::stoi(selectedPstate);
    globalPStateLimit = config["pstate_limit"].get<int>();
    saveConfig();

    // the new global limit is applied unless a process rule is active
    reevaluatePState();
}

void limitnvpstate::saveProcessRules() {
    storeRulesToConfig(processRules);
    saveConfig();

    // update the rules used for matching
    setActiveRules(processRules);
    refreshRuleList();

    reevaluatePState();
}

void limitnvpstate::refreshRuleList() {
    ui.processExceptionsList->clear();

    for (const ProcessRule& rule : processRules) {
        QListWidgetItem* item = new QListWidgetItem(QString::fromStdString(describeRule(rule)));
        item->setToolTip(item->text());
        ui.processExceptionsList->addItem(item);
    }
}

void limitnvpstate::closeEvent(QCloseEvent* event) {
    // hide instead of closing
    hide();
    event->ignore();
}

void limitnvpstate::changeEvent(QEvent* e) {
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange) {
        if (isMinimized()) {
            hide();
        }
    }
}

namespace {
    bool isSameTarget(const ProcessRule& a, const ProcessRule& b) {
        return a.matchPath == b.matchPath && toLower(a.pattern) == toLower(b.pattern);
    }
}

void limitnvpstate::addProcess() {
    RuleDialog dialog(this, availablePStates);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    for (const ProcessRule& newRule : dialog.rules()) {
        // an existing entry for the same target is updated instead of duplicated
        auto existing = std::find_if(processRules.begin(), processRules.end(), [&](const ProcessRule& rule) {
            return isSameTarget(rule, newRule);
            });

        if (existing != processRules.end()) {
            *existing = newRule;
        } else {
            processRules.push_back(newRule);
        }
    }

    saveProcessRules();
}

void limitnvpstate::editProcess() {
    QList<QListWidgetItem*> selectedItems = ui.processExceptionsList->selectedItems();

    if (selectedItems.size() != 1) {
        return;
    }

    int row = ui.processExceptionsList->row(selectedItems.first());
    ProcessRule current = processRules[row];

    RuleDialog dialog(this, availablePStates, &current);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const ProcessRule edited = dialog.rules().front();
    processRules[row] = edited;

    // remove other entries that now point at the same target
    for (int i = (int)processRules.size() - 1; i >= 0; i--) {
        if (i != row && isSameTarget(processRules[i], edited)) {
            processRules.erase(processRules.begin() + i);
        }
    }

    saveProcessRules();
}

void limitnvpstate::getAvailablePStates() {
    NV_GPU_PERF_PSTATES20_INFO pStatesInfo;
    pStatesInfo.version = NV_GPU_PERF_PSTATES20_INFO_VER;

    if (NvAPI_GPU_GetPstates20(hPhysicalGpus[activeGpuIndex], &pStatesInfo) != 0) {
        QMessageBox::critical(nullptr, "limit-nvpstate", "Error: Failed to obtain available P-States");
        exit(1);
    }

    // don't treat the changes made below as user selections
    QSignalBlocker blocker(ui.selectedPState);

    // clear any items already added
    ui.selectedPState->clear();

    // used to validate selection and as choices for process rules
    availablePStates.clear();

    for (int i = 1; i < pStatesInfo.numPstates; i++) {
        int pState = pStatesInfo.pstates[i].pstateId;
        availablePStates.push_back(pState);

        ui.selectedPState->addItem(QString::fromStdString("P" + std::to_string(pState)));
    }

    // handle invalid selection
    bool isSelectedPStateValid = std::find(availablePStates.begin(), availablePStates.end(), config["pstate_limit"]) != availablePStates.end();

    if (!isSelectedPStateValid) {
        config["pstate_limit"] = availablePStates.back();
        saveConfig();
    }

    // set option in interface
    std::string selectedPstate = "P" + std::to_string((int)config["pstate_limit"]);
    QString selectedPStateQString = QString::fromStdString(selectedPstate);
    ui.selectedPState->setCurrentText(selectedPStateQString);

    globalPStateLimit = config["pstate_limit"].get<int>();
}

void limitnvpstate::exitApp(int exitCode) {
    // stop the triggers first so they can't limit the P-State again
    if (config["unlimit_trigger"] == 0) {
        stopProcessRunningTrigger();
    } else if (config["unlimit_trigger"] == 1) {
        stopProcessForegroundTrigger();
    }

    if (setPState(hPhysicalGpus[activeGpuIndex], 0) != 0) {
        QMessageBox::critical(nullptr, "limit-nvpstate", "Error: Failed to set P-State");
        exit(1);
    }

    exit(exitCode);
}
